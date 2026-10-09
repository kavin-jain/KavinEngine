#!/usr/bin/env python3
"""Lichess bot operations for the Telegram bot on lynxS (Hermes quick commands and the 8:00 cron report).
Plain-text output, short enough for one Telegram message. Runs with lichess-bot's venv (python-chess).
  bot_ops.py status            bot online?, current game, ratings, today's score, server health
  bot_ops.py report [HOURS]    the last HOURS (24): games, W/D/L per speed, time losses, rating change, losses
  bot_ops.py last              the last finished game
  bot_ops.py analyze [ID]      Stockfish 19 analysis of game ID (default: last finished), sent to Telegram when done
  bot_ops.py watchdog          (systemd timer, every 5 min) restart lichess-bot and tell Telegram if the bot is offline
                               or has been idle > 20 min without a reason (at most one restart per 30 min)
"""
import json, pathlib, re, subprocess, sys, time, urllib.request

BOT = "KavinEngine"
HOME = pathlib.Path.home()
TOKEN = (HOME / ".lichess-token").read_text().strip()
STOCKFISH = HOME / "tools/stockfish/src/stockfish"
HERMES = HOME / ".local/bin/hermes"
SYZYGY = HOME / "Projects/lichess-bot/engines/syzygy"
STATE = HOME / ".cache/kavinengine-bot/state.json"
ANALYZE = pathlib.Path(__file__).with_name("analyze_game.py")


def api(path, ndjson=False):
    req = urllib.request.Request("https://lichess.org" + path, headers={
        "Authorization": f"Bearer {TOKEN}", "Accept": "application/x-ndjson" if ndjson else "application/json"})
    body = urllib.request.urlopen(req, timeout=30).read().decode()
    return [json.loads(l) for l in body.splitlines() if l.strip()] if ndjson else json.loads(body)


def games_since(hours):
    since = int((time.time() - hours * 3600) * 1000)
    return api(f"/api/games/user/{BOT}?since={since}&max=500&moves=false", ndjson=True)


def side(g):  # (our colour, opponent dict, our player dict)
    us = "white" if g["players"]["white"].get("user", {}).get("name") == BOT else "black"
    return us, g["players"]["black" if us == "white" else "white"], g["players"][us]


def outcome(g):  # "W", "D", "L" or None (unfinished)
    if g["status"] in ("created", "started"): return None
    us, _, _ = side(g)
    return "D" if "winner" not in g else "W" if g["winner"] == us else "L"


def link(g): return f"lichess.org/{g['id']}"


def name(p): return f"{p.get('user', {}).get('name', '?')} ({p.get('rating', '?')})"


def ratings():
    perfs = api(f"/api/user/{BOT}")["perfs"]
    return ", ".join(f"{k} {v['rating']}{'?' if v.get('prov') else ''} ({v['games']} games)"
                     for k, v in perfs.items() if k in ("bullet", "blitz", "rapid", "classical") and v.get("games"))


def wifi_drops():  # disconnects of the bot's Wi-Fi since the last report (each one is 2 carrier changes)
    now = int(pathlib.Path("/sys/class/net/wlp2s0/carrier_changes").read_text())
    state = json.loads(STATE.read_text()) if STATE.exists() else {}
    last = state.get("carrier_changes", now)
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({"carrier_changes": now, "at": time.time()}))
    return max(0, now - last) // 2


def tablebases():
    n = len(list(SYZYGY.glob("*.rtbw"))) + len(list(SYZYGY.glob("*.rtbz")))
    size = sum(f.stat().st_size for f in SYZYGY.glob("*.rtb?")) / 1e9
    return f"{n}/1020 files ({size:.0f} GB)" + (" complete" if n >= 1020 else ", 6-piece downloading" if n > 290 else "")


def score_line(gs):
    res = [outcome(g) for g in gs if outcome(g)]
    w, d, l = res.count("W"), res.count("D"), res.count("L")
    pct = f", score {100 * (w + d / 2) / len(res):.0f}%" if res else ""
    return f"+{w} ={d} -{l}{pct}"


def cmd_status():
    online = subprocess.run(["systemctl", "--user", "is-active", "lichess-bot"], capture_output=True, text=True).stdout.strip()
    st = api(f"/api/users/status?ids={BOT}&withGameIds=true")[0]
    today = games_since((time.time() - time.mktime(time.localtime()[:3] + (0, 0, 0, 0, 0, -1))) / 3600)
    lines = [f"♟ {BOT}: service {online}, Lichess {'online' if st.get('online') else 'OFFLINE'}",
             f"Now playing: lichess.org/{st['playingId']}" if st.get("playingId") else "Now: between games",
             f"Ratings: {ratings()}",
             f"Today: {len([g for g in today if outcome(g)])} games {score_line(today)}",
             f"Tablebases: {tablebases()}",
             f"Uptime: {subprocess.run(['uptime', '-p'], capture_output=True, text=True).stdout.strip()}"]
    print("\n".join(lines))


def cmd_report(hours=24):
    gs = [g for g in games_since(hours) if outcome(g)]
    lines = [f"♟ {BOT} — last {hours} h", f"Games {len(gs)}: {score_line(gs)}"]
    for speed in ("bullet", "blitz", "rapid", "classical"):
        sub = [g for g in gs if g["speed"] == speed]
        if sub:
            diff = sum(side(g)[2].get("ratingDiff", 0) for g in sub if g.get("rated"))
            lines.append(f"  {speed}: {len(sub)} games {score_line(sub)}, rating {diff:+d}")
    flags = [g for g in gs if g["status"] == "outoftime" and outcome(g) == "L"]
    lines.append(f"Lost on time: {len(flags)}" + (" ⚠" if flags else ""))
    wins = [g for g in gs if outcome(g) == "W"]
    if wins:
        best = max(wins, key=lambda g: side(g)[1].get("rating", 0))
        lines.append(f"Best win: {name(side(best)[1])} {link(best)}")
    losses = [g for g in gs if outcome(g) == "L"]
    for g in losses[:5]:
        lines.append(f"Loss: {name(side(g)[1])}, {g['speed']}, {g['status']} {link(g)}")
    if len(losses) > 5: lines.append(f"…and {len(losses) - 5} more losses")
    lines += [f"Ratings now: {ratings()}", f"Wi-Fi drops since last report: {wifi_drops()}", f"Tablebases: {tablebases()}",
              "Analyse a loss: /analyze (last game) or ask Hermes \"analyse game <id>\""]
    print("\n".join(lines))


def last_game():
    gs = [g for g in api(f"/api/games/user/{BOT}?max=5&moves=false", ndjson=True) if outcome(g)]
    if not gs: sys.exit("no finished game found")
    return gs[0]


def cmd_last():
    g = last_game()
    us, opp, me = side(g)
    res = {"W": "won", "D": "drew", "L": "lost"}[outcome(g)]
    clock = g.get("clock", {})
    print(f"♟ Last game: {res} vs {name(opp)} as {us}, {g['speed']} {clock.get('initial', 0) // 60}+{clock.get('increment', 0)}, "
          f"{g['status']}, rating {me.get('ratingDiff', 0):+d}\n{link(g)}")


def cmd_analyze(gid=None, run=False):
    """Without run: start a detached background job and return at once (Hermes quick commands time out at 30 s)."""
    gid = gid or last_game()["id"]
    if not run:
        subprocess.Popen([sys.executable, __file__, "analyze", gid, "--run"], start_new_session=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return print(f"🔍 Analysing lichess.org/{gid} with Stockfish 19 (~3-5 min, low priority so the bot keeps its "
                     f"speed). The result will arrive here.")
    out = subprocess.run(["nice", "-n", "19", sys.executable, str(ANALYZE), gid, str(STOCKFISH), "500000"],
                         capture_output=True, text=True, timeout=3600).stdout.splitlines()
    head, moves = out[:1], out[1:]
    trend = [t for t in (m.split() for m in moves) if len(t) >= 3 and re.fullmatch(r"\d+\.", t[0]) and int(t[0][:-1]) % 10 == 0]
    mistakes = [m.strip() for m in moves if "MISTAKE" in m]
    ours = [m for m in mistakes if f"by {'White' if BOT in head[0].split(' vs ')[0] else 'Black'}" in m]
    body = head + [f"Stockfish eval (+ = White): " + " | ".join(f"m{t[0][:-1]} {int(t[2]) / 100:+.1f}" for t in trend
                                                                 if t[2].lstrip('-').isdigit())]
    body += [f"Our mistakes ({len(ours)}):"] + ours[:12] + [f"All mistakes, both sides: {len(mistakes)}", f"lichess.org/{gid}"]
    sent = subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], input="\n".join(body), text=True).returncode
    with open(STATE.parent / "analyze.log", "a") as log:  # background job: the only trace of what was sent
        log.write(f"{time.strftime('%F %T')} {gid} hermes-send exit {sent}\n" + "\n".join(body) + "\n\n")


IDLE_LIMIT, RESTART_GAP, BOT_GAMES_PER_DAY = 20 * 60, 30 * 60, 100  # Lichess allows 100 bot-vs-bot games a day


def cmd_watchdog():
    """Independent of lichess-bot's internals: if it stops playing for any reason, restart it and say so."""
    path = STATE.with_name("watchdog.json")  # own file: wifi_drops() rewrites state.json whole
    wd = json.loads(path.read_text()) if path.exists() else {}
    now = time.time()
    status = api(f"/api/users/status?ids={BOT}")[0]
    if status.get("playing"):
        wd["last_active"] = now
    else:
        last = api(f"/api/games/user/{BOT}?max=1&moves=false", ndjson=True)
        if last:
            wd["last_active"] = max(wd.get("last_active", 0), last[0].get("lastMoveAt", 0) / 1000)
    idle = now - wd.get("last_active", now)
    reason = None
    if not status.get("online"):
        reason = "offline on Lichess"
    elif idle > IDLE_LIMIT:
        day = games_since(24)
        bot_games = sum(1 for g in day if side(g)[1].get("user", {}).get("title") == "BOT")
        if bot_games >= BOT_GAMES_PER_DAY:
            if not wd.get("cap_reported"):
                wd["cap_reported"] = True
                subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], text=True,
                               input=f"♟ {BOT} reached Lichess's {BOT_GAMES_PER_DAY} bot games in 24 h; it waits for humans "
                                     f"or for the window to roll over. No restart needed.")
        else:
            reason = f"idle for {idle / 60:.0f} min ({bot_games} bot games in 24 h)"
    if status.get("playing") or idle < IDLE_LIMIT:
        wd["cap_reported"] = False
    if reason and now - wd.get("last_restart", 0) > RESTART_GAP:
        wd["last_restart"] = now
        ok = subprocess.run(["systemctl", "--user", "restart", "lichess-bot.service"]).returncode == 0
        msg = f"⚠️ Watchdog: {BOT} was {reason}; lichess-bot {'restarted' if ok else 'restart FAILED'}."
        subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], input=msg, text=True)
        print(msg)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(wd))
    print(f"{time.strftime('%F %T')} online={status.get('online')} playing={bool(status.get('playing'))} "
          f"idle={idle / 60:.1f} min{' -> ' + reason if reason else ''}")


if __name__ == "__main__":
    cmd, args = (sys.argv[1] if len(sys.argv) > 1 else "status"), sys.argv[2:]
    if cmd == "status": cmd_status()
    elif cmd == "report": cmd_report(int(args[0]) if args else 24)
    elif cmd == "last": cmd_last()
    elif cmd == "analyze": cmd_analyze(next((a for a in args if a != "--run"), None), "--run" in args)
    elif cmd == "watchdog": cmd_watchdog()
    else: sys.exit(__doc__)
