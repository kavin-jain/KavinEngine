#!/usr/bin/env python3
"""Lichess bot operations for the Telegram bot on lynxS (Hermes quick commands and the 8:00 cron report).
Plain-text output, short enough for one Telegram message. Runs with lichess-bot's venv (python-chess).
  bot_ops.py status            bot online?, current game, ratings, today's score, server health
  bot_ops.py report [HOURS]    the last HOURS (24): games, W/D/L per speed, time losses, rating change, losses
  bot_ops.py last              the last finished game
  bot_ops.py analyze [ID]      Stockfish 19 analysis of game ID (default: last finished), sent to Telegram when done
  bot_ops.py on | off          switch the bot on / off (Telegram /chessboton, /chessbotoff); off plays the running game out
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
BOT_HOST = "oracle"  # lichess-bot's machine: the Oracle VM since 2026-10-11 (lynxS's home link drops); "" = this one


def on_bot_host(cmd): return (["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", BOT_HOST] if BOT_HOST else []) + cmd


OFF_FLAG = ".chessbot-off"  # in BOT_HOST's home; lichess-bot.service: ExecCondition=/usr/bin/test ! -e %h/.chessbot-off
WATCHDOG = STATE.with_name("watchdog.json")  # own file: link_drops() rewrites state.json whole


def host_sh(script, timeout=20):  # a shell snippet on BOT_HOST, run in its home; returncode 255 = ssh failed
    return subprocess.run(on_bot_host([script]) if BOT_HOST else ["sh", "-c", script], cwd=HOME,
                          capture_output=True, text=True, timeout=timeout)


def switch_state():
    """('on' | 'off', lichess-bot.service state) on BOT_HOST; raises when the host can't be reached."""
    r = host_sh(f"test -e {OFF_FLAG} && echo off || echo on; systemctl --user is-active lichess-bot")
    out = r.stdout.split()
    if r.returncode == 255 or len(out) != 2:
        raise RuntimeError(f"can't reach {BOT_HOST}: {(r.stderr or r.stdout).strip()[-200:]}")
    return out[0], out[1]


def wd_load(): return json.loads(WATCHDOG.read_text()) if WATCHDOG.exists() else {}


def wd_save(wd):
    WATCHDOG.parent.mkdir(parents=True, exist_ok=True)
    WATCHDOG.write_text(json.dumps(wd))


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


def link_drops():  # disconnects per link since the last report (each is 2 carrier changes); Ethernet is the bot's
    state = json.loads(STATE.read_text()) if STATE.exists() else {}  # link since 2026-10-09, Wi-Fi the backup
    now, out = {}, []
    for label, dev in (("Ethernet", "enp1s0"), ("Wi-Fi", "wlp2s0")):
        f = pathlib.Path(f"/sys/class/net/{dev}/carrier_changes")
        if f.exists():
            now[dev] = int(f.read_text())
            out.append(f"{label} {max(0, now[dev] - state.get(dev, now[dev])) // 2}")
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({**now, "at": time.time()}))
    return ", ".join(out) or "no link counters"


def tablebases():
    n = len(list(SYZYGY.glob("*.rtbw"))) + len(list(SYZYGY.glob("*.rtbz")))
    size = sum(f.stat().st_size for f in SYZYGY.glob("*.rtb?")) / 1e9
    return f"{n} files ({size:.1f} GB): " + ("3-4-5 complete, up to 7 pieces online" if n >= 290 else "3-4-5 INCOMPLETE")


def score_line(gs):
    res = [outcome(g) for g in gs if outcome(g)]
    w, d, l = res.count("W"), res.count("D"), res.count("L")
    pct = f", score {100 * (w + d / 2) / len(res):.0f}%" if res else ""
    return f"+{w} ={d} -{l}{pct}"


def cmd_status():
    online = subprocess.run(on_bot_host(["systemctl", "--user", "is-active", "lichess-bot"]), capture_output=True, text=True).stdout.strip()
    st = api(f"/api/users/status?ids={BOT}&withGameIds=true")[0]
    today = games_since((time.time() - time.mktime(time.localtime()[:3] + (0, 0, 0, 0, 0, -1))) / 3600)
    try: switched = ", switched OFF (/chessboton to resume)" if switch_state()[0] == "off" else ""
    except Exception as e: switched = f", on/off switch unknown ({e})"
    lines = [f"♟ {BOT}: service {online}{switched}, Lichess {'online' if st.get('online') else 'OFFLINE'}",
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
    lines += [f"Ratings now: {ratings()}", f"Link drops since last report: {link_drops()}", f"Tablebases: {tablebases()}",
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


def cmd_switch(on):
    """/chessboton, /chessbotoff (Hermes quick commands: 30 s cap). Off never forfeits a game: lichess-bot gets SIGINT
    (config quit_after_all_games_finish: true), takes no new games, plays the running one out and exits; the flag then
    keeps it off through crash restarts, reboots, the watchdog and arena joins. On removes the flag and starts the
    service; if it is still finishing a game, systemd's own restart after that exit brings it straight back."""
    try:
        flag, svc = switch_state()
        wd = wd_load()
        if on:
            if flag == "on" and svc == "active":
                return print("♟ Chess bot is already ON.")
            r = host_sh(f"rm -f {OFF_FLAG} && systemctl --user start lichess-bot")
            if r.returncode:
                return print(f"⚠️ Chess bot ON failed: {(r.stderr or r.stdout).strip()[-300:]}")
            wd.update(last_active=time.time(), off_reported=False, off_idle=0)  # the watchdog's idle clock starts now
            wd_save(wd)
            return print("♟ Chess bot ON: " + ("it comes back as soon as the game it is finishing ends." if flag == "off"
                                               and svc == "active" else "looking for games again."))
        if flag == "off":
            return print("♟ Chess bot is already OFF" + (", finishing its last game." if svc == "active" else
                                                         f"; {BOT_HOST} is free. /chessboton to resume."))
        r = host_sh(f"touch {OFF_FLAG}")
        if r.returncode:
            return print(f"⚠️ Chess bot OFF failed: {(r.stderr or r.stdout).strip()[-300:]}")
        if svc != "active":
            wd.update(off_reported=True); wd_save(wd)
            return print(f"♟ Chess bot OFF (the service wasn't running); {BOT_HOST} is free. /chessboton to resume.")
        try: game = api(f"/api/users/status?ids={BOT}&withGameIds=true")[0].get("playingId")
        except Exception: game = None
        r = host_sh("systemctl --user kill --kill-whom=main --signal=SIGINT lichess-bot")  # a 2nd SIGINT force-quits:
        if r.returncode:                                                                  # only ever sent once (flag)
            host_sh(f"rm -f {OFF_FLAG}")
            return print(f"⚠️ Chess bot OFF failed, bot left ON: {(r.stderr or r.stdout).strip()[-300:]}")
        if game:
            return print(f"♟ Chess bot switching OFF: no new games; it finishes lichess.org/{game} first. "
                         f"You'll get a message when {BOT_HOST} is free.")
        r = host_sh("for i in $(seq 15); do systemctl --user is-active -q lichess-bot || break; sleep 1; done; "
                    "systemctl --user is-active lichess-bot", timeout=25)
        if r.stdout.strip() == "active":
            return print(f"♟ Chess bot switching OFF: no new games, shutting down. You'll get a message when {BOT_HOST} is free.")
        wd.update(off_reported=True); wd_save(wd)
        print(f"♟ Chess bot OFF; {BOT_HOST} is free. /chessboton to resume.")
    except Exception as e:
        print(f"⚠️ Chess bot {'ON' if on else 'OFF'} failed: {e}")


IDLE_LIMIT, RESTART_GAP = 20 * 60, 30 * 60


def bot_cap_until():
    """When Lichess's bot-vs-bot cap ends (0: not capped), from lichess-bot's own rate-limit error in the journal.
    lila's BotLimit counts every bot-vs-bot game START (aborted ones too) in a 24 h window that opens at the first
    game, so counting exported games misses it (99 vs 100 on 2026-10-10). The error is rich-wrapped over many lines,
    with the source column (matchmaking.py:129) inside the message."""
    out = subprocess.run(on_bot_host(["journalctl", "--user", "-u", "lichess-bot", "--since", "-25h", "-o", "json",
                                      "--output-fields=MESSAGE"]), capture_output=True, text=True).stdout
    entries = [json.loads(l) for l in out.splitlines()]
    starts, text, pos = [], [], 0
    for e in entries:
        m = e.get("MESSAGE")
        m = m if isinstance(m, str) else ""
        starts.append(pos); text.append(m); pos += len(m) + 1
    joined = " ".join(text)
    until = 0
    for hit in re.finditer(r"You\s+played\s+\d+\s+games.{0,800}?bot\.vsBot\.day',\s*'seconds':\s*(\d+)", joined, re.S):
        i = max(j for j, st in enumerate(starts) if st <= hit.start())
        until = max(until, int(entries[i]["__REALTIME_TIMESTAMP"]) / 1e6 + int(hit.group(1)))
    return until


def cmd_watchdog():
    """Independent of lichess-bot's internals: if it stops playing for any reason, restart it and say so."""
    wd = wd_load()
    now = time.time()
    status = api(f"/api/users/status?ids={BOT}")[0]
    try: flag, svc = switch_state()
    except Exception: flag, svc = "on", "unknown"  # host unreachable: the restart below fails and says so
    if flag == "off":  # /chessbotoff: no restarts, no idle or cap alerts; confirm once the server is free
        wd["last_active"] = now  # after /chessboton, idle counts from the switch, not from the last game
        if svc == "active" and not status.get("playing"):
            wd["off_idle"] = wd.get("off_idle", 0) + 1
            if wd["off_idle"] >= 2:  # its game is over but lichess-bot hasn't exited for 2 checks: nothing to lose
                host_sh("systemctl --user stop lichess-bot")
                svc = "stopped"
        else:
            wd["off_idle"] = 0
        if svc != "active" and not wd.get("off_reported"):
            wd["off_reported"] = True
            subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], text=True,
                           input=f"♟ Chess bot is OFF; {BOT_HOST} is free. Send /chessboton to resume.")
        wd_save(wd)
        return print(f"{time.strftime('%F %T')} switched off, service {svc}, playing={bool(status.get('playing'))}")
    wd.update(off_reported=False, off_idle=0)
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
        cap = bot_cap_until()
        if cap > now:  # lichess-bot already waits until then by itself: a restart only re-hits the limit
            if not wd.get("cap_reported"):
                wd["cap_reported"] = True
                subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], text=True,
                               input=f"♟ {BOT} reached Lichess's 100 bot-vs-bot games for the day; bot games resume at "
                                     f"{time.strftime('%H:%M', time.localtime(cap))} (humans can still challenge). No restart needed.")
        elif now - cap > IDLE_LIMIT:  # matchmaking gets IDLE_LIMIT after the cap too (2026-10-12: a restart 2.5 min
            reason = f"idle for {idle / 60:.0f} min"  # after it ended set off 34 min of 429s on the event stream)
    if status.get("playing") or idle < IDLE_LIMIT:
        wd["cap_reported"] = False
    if reason and now - wd.get("last_restart", 0) > RESTART_GAP:
        wd["last_restart"] = now
        ok = subprocess.run(on_bot_host(["systemctl", "--user", "restart", "lichess-bot.service"])).returncode == 0
        msg = f"⚠️ Watchdog: {BOT} was {reason}; lichess-bot {'restarted' if ok else 'restart FAILED'}."
        subprocess.run([str(HERMES), "send", "-t", "telegram", "-q"], input=msg, text=True)
        print(msg)
    wd_save(wd)
    print(f"{time.strftime('%F %T')} online={status.get('online')} playing={bool(status.get('playing'))} "
          f"idle={idle / 60:.1f} min{' -> ' + reason if reason else ''}")


if __name__ == "__main__":
    cmd, args = (sys.argv[1] if len(sys.argv) > 1 else "status"), sys.argv[2:]
    if cmd == "status": cmd_status()
    elif cmd == "report": cmd_report(int(args[0]) if args else 24)
    elif cmd == "last": cmd_last()
    elif cmd == "analyze": cmd_analyze(next((a for a in args if a != "--run"), None), "--run" in args)
    elif cmd == "watchdog": cmd_watchdog()
    elif cmd in ("on", "off"): cmd_switch(cmd == "on")
    else: sys.exit(__doc__)
