#!/usr/bin/env python3
"""Daily review of the Lichess bot's games (game-review.yml): Stockfish grades every move of ours, then the current
engine replays each blunder position to see whether it still goes wrong. Writes OUT/report.md (also the job
summary), OUT/moves.json (every graded move of ours) and OUT/open.epd (blunders the current engine still plays,
with Stockfish's move as bm: the to-fix list).
  review_games.py --hours 25 --stockfish SF --engine ENGINE --out DIR [--nodes 500000] [--movetime 3]
Grades use Lichess's win-percentage curve, so a 200 cp slip in a won position doesn't count like one at 0.00:
mistake = our win chance drops >= 10 points, blunder >= 20."""
import argparse, io, json, math, pathlib, time, urllib.request
import chess, chess.engine, chess.pgn

BOT = "KavinEngine"


def win_pct(cp):  # lichess.org/page/accuracy
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * cp)) - 1)


def fetch(hours):
    since = int((time.time() - hours * 3600) * 1000)
    req = urllib.request.Request(f"https://lichess.org/api/games/user/{BOT}?since={since}&clocks=true&opening=true"
                                 "&perfType=bullet,blitz,rapid,classical", headers={"Accept": "application/x-chess-pgn"})
    text = urllib.request.urlopen(req, timeout=120).read().decode()
    games, f = [], io.StringIO(text)
    while (g := chess.pgn.read_game(f)) is not None:
        games.append(g)
    return games


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hours", type=float, default=25); ap.add_argument("--nodes", type=int, default=500_000)
    ap.add_argument("--movetime", type=float, default=3.0)
    for f in ("stockfish", "engine", "out"): ap.add_argument("--" + f, required=True)
    a = ap.parse_args()
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
    sf = chess.engine.SimpleEngine.popen_uci(a.stockfish); sf.configure({"Threads": 4, "Hash": 256})

    def ev(board, us):  # (our-POV cp, Stockfish's best move); finished games score exactly
        if board.is_game_over(claim_draw=True):
            r = board.result(claim_draw=True)
            return (0 if r == "1/2-1/2" else 10000 if (r == "1-0") == (us == chess.WHITE) else -10000), None
        info = sf.analyse(board, chess.engine.Limit(nodes=a.nodes))
        return info["score"].pov(us).score(mate_score=10000), info.get("pv", [None])[0]

    games, moves, rows = fetch(a.hours), [], []
    for g in games:
        h = g.headers
        if h.get("Variant", "Standard") != "Standard" or BOT not in (h["White"], h["Black"]): continue
        us = chess.WHITE if h["White"] == BOT else chess.BLACK
        nodes = list(g.mainline())
        if len(nodes) < 4: continue  # aborted
        gid, base = h["Site"].rsplit("/", 1)[-1], int(h.get("TimeControl", "0+0").split("+")[0] or 0)
        board, (cur, best), best_wp = g.board(), ev(g.board(), us), 0
        for n in nodes:
            mover, fen, san = board.turn, board.fen(), board.san(n.move)
            best_san = board.san(best) if best else ""
            board.push(n.move)
            nxt, nxt_best = ev(board, us)
            best_wp = max(best_wp, win_pct(cur))
            if mover == us:
                clock = n.clock()
                pieces = sum(1 for p in board.piece_map().values() if p.piece_type not in (chess.PAWN, chess.KING))
                moves.append({"game": gid, "ply": board.ply(), "fen": fen, "move": san, "best": best_san,
                              "cp_before": cur, "cp_after": nxt, "drop": round(win_pct(cur) - win_pct(nxt), 1),
                              "clock": clock, "time_trouble": clock is not None and clock < max(10, base / 10),
                              "phase": "opening" if board.ply() <= 20 else "endgame" if pieces <= 6 else "middlegame",
                              "speed": h.get("Event", ""), "opponent": h["Black" if us else "White"]})
            cur, best = nxt, nxt_best
        res = h["Result"]
        score = 0.5 if res == "1/2-1/2" else 1.0 if (res == "1-0") == (us == chess.WHITE) else 0.0
        rows.append({"game": gid, "color": "white" if us else "black", "opponent": h["Black" if us else "White"],
                     "elo": h.get("BlackElo" if us else "WhiteElo"), "tc": h.get("TimeControl"), "score": score,
                     "termination": h.get("Termination", ""), "best_win_pct": round(best_wp)})
    # The current engine replays every blunder at a fixed time; Stockfish grades its new choice.
    eng = chess.engine.SimpleEngine.popen_uci(a.engine); eng.configure({"Threads": 1, "Hash": 64})
    blunders = [m for m in moves if m["drop"] >= 20]
    open_epd = []
    for m in blunders:
        board = chess.Board(m["fen"]); us = board.turn
        mv = eng.play(board, chess.engine.Limit(time=a.movetime)).move
        m["now"] = board.san(mv)
        board.push(mv)
        m["now_drop"] = round(win_pct(m["cp_before"]) - win_pct(ev(board, us)[0]), 1)
        if m["now_drop"] >= 10:
            b = chess.Board(m["fen"])
            open_epd.append(b.epd(bm=b.parse_san(m["best"]), id=f"{m['game']}/{m['ply']}") if m["best"] else b.epd())
    eng.quit(); sf.quit()

    (out / "moves.json").write_text(json.dumps({"games": rows, "moves": moves}, indent=1))
    (out / "open.epd").write_text("\n".join(open_epd) + ("\n" if open_epd else ""))
    n, w = len(rows), sum(r["score"] for r in rows)
    mist = [m for m in moves if 10 <= m["drop"] < 20]
    rep = [f"# Game review: last {a.hours:g} h", "",
           f"**{n} games, score {w:g}/{n}** "
           f"(W{sum(r['score'] == 1 for r in rows)} D{sum(r['score'] == .5 for r in rows)} L{sum(r['score'] == 0 for r in rows)}), "
           f"{len(moves)} of our moves graded by Stockfish at {a.nodes:,} nodes.", "",
           "| | Blunders (≥ 20 %) | Mistakes (10–20 %) |", "|---|---|---|"]
    for ph in ("opening", "middlegame", "endgame"):
        rep.append(f"| {ph} | {sum(m['phase'] == ph for m in blunders)} | {sum(m['phase'] == ph for m in mist)} |")
    rep.append(f"| in time trouble | {sum(m['time_trouble'] for m in blunders)} | {sum(m['time_trouble'] for m in mist)} |")
    rep += ["", f"**Still wrong on the current engine: {len(open_epd)} of {len(blunders)} blunders** "
            f"({a.movetime:g} s, 1 thread; list in open.epd)", ""]
    link = lambda m: f"[{m['game']}#{m['ply']}](https://lichess.org/{m['game']}#{m['ply']})"
    if blunders:
        rep += ["| Position | Phase | Clock | Played | Stockfish | Drop | Now plays | Now drop |", "|---|---|---|---|---|---|---|---|"]
        for m in sorted(blunders, key=lambda m: -m["drop"]):
            rep.append(f"| {link(m)} | {m['phase']} | {m['clock'] if m['clock'] is not None else '?'} | {m['move']} | "
                       f"{m['best']} | {m['drop']:.0f} | {m['now']} | {m['now_drop']:.0f} |")
    bad = [r for r in rows if r["score"] < 1 and r["best_win_pct"] >= 85]
    timeouts = [r for r in rows if r["score"] < 1 and "Time" in r["termination"]]
    rep += ["", f"**Won positions not won** (win chance ≥ 85 % at some point): {len(bad)}"]
    rep += [f"- [{r['game']}](https://lichess.org/{r['game']}) vs {r['opponent']} ({r['elo']}), {r['tc']}, "
            f"score {r['score']:g}, peak {r['best_win_pct']} %" for r in bad]
    rep += ["", f"**Lost or drawn on time:** {len(timeouts)}"]
    rep += [f"- [{r['game']}](https://lichess.org/{r['game']}) vs {r['opponent']}, {r['tc']}" for r in timeouts]
    (out / "report.md").write_text("\n".join(rep) + "\n")
    print("\n".join(rep))


if __name__ == "__main__":
    main()
