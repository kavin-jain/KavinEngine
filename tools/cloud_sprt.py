#!/usr/bin/env python3
"""Cloud SPRT on GitHub Actions: dispatch sprt-batch.yml batches until the merged games decide H0/H1.
Each batch is N parallel jobs of a fixed size (well under GitHub's 6-hour job limit). The running totals live
in results/sprt/<id>-cloud/state.json, so an interrupted run resumes where it stopped.
Usage: tools/cloud_sprt.py ID DEV_REF BASE_REF [--tc 8+0.08] [--elo0 0] [--elo1 5] [--jobs 10] [--rounds 100]
                           [--max-games 20000]   (both refs must be pushed to GitHub)"""
import argparse, json, pathlib, subprocess, sys, time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from sprt_math import bounds, elo, llr


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip()


def gh_json(*args):  # None on a transient API error, so polling survives network hiccups
    r = subprocess.run(("gh",) + args, capture_output=True, text=True)
    return json.loads(r.stdout) if r.returncode == 0 else None


def find_run(title, tries=30):
    for i in range(tries):
        for r in gh_json("run", "list", "--workflow", "sprt-batch.yml", "--limit", "50",
                         "--json", "databaseId,displayTitle") or []:
            if r["displayTitle"] == title:
                return r["databaseId"]
        if i + 1 < tries: time.sleep(10)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id"); ap.add_argument("dev"); ap.add_argument("base")
    ap.add_argument("--tc", default="8+0.08")
    ap.add_argument("--elo0", type=float, default=0.0); ap.add_argument("--elo1", type=float, default=5.0)
    ap.add_argument("--jobs", type=int, default=10); ap.add_argument("--rounds", type=int, default=100)
    ap.add_argument("--concurrency", type=int, default=3); ap.add_argument("--max-games", type=int, default=20000)
    a = ap.parse_args()
    dev, base = run("git", "rev-parse", a.dev), run("git", "rev-parse", a.base)
    out = pathlib.Path(f"results/sprt/{a.id}-cloud"); out.mkdir(parents=True, exist_ok=True)
    state_f, log_f = out / "state.json", out / "summary.log"
    st = json.loads(state_f.read_text()) if state_f.exists() else {
        "dev": dev, "base": base, "tc": a.tc, "elo": [a.elo0, a.elo1], "batches": 0, "penta": [0] * 5,
        "wins": 0, "losses": 0, "draws": 0, "games": 0, "time_losses": {"new": 0, "base": 0}, "failed_jobs": 0}
    if (st["dev"], st["base"], st["tc"]) != (dev, base, a.tc):
        sys.exit(f"{state_f} belongs to a different test; pick a new id")
    lower, upper = bounds()

    def log(msg):
        print(msg, flush=True)
        with log_f.open("a") as f: f.write(msg + "\n")

    while True:
        b = st["batches"] + 1
        title = f"sprt {a.id} batch {b}"
        rid = find_run(title, tries=1)  # resuming: the batch may already be running
        if rid is None:
            run("gh", "workflow", "run", "sprt-batch.yml", "-f", f"id={a.id}", "-f", f"batch={b}", "-f", f"dev={dev}",
                "-f", f"base={base}", "-f", f"tc={a.tc}", "-f", f"rounds={a.rounds}", "-f", f"concurrency={a.concurrency}",
                "-f", "jobs=" + json.dumps(list(range(1, a.jobs + 1))))
            rid = find_run(title) or sys.exit(f"run '{title}' did not appear")
            log(f"batch {b}: run {rid} dispatched ({a.jobs} jobs x {2 * a.rounds} games)")
        else:
            log(f"batch {b}: resuming run {rid}")
        while (gh_json("run", "view", str(rid), "--json", "status") or {}).get("status") != "completed":
            time.sleep(60)
        bdir = out / f"b{b}"
        subprocess.run(["gh", "run", "download", str(rid), "-D", str(bdir)], check=False, capture_output=True)
        results = sorted(bdir.glob("*/result.json"))
        st["failed_jobs"] += a.jobs - len(results)
        if not results:
            sys.exit(f"batch {b}: no job produced results; see gh run view {rid} --log-failed")
        for rf in results:
            r = json.loads(rf.read_text())
            st["penta"] = [x + y for x, y in zip(st["penta"], r["penta"])]
            for k in ("wins", "losses", "draws", "games"): st[k] += r[k]
            for k in ("new", "base"): st["time_losses"][k] += r["time_losses"][k]
        if b == 1: log("bench (job 1): " + json.loads(results[0].read_text())["bench"].replace("\n", " | "))
        st["batches"] = b
        state_f.write_text(json.dumps(st, indent=1))
        L = llr(st["penta"], a.elo0, a.elo1)
        e, ci, ne = elo(st["penta"])
        tl = st["time_losses"]
        log(f"batch {b}: games {st['games']} W{st['wins']} D{st['draws']} L{st['losses']} Ptnml {st['penta']} "
            f"Elo {e:+.1f} ± {ci:.1f} nElo {ne:+.1f} LLR {L:.2f} ({lower:.2f}, {upper:.2f}) [{a.elo0}, {a.elo1}] "
            f"time losses new {tl['new']} base {tl['base']} failed jobs {st['failed_jobs']}")
        if L >= upper or L <= lower or st["games"] >= a.max_games:
            verdict = "H1 accepted" if L >= upper else "H0 accepted" if L <= lower else "inconclusive (game cap)"
            log(f"RESULT {a.id}: {verdict} after {st['games']} games, Elo {e:+.1f} ± {ci:.1f}, LLR {L:.2f}")
            return


if __name__ == "__main__":
    main()
