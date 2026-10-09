"""Checks tools/lichess-bot-kavinengine.patch: reconcile_games, start_game_thread's duplicate guard and
lower_engine_priority, with fakes (no network, no engine; Linux, for setpriority). Run from a patched lichess-bot checkout (commit df7e730):
  git apply ../KavinEngine/tools/lichess-bot-kavinengine.patch
  PYTHONPATH=. venv/bin/python ../KavinEngine/tools/test_lichess_bot_patch.py
Also run lichess-bot's own suite after patching: venv/bin/python -m pytest test_bot (53 passed, 2026-10-09)."""
import os, subprocess, sys
sys.argv = ["x"]
from lib import lichess_bot as lb

started = []
lb.log_proc_count = lambda *a: None


class FakePool:
    def apply_async(self, *a, **k):
        started.append(a or k)


class FakeLi:
    def __init__(self, ongoing):
        self.ongoing = ongoing

    def get_ongoing_games(self):
        return self.ongoing


def game(gid, speed="blitz"):
    return {"gameId": gid, "speed": speed, "opponent": {"username": "opp"}}


args = {"control_queue": None}

# 1. a live game with no thread is resumed; a game whose thread runs is left alone
lb.running_games.clear(); lb.running_games.add("RUN1"); started.clear()
active = {"RUN1": "a"}
lb.reconcile_games(FakeLi([game("RUN1"), game("LOST1")]), active, set(), FakePool(), dict(args), [])
assert "LOST1" in lb.running_games and "LOST1" in active and len(started) == 1, (active, started)

# 2. a stale slot (accepted challenge that never started) survives one run and is freed on the second
lb.running_games.clear(); started.clear()
active = {"STALE": "b"}; suspects = set()
lb.reconcile_games(FakeLi([]), active, suspects, FakePool(), dict(args), [])
assert "STALE" in active and suspects == {"STALE"}
lb.reconcile_games(FakeLi([]), active, suspects, FakePool(), dict(args), [])
assert "STALE" not in active and suspects == set()

# 3. a queued challenge whose game starts before the second run keeps its slot
active = {"Q": "c"}; suspects = set()
lb.reconcile_games(FakeLi([]), active, suspects, FakePool(), dict(args), [])
lb.running_games.add("Q")
lb.reconcile_games(FakeLi([game("Q")]), active, suspects, FakePool(), dict(args), [])
assert "Q" in active and suspects == set()

# 4. an API error changes nothing
active = {"X": "d"}; suspects = {"X"}
lb.reconcile_games(FakeLi(None), active, suspects, FakePool(), dict(args), [])
assert active == {"X": "d"} and suspects == {"X"}

# 5. correspondence games are left alone
lb.running_games.clear(); started.clear(); active = {}
lb.reconcile_games(FakeLi([game("C1", "correspondence")]), active, set(), FakePool(), dict(args), [])
assert not started and not active

# 6. a repeated gameStart for a running game starts no second thread; an accepted (queued) challenge does start
lb.running_games.clear(); started.clear(); active = {"ACC": "e"}
lb.start_game_thread(active, "ACC", "e", dict(args), FakePool())
lb.start_game_thread(active, "ACC", "e", dict(args), FakePool())
assert len(started) == 1, started

# 7. engines of non-tournament games run at nice 10, tournament engines stay at 0; get_pid() returns a str (the
# 2026-10-09 int/str mix-up made every casual game fail for an hour), "?" for homemade engines
class FakeEngine:
    def __init__(self, pid):
        self.pid = pid

    def get_pid(self):
        return self.pid


sleeper = subprocess.Popen(["sleep", "30"], start_new_session=True)
lb.lower_engine_priority(FakeEngine(str(sleeper.pid)), {"tournamentId": "EQW8EnjT"}, "T")
assert os.getpriority(os.PRIO_PROCESS, sleeper.pid) == 0
lb.lower_engine_priority(FakeEngine(str(sleeper.pid)), {}, "C")
assert os.getpriority(os.PRIO_PROCESS, sleeper.pid) == 10
lb.lower_engine_priority(FakeEngine("?"), {}, "H")
sleeper.kill()
print("all patch checks passed")
