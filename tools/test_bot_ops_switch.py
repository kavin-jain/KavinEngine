"""Checks bot_ops.py's on/off switch, its Telegram confirmations (cmd_notify) and the watchdog's handling of it, with a
fake bot host and fake Lichess (no ssh, no network, no Telegram). Needs ~/.lichess-token to exist (read at import):
  python3 tools/test_bot_ops_switch.py"""
import io, pathlib, sys, tempfile, contextlib, types
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import bot_ops as b

host = {}          # fake BOT_HOST: flag file + lichess-bot.service (+ what happens on later polls)
told, sh, spawned = [], [], []
lichess = {}


def fake_sh(script, timeout=20):
    sh.append(script)
    if host["down"]: return types.SimpleNamespace(returncode=255, stdout="", stderr="ssh: connect timed out")
    out = ""
    if script.startswith("test -e"):
        host["polls"] += 1
        if host["polls"] == host.get("game_ends_at"): host["svc"] = "activating"; lichess["playing"] = False
        if host["polls"] == host.get("restart_at"): host["svc"], host["pid"] = "active", "222"; lichess["online"] = True
        out = f"{'off' if host['flag'] else 'on'}\n{host['svc']}\n{host['pid'] if host['svc'] == 'active' else 0}\n"
    elif script.startswith("touch"): host["flag"] = True
    elif script.startswith("rm -f"):
        host["flag"] = False
        if "start" in script and host["svc"] != "active":
            host["svc"], host["pid"] = "active", "111"
            lichess["online"] = host["comes_online"]
    elif "SIGINT" in script:
        host["sigint"] += 1
        if not lichess["playing"]: host["svc"] = "activating"  # exits at once; the restart is skipped by the flag
    elif script.startswith("for i"): out = host["svc"] + "\n"
    elif "stop lichess-bot" in script: host["svc"] = "inactive"
    elif script.startswith("journalctl"): out = "last log line\n"
    return types.SimpleNamespace(returncode=0, stdout=out, stderr="")


def fake_api(path, ndjson=False):
    if "/api/users/status" in path:
        return [{"online": lichess["online"], "playing": lichess["playing"], "playingId": lichess["game"]}]
    if path.startswith("/game/export/"):
        return {"id": "AbCd1234", "status": "mate", "winner": "white",
                "players": {"white": {"user": {"name": b.BOT}}, "black": {"user": {"name": "SomeBot"}, "rating": 2900}}}
    return []


def run(fn, *a):
    out = io.StringIO()
    with contextlib.redirect_stdout(out): fn(*a)
    return out.getvalue().strip()


def reset(flag=False, svc="active", playing=False, down=False, comes_online=True, **later):
    host.clear(); host.update(flag=flag, svc=svc, pid="100", sigint=0, down=down, polls=0, comes_online=comes_online, **later)
    lichess.update(playing=playing, game="AbCd1234" if playing else None, online=svc == "active")
    told.clear(); sh.clear(); spawned.clear()
    b.WATCHDOG.unlink(missing_ok=True)


b.host_sh, b.api, b.tell = fake_sh, fake_api, told.append
b.spawn_notify = lambda kind, arg="": spawned.append((kind, arg)) or ""
b.time.sleep = lambda s: None
b.WATCHDOG = pathlib.Path(tempfile.mkdtemp()) / "watchdog.json"
b.bot_cap_until = lambda: 0

reset()                                                    # off, no game: stops at once, the reply confirms
r = run(b.cmd_switch, False)
assert host["flag"] and host["sigint"] == 1 and r.startswith("✅ Chess bot OFF;") and not spawned, r
assert b.wd_load()["off_reported"]
assert run(b.cmd_switch, False).startswith("♟ Chess bot is already OFF") and host["sigint"] == 1  # no 2nd SIGINT
assert run(b.cmd_switch, False).endswith("/chessbot_on to turn it back on.")

reset(playing=True, game_ends_at=4)                        # off mid-game: game named, played out, then confirmed
r = run(b.cmd_switch, False)
assert "AbCd1234" in r and host["svc"] == "active" and spawned == [("off", "AbCd1234")], r
assert run(b.cmd_switch, False).startswith("♟ Chess bot is already OFF: finishing")  # still playing
run(b.cmd_notify, "off", "AbCd1234")
assert len(told) == 1 and "won vs SomeBot (2900)" in told[0] and "✅ Chess bot is OFF" in told[0], told
run(b.cmd_watchdog); assert len(told) == 1                 # the watchdog doesn't repeat it

reset(playing=True)                                        # off mid-game, then the process hangs after the game
run(b.cmd_switch, False)
run(b.cmd_watchdog); assert host["svc"] == "active" and not told       # still playing: wait, say nothing
lichess.update(playing=False, game=None)
run(b.cmd_watchdog); assert host["svc"] == "active"                   # 1st idle check: wait
run(b.cmd_watchdog); assert host["svc"] == "inactive" and not told   # 2nd: stopped; notify's job to report
w = b.wd_load(); w["off_at"] -= 16 * 60; b.wd_save(w)
run(b.cmd_watchdog); assert len(told) == 1 and "OFF" in told[0]      # backstop if notify died (15 min)
run(b.cmd_watchdog); assert len(told) == 1
assert not any("restart" in s for s in sh)                            # the watchdog never restarts it while off

reset(playing=True)                                        # off mid-game, then on before the game ends
run(b.cmd_switch, False)
r = run(b.cmd_switch, True)
assert not host["flag"] and "still finishing" in r and spawned[-1] == ("on", "100"), r
run(b.cmd_notify, "off", "AbCd1234"); assert not told     # off job gives up quietly: switched back
host["restart_at"] = host["polls"] + 3                     # game ends, systemd restarts it (new PID 222)
run(b.cmd_notify, "on", "100")
assert told == ["✅ Chess bot is back ON: online on Lichess from oracle, looking for games."], told

reset(flag=True, svc="inactive")                           # on from off: online within the command
r = run(b.cmd_switch, True)
assert not host["flag"] and r.startswith("✅ Chess bot ON: online on Lichess") and not spawned, r
assert b.wd_load()["last_active"] > 0 and not b.wd_load()["off_reported"]
assert run(b.cmd_switch, True).startswith("♟ Chess bot is already ON")
reset(flag=True, svc="inactive", comes_online=False)       # on, Lichess slow to show it online: notify confirms
r = run(b.cmd_switch, True)
assert "I'll message you when it is online" in r and spawned == [("on", "")], r
lichess["online"] = True
run(b.cmd_notify, "on", "-"); assert told and told[0].startswith("✅ Chess bot is back ON"), told
reset(flag=True, svc="inactive", comes_online=False)       # on that never comes online: warning with the log
run(b.cmd_switch, True); real_time = b.time.time
b.time.time = lambda: real_time() + 400 * (len(sh) > 3)    # past the 5 min deadline after the first poll
run(b.cmd_notify, "on", "-"); b.time.time = real_time
assert told and told[0].startswith("⚠️ Chess bot still not ON") and "last log line" in told[0], told

reset(down=True)                                           # host unreachable: honest failure, nothing claimed
assert run(b.cmd_switch, False).startswith("⚠️ Chess bot OFF failed: can't reach")
assert run(b.cmd_switch, True).startswith("⚠️ Chess bot ON failed: can't reach")
print("ok")
