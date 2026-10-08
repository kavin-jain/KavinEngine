#!/usr/bin/env python3
"""CCRL-anchored rating: dispatch .github/workflows/gauntlet.yml, merge its jobs' results and fit our rating by
maximum likelihood with the anchors' CCRL Blitz ratings held fixed (logistic Elo model, draws as half points).
Results land in results/gauntlet/<id>/. Usage: tools/gauntlet.py ID REF [--tc 60+0.6] [--rounds 10] [--jobs 8]
       tools/gauntlet.py --selftest"""
import argparse, json, math, pathlib, subprocess, sys, time

# Anchors: fixed opponents with CCRL Blitz ratings (2'+1" on an i7-4770K, computerchess.org.uk/ccrl/404), one JSON
# entry each in tools/anchors.json: name, repo, tag, build, binary, id_contains, rating, source. The workflow skips any
# anchor whose UCI "id name" lacks id_contains (GitHub tag v31.0 of Stash reported "v30.15", so it was dropped).
ANCHOR_FILE = pathlib.Path(__file__).with_name("anchors.json")
ANCHOR_LIST = json.loads(ANCHOR_FILE.read_text())
ANCHORS = {a["name"]: a["rating"] for a in ANCHOR_LIST}


def expected(d):
    return 1.0 / (1.0 + 10.0 ** (-d / 400.0))


def fit(results):
    """results: {anchor: {w, d, l}} -> (rating, 95% half-width). Solves sum(score - n * E(R - r)) = 0 by bisection;
    the error uses the observed per-game score variance against each anchor (the delta method)."""
    rows = [(ANCHORS[a], r["w"] + 0.5 * r["d"], r["w"] + r["d"] + r["l"], r) for a, r in results.items() if a in ANCHORS]
    f = lambda R: sum(s - n * expected(R - ra) for ra, s, n, _ in rows)
    lo, hi = 0.0, 5000.0
    for _ in range(100):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if f(mid) > 0 else (lo, mid)
    R = (lo + hi) / 2
    var = sum(n * ((r["w"] + 0.25 * r["d"]) / n - (s / n) ** 2) for ra, s, n, r in rows if n)
    slope = sum(n * expected(R - ra) * (1 - expected(R - ra)) * math.log(10) / 400 for ra, s, n, _ in rows)
    return R, 1.96 * math.sqrt(var) / slope if slope else float("inf")


def gh_json(*args):  # None on a transient API error
    r = subprocess.run(("gh",) + args, capture_output=True, text=True)
    return json.loads(r.stdout or "null") if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id", nargs="?"); ap.add_argument("ref", nargs="?")
    ap.add_argument("--tc", default="60+0.6"); ap.add_argument("--rounds", type=int, default=10)
    ap.add_argument("--jobs", type=int, default=8); ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--anchors", default="", help="comma-separated anchor names (default: all in tools/anchors.json)")
    a = ap.parse_args()
    if a.selftest:  # an engine at 3000 with exactly expected scores must fit back to 3000
        res = {t: {"w": 0, "d": 0, "l": 0} for t in ANCHORS}
        for t, ra in ANCHORS.items():
            e = expected(3000 - ra); res[t] = {"w": round(1000 * e * 0.6), "d": round(1000 * e * 0.8), "l": 0}
            res[t]["l"] = 1000 - res[t]["w"] - res[t]["d"]
        R, ci = fit(res)
        assert abs(R - 3000) < 3, R
        return print(f"gauntlet fit OK: {R:.1f} ± {ci:.1f}")
    ref = subprocess.run(["git", "rev-parse", a.ref], check=True, capture_output=True, text=True).stdout.strip()
    out = pathlib.Path(f"results/gauntlet/{a.id}"); out.mkdir(parents=True, exist_ok=True)
    title = f"gauntlet {a.id}"
    find = lambda: next((r["databaseId"] for r in gh_json("run", "list", "--workflow", "gauntlet.yml", "--limit", "30",
                                                          "--json", "databaseId,displayTitle") or [] if r["displayTitle"] == title), None)
    rid = find()
    if rid is None:
        names = a.anchors.split(",") if a.anchors else list(ANCHORS)
        spec = json.dumps([x for x in ANCHOR_LIST if x["name"] in names], separators=(",", ":"))
        subprocess.run(["gh", "workflow", "run", "gauntlet.yml", "-f", f"id={a.id}", "-f", f"ref={ref}", "-f", f"tc={a.tc}",
                        "-f", f"anchors={spec}",
                        "-f", f"rounds={a.rounds}", "-f", "jobs=" + json.dumps(list(range(1, a.jobs + 1)))], check=True)
        for _ in range(30):
            time.sleep(10)
            if (rid := find()): break
    print(f"run {rid}", flush=True)
    while (gh_json("run", "view", str(rid), "--json", "status") or {}).get("status") != "completed":
        time.sleep(120)
    for _ in range(5):
        subprocess.run(["gh", "run", "download", str(rid), "-D", str(out)], capture_output=True)
        files = sorted(out.glob("*/result.json"))
        if files: break
        time.sleep(60)
    merged = {}
    for f in files:
        for t, r in json.loads(f.read_text()).items():
            m = merged.setdefault(t, {"w": 0, "d": 0, "l": 0, "time_losses": 0})
            for k in m: m[k] += r[k]
    R, ci = fit(merged)
    lines = [f"gauntlet {a.id} ({ref[:7]}, tc {a.tc}, book 8moves_v3, {len(files)}/{a.jobs} jobs)"]
    for t, r in sorted(merged.items()):
        n = r["w"] + r["d"] + r["l"]; s = (r["w"] + 0.5 * r["d"]) / n
        perf = ANCHORS[t] + 400 * math.log10(s / (1 - s)) if 0 < s < 1 else float("nan")
        lines.append(f"  vs {t} ({ANCHORS[t]}): +{r['w']} ={r['d']} -{r['l']} score {100 * s:.1f}% "
                     f"perf {perf:.0f} time losses {r['time_losses']}")
    lines.append(f"RESULT {a.id}: CCRL Blitz-anchored rating {R:.0f} ± {ci:.0f} over {sum(sum(v[k] for k in 'wdl') for v in merged.values())} games")
    (out / "summary.log").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
