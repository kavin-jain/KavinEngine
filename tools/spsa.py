#!/usr/bin/env python3
"""SPSA tuning of the search constants (Spall's simultaneous perturbation; the OpenBench formulation) on GitHub Actions.

GitHub runners cannot share one live parameter set, so each round runs M independent chains (spsa.yml jobs) from the
same values for a fixed time. Each chain repeatedly plays theta + c*D against theta - c*D (D a random +-1 vector,
P game pairs, fastchess) and moves theta along D by the result. The round's new theta is the start plus the SUM of the
chains' moves: the same steps a single chain would take, computed on values up to one round old. Global step k (one
per game pair) sets the gain schedules: c_k = c / (k+1)^0.101, a_k = a / (A+k+1)^0.602, A = 0.1 N.

  tools/spsa.py run ID REF [--chains 32] [--minutes 75] [--rounds 6] [--pairs 8] [--n-total 60000] [--tc 8+0.08]
      on the Mac: dispatch rounds, merge, keep state in results/spsa/ID/ (resumable). REF is pushed and builds with
      make TUNE=1; the parameters, defaults and ranges come from that engine's UCI options.
  tools/spsa.py chain ...   on a runner (spsa.yml): one chain, writes result.json after every step.
"""
import argparse, json, math, pathlib, random, re, subprocess, sys, time

ALPHA, GAMMA, R_END = 0.602, 0.101, 0.002


def uci_spec(engine):  # {name: [default, lo, hi]} for every tunable (all spin options except the engine settings)
    out = subprocess.run([engine], input="uci\nquit\n", capture_output=True, text=True, timeout=60).stdout
    skip = {"Hash", "Threads", "Move Overhead", "Clock Reserve"}
    return {n: [int(d), int(lo), int(hi)] for n, d, lo, hi in
            re.findall(r"option name (\S+) type spin default (-?\d+) min (-?\d+) max (-?\d+)", out) if n not in skip}


def c_end(lo, hi):  # final perturbation: 1/20 of the range, at least half a unit (values are rounded at random)
    return max(0.5, (hi - lo) / 20)


def chain(a):
    spec, st = uci_spec(a.engine), json.loads(a.state)
    theta0 = {n: float(st.get("theta", {}).get(n, d)) for n, (d, lo, hi) in spec.items()}
    theta = dict(theta0)
    N, M, k0 = st["n_total"], st["chains"], st["k0"]
    A = 0.1 * N
    rng = random.Random(a.seed)
    res = {"chain": a.chain, "theta0": theta0, "theta": theta, "spec": spec, "pairs": 0, "wins": 0, "losses": 0,
           "draws": 0, "steps": 0}
    deadline = time.time() + 60 * a.minutes
    while time.time() < deadline:
        k = k0 + res["pairs"] * M  # global step: M chains advance it together
        plus, minus, step = {}, {}, {}
        for n, (d, lo, hi) in spec.items():
            ce = c_end(lo, hi)
            ck = ce * N ** GAMMA / (k + 1) ** GAMMA
            ak = R_END * ce * ce * (A + N) ** ALPHA / (A + k + 1) ** ALPHA
            delta = rng.choice((-1, 1))
            u = rng.random()  # one stochastic rounding for both sides keeps them exactly 2*c_k*D apart on average
            plus[n] = min(hi, max(lo, math.floor(theta[n] + ck * delta + u)))
            minus[n] = min(hi, max(lo, math.floor(theta[n] - ck * delta + u)))
            step[n] = ak * delta / ck
        opts = lambda v: [f"option.{n}={x}" for n, x in v.items()]
        cmd = [a.fastchess, "-engine", f"cmd={a.engine}", "name=plus", *opts(plus),
               "-engine", f"cmd={a.engine}", "name=minus", *opts(minus),
               "-each", f"tc={a.tc}", "option.Hash=16", "-openings", f"file={a.book}", "format=epd", "order=random",
               "-srand", str(rng.randrange(1 << 30)), "-rounds", str(a.pairs), "-repeat",
               "-concurrency", str(a.concurrency), "-recover"]
        log = subprocess.run(cmd, capture_output=True, text=True).stdout
        found = re.findall(r"Wins: (\d+), Losses: (\d+), Draws: (\d+)", log)
        if not found:
            sys.exit("fastchess gave no result:\n" + log[-2000:])
        w, l, d = map(int, found[-1])
        for n, (_, lo, hi) in spec.items():
            theta[n] = min(hi, max(lo, theta[n] + step[n] * (w - l)))
        res["pairs"] += a.pairs; res["steps"] += 1
        res["wins"] += w; res["losses"] += l; res["draws"] += d
        pathlib.Path(a.out).write_text(json.dumps(res, indent=1))
        print(f"step {res['steps']} k {k} W{w} L{l} D{d} " + " ".join(f"{n}={v:.1f}" for n, v in theta.items()), flush=True)


def gh(*args):
    r = subprocess.run(("gh",) + args, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def find_run(title, tries=30):
    for i in range(tries):
        for r in json.loads(gh("run", "list", "--workflow", "spsa.yml", "--limit", "30", "--json",
                               "databaseId,displayTitle") or "[]"):
            if r["displayTitle"] == title:
                return r["databaseId"]
        if i + 1 < tries: time.sleep(10)
    return None


def run(a):
    ref = subprocess.run(["git", "rev-parse", a.ref], check=True, capture_output=True, text=True).stdout.strip()
    out = pathlib.Path(f"results/spsa/{a.id}"); out.mkdir(parents=True, exist_ok=True)
    state_f, log_f = out / "state.json", out / "summary.log"
    st = json.loads(state_f.read_text()) if state_f.exists() else {"ref": ref, "theta": {}, "k": 0, "round": 0}
    if st["ref"] != ref: sys.exit(f"{state_f} belongs to {st['ref']}; pick a new id")

    def log(msg):
        print(msg, flush=True)
        with log_f.open("a") as f: f.write(msg + "\n")

    while st["round"] < a.rounds:
        r = st["round"] + 1
        title = f"spsa {a.id} round {r}"
        rid = find_run(title, tries=1)
        if rid is None:
            state = json.dumps({"theta": st["theta"], "k0": st["k"], "chains": a.chains, "n_total": a.n_total})
            gh("workflow", "run", "spsa.yml", "-f", f"id={a.id}", "-f", f"round={r}", "-f", f"ref={ref}",
               "-f", f"state={state}", "-f", f"minutes={a.minutes}", "-f", f"pairs={a.pairs}", "-f", f"tc={a.tc}",
               "-f", "jobs=" + json.dumps(list(range(1, a.chains + 1))))
            rid = find_run(title) or sys.exit(f"run '{title}' did not appear")
            log(f"round {r}: run {rid} dispatched ({a.chains} chains x {a.minutes} min, from step {st['k']})")
        while json.loads(gh("run", "view", str(rid), "--json", "status") or "{}").get("status") != "completed":
            time.sleep(60)
        rdir = out / f"r{r}"
        for _ in range(5):
            subprocess.run(["gh", "run", "download", str(rid), "-D", str(rdir)], capture_output=True)
            results = [json.loads(f.read_text()) for f in sorted(rdir.glob("*/result.json"))]
            if results: break
            time.sleep(60)
        results = [x for x in results if x["pairs"]]
        if not results: sys.exit(f"round {r}: no chain finished a step; see gh run view {rid} --log-failed")
        spec, theta0 = results[0]["spec"], results[0]["theta0"]
        theta = {n: min(hi, max(lo, theta0[n] + sum(x["theta"][n] - x["theta0"][n] for x in results)))
                 for n, (_, lo, hi) in spec.items()}
        st.update(theta=theta, k=st["k"] + sum(x["pairs"] for x in results), round=r)
        state_f.write_text(json.dumps(st, indent=1))
        w, l, d = (sum(x[f] for x in results) for f in ("wins", "losses", "draws"))
        log(f"round {r}: {len(results)}/{a.chains} chains, {sum(x['pairs'] for x in results)} pairs (W{w} L{l} D{d}), "
            f"step {st['k']}/{a.n_total}\n  " + " ".join(f"{n}={theta[n]:.1f} ({spec[n][0]})" for n in theta))
    log(f"RESULT {a.id}: " + " ".join(f"{n}={round(v)}" for n, v in st["theta"].items()))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("id"); r.add_argument("ref")
    r.add_argument("--chains", type=int, default=32); r.add_argument("--minutes", type=int, default=75)
    r.add_argument("--rounds", type=int, default=6); r.add_argument("--pairs", type=int, default=8)
    r.add_argument("--n-total", type=int, default=60000); r.add_argument("--tc", default="8+0.08")
    c = sub.add_parser("chain")
    for f in ("engine", "fastchess", "book", "state", "tc", "out"): c.add_argument("--" + f, required=True)
    for f in ("chain", "seed", "minutes", "pairs"): c.add_argument("--" + f, type=int, required=True)
    c.add_argument("--concurrency", type=int, default=4)
    a = ap.parse_args()
    run(a) if a.cmd == "run" else chain(a)


if __name__ == "__main__":
    main()
