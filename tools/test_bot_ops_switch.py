"""Checks bot_ops.py's on/off switch and the watchdog's handling of it, with a fake bot host and fake Lichess
(no ssh, no network, no Telegram). Needs ~/.lichess-token to exist (bot_ops reads it at import):
  python3 tools/test_bot_ops_switch.py"""
import io, json, pathlib, sys, tempfile, contextlib, types
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import bot_ops as b

host = {}       # fake BOT_HOST: flag file + lichess-bot.service
sent, sh = [], []  # Telegram messages, shell snippets run on the host
lichess = {"playing": False, "game": None}


def fake_sh(script, timeout=20):
    sh.append(script)
    if host["down"]: return types.SimpleNamespace(returncode=255, stdout="", stderr="ssh: connect timed out")
    out = ""
    if script.startswith("test -e"): out = f"{'off' if host['flag'] else 'on'}\n{host['svc']}\n"
    elif script.startswith("touch"): host["flag"] = True
    elif script.startswith("rm -f"):
        host["flag"] = False
        if "start" in script and host["svc"] != "active": host["svc"] = "active"
    elif "SIGINT" in script:
        host["sigint"] += 1
        if not lichess["playing"]: host["svc"] = "activating"  # exits at once; the restart is skipped by the flag
    elif script.startswith("for i"): out = host["svc"] + "\n"
    elif "stop lichess-bot" in script: host["svc"] = "inactive"
    return types.SimpleNamespace(returncode=0, stdout=out, stderr="")


def fake_api(path, ndjson=False):
    if "/api/users/status" in path:
        return [{"online": host["svc"] == "active", "playing": lichess["playing"], "playingId": lichess["game"]}]
    return []


def run(fn, *a):
    out = io.StringIO()
    with contextlib.redirect_stdout(out): fn(*a)
    return out.getvalue().strip()


def reset(flag=False, svc="active", playing=False, down=False):
    host.update(flag=flag, svc=svc, sigint=0, down=down)
    lichess.update(playing=playing, game="AbCd1234" if playing else None)
    sent.clear(); sh.clear()
    b.WATCHDOG.unlink(missing_ok=True)


b.host_sh, b.api = fake_sh, fake_api
b.WATCHDOG = pathlib.Path(tempfile.mkdtemp()) / "watchdog.json"
b.bot_cap_until = lambda: 0
b.subprocess = types.SimpleNamespace(run=lambda cmd, **k: sent.append(k.get("input")) or types.SimpleNamespace(returncode=0))

reset()                                              # off, no game: stops at once and confirms
r = run(b.cmd_switch, False)
assert host["flag"] and host["sigint"] == 1 and "OFF;" in r and b.wd_load()["off_reported"], r
assert run(b.cmd_switch, False).startswith("♟ Chess bot is already OFF") and host["sigint"] == 1  # no 2nd SIGINT

reset(playing=True)                                  # off mid-game: game named, played out, no stop command
r = run(b.cmd_switch, False)
assert "AbCd1234" in r and host["svc"] == "active" and not any("stop" in s for s in sh), r
run(b.cmd_watchdog); assert not sent and host["svc"] == "active"            # still playing: wait, say nothing
lichess.update(playing=False, game=None)                                    # game over, process hangs
run(b.cmd_watchdog); assert host["svc"] == "active" and not sent           # 1st idle check: wait
run(b.cmd_watchdog); assert host["svc"] == "inactive" and len(sent) == 1 and "OFF" in sent[0]  # 2nd: stopped
run(b.cmd_watchdog); assert len(sent) == 1                                  # reported once
assert not any("restart" in s for s in sh)                                  # the watchdog never restarts it

reset(flag=True, svc="active", playing=True)          # on while it finishes its last game
r = run(b.cmd_switch, True)
assert not host["flag"] and "as soon as" in r and b.wd_load()["last_active"] > 0, r
reset(flag=True, svc="inactive")                      # on from off
r = run(b.cmd_switch, True)
assert not host["flag"] and host["svc"] == "active" and "looking for games" in r, r
assert run(b.cmd_switch, True) == "♟ Chess bot is already ON."

reset(down=True)                                      # host unreachable: honest failure, nothing claimed
assert run(b.cmd_switch, False).startswith("⚠️ Chess bot OFF failed: can't reach")
assert run(b.cmd_switch, True).startswith("⚠️ Chess bot ON failed: can't reach")
print("ok")
