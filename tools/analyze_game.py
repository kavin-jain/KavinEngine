#!/usr/bin/env python3
"""Analyse a Lichess game with a strong UCI engine: eval after every move (White's view, centipawns) and each side's
mistakes (the mover's eval drops >= 100 cp). Usage: analyze_game.py GAME_ID ENGINE [NODES=2000000]"""
import io, sys, urllib.request
import chess, chess.engine, chess.pgn

gid, engine_cmd = sys.argv[1], sys.argv[2]
nodes = int(sys.argv[3]) if len(sys.argv) > 3 else 2_000_000
pgn = urllib.request.urlopen(f"https://lichess.org/game/export/{gid}?clocks=false&evals=false").read().decode()
game = chess.pgn.read_game(io.StringIO(pgn))
h = game.headers
print(f"{h['White']} ({h.get('WhiteElo')}) vs {h['Black']} ({h.get('BlackElo')}), {h['TimeControl']}, "
      f"{h['Result']} {h.get('Termination')}")
eng = chess.engine.SimpleEngine.popen_uci(engine_cmd)
eng.configure({"Hash": 64})  # also runs on lynxS next to the Lichess bot


def analyse(board):  # (eval in cp from White's view, best move); game over: exact result
    if board.is_game_over():
        r = board.result()
        return (10000 if r == "1-0" else -10000 if r == "0-1" else 0), None
    info = eng.analyse(board, chess.engine.Limit(nodes=nodes))
    return info["score"].white().score(mate_score=10000), info["pv"][0]


board = game.board()
ev, best = analyse(board)
for mv in game.mainline_moves():
    num, san, mover = f"{board.fullmove_number}{'.' if board.turn else '...'}", board.san(mv), board.turn
    best_san = board.san(best) if best else ""
    board.push(mv)
    after, next_best = analyse(board)
    drop = (ev - after) if mover == chess.WHITE else (after - ev)
    note = f"  MISTAKE by {'White' if mover else 'Black'}: -{drop} cp, best was {best_san}" if drop >= 100 and mv != best else ""
    print(f"{num:>7} {san:>8} {after:>7}{note}")
    ev, best = after, next_best
eng.quit()
