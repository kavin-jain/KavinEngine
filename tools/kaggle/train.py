# NNUE training on a Kaggle GPU, so no training runs on the Mac. tools/kaggle/push.sh fills in REF and JOBS,
# then pushes this file as a private script kernel. Outputs (/kaggle/working): <net>.bin, <net>.evals.txt, <net>.log.
import glob, os, shutil, subprocess

REF = "__REF__"
JOBS = __JOBS__  # [(net_id, hidden, superbatches, wdl, data: lichess|sp1|sp2|own|leela|leela2|leela4, init "<net>-<sb>" or "", lr)]


def sh(cmd):
    print("+", cmd, flush=True)
    subprocess.run(cmd, shell=True, check=True, executable="/bin/bash")


sh("nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv")
sh("curl -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal")
os.environ["PATH"] = os.path.expanduser("~/.cargo/bin") + ":" + os.environ["PATH"]
os.environ.setdefault("CUDA_PATH", "/usr/local/cuda")
# bullet links -lcuda; on Kaggle the driver library lives outside CUDA_PATH and may lack the unversioned name.
libcuda = (glob.glob("/usr/local/nvidia/lib64/libcuda.so*") + glob.glob("/usr/lib/x86_64-linux-gnu/libcuda.so*")
           + glob.glob("/usr/local/cuda/lib64/stubs/libcuda.so"))[0]
os.makedirs("/tmp/cudalib", exist_ok=True)
if not os.path.exists("/tmp/cudalib/libcuda.so"): os.symlink(libcuda, "/tmp/cudalib/libcuda.so")
os.environ["RUSTFLAGS"] = "-L /tmp/cudalib"
print("libcuda:", libcuda, flush=True)
sh(f"git clone -q https://github.com/kavin-jain/KavinEngine /tmp/ke && git -C /tmp/ke checkout -q {REF}")
T = "/tmp/ke/train"
sh(f"cd {T} && cargo build -q --release --no-default-features --features cuda")
# Training data comes from a public release: Kaggle refuses GPU sessions with the 7 GB dataset attached
# (probed 2026-10-07: GPU alone and dataset alone both run, together they fail with an empty log).
def fetch_release(tag, names):  # direct download URLs: no GitHub API rate limit on shared Kaggle IPs
    d = f"/tmp/{tag}"
    os.makedirs(d, exist_ok=True)
    urls = " ".join(f"https://github.com/kavin-jain/KavinEngine/releases/download/{tag}/{n}" for n in names)
    sh(f"cd {d} && printf '%s\\n' {urls} | xargs -n1 -P8 curl -sSfLO && du -sh .")
    return d


def lichess():
    return fetch_release("data-lichess-v1", [f"train_{i:02d}.bin" for i in range(32)] + ["val.bin"])


def unzstd(src, dst):
    sh("pip install -q zstandard")
    import zstandard
    with open(src, "rb") as a, open(dst, "wb") as b:
        zstandard.ZstdDecompressor().copy_stream(a, b)
    os.remove(src)


def shard(name, raw_files):  # 32-byte records -> shuffled train_NN.bin + val.bin
    d = f"/tmp/{name}"
    os.makedirs(d, exist_ok=True)
    sh(f"make -C /tmp/ke -s convert && /tmp/ke/build/lichess_convert shard {d} {' '.join(raw_files)} && "
       f"/tmp/ke/build/lichess_convert shuffle {d}/*.bin && rm {' '.join(raw_files)} && du -sh {d}")
    return d


def selfplay(*sets):  # datagen releases, e.g. ("sp1", 6), ("sp2", 16): zstd files merged into one shuffled set
    raw = []
    for tag, jobs in sets:
        d = fetch_release(f"data-{tag}", [f"{tag}-j{j}.bin.zst" for j in range(1, jobs + 1)])
        for f in glob.glob(f"{d}/*.zst"):
            unzstd(f, f[:-4]); raw.append(f[:-4])
    return shard("+".join(t for t, _ in sets), raw)


def leela():  # Leela-derived positions (linrock/bullet-training-data on Hugging Face, ODbL), one ~9 GB file
    f = "test77nov-unfilt-test79-maraprmay-v6-dd.skip-see-ge0.wdl-pdist.iter-1.bullet.bin.zst"
    os.makedirs("/tmp/leela-raw", exist_ok=True)
    sh(f"curl -sSfL -o /tmp/leela-raw/l.zst https://huggingface.co/datasets/linrock/bullet-training-data/resolve/main/S2/{f}")
    unzstd("/tmp/leela-raw/l.zst", "/tmp/leela-raw/l.bin")
    return shard("leela", ["/tmp/leela-raw/l.bin"])


def leela_stream(iters):  # several S2 files, streamed download -> zstd -> shard: no 17 GB raw file per iteration on disk
    d = f"/tmp/leela-s2-{'-'.join(map(str, iters))}"
    os.makedirs(d, exist_ok=True)
    sh("pip install -q zstandard && make -C /tmp/ke -s convert")
    for i in iters:
        if shutil.disk_usage(d).free < 25e9:  # each file adds ~17 GB of shards; train on what fits
            print(f"disk nearly full: stopping before iter-{i}", flush=True); break
        f = f"test77nov-unfilt-test79-maraprmay-v6-dd.skip-see-ge0.wdl-pdist.iter-{i}.bullet.bin.zst"
        sh(f"set -o pipefail; curl -sSfL https://huggingface.co/datasets/linrock/bullet-training-data/resolve/main/S2/{f} | "
           "python3 -c 'import sys, zstandard; zstandard.ZstdDecompressor().copy_stream(sys.stdin.buffer, sys.stdout.buffer)' | "
           f"/tmp/ke/build/lichess_convert shard {d} /dev/stdin")  # shard appends, so the files mix at random
    sh(f"/tmp/ke/build/lichess_convert shuffle {d}/*.bin && du -sh {d} && df -h /tmp")
    return d


def eval_slope(net, data):  # least-squares slope of a reference net's eval (our cp) against the data's labels
    sh("make -C /tmp/ke -s loss NNUE_HIDDEN=256 NNUE_KB=10 NNUE_OB=8")
    out = subprocess.run(["/tmp/ke/build/nnue_loss_256", net, data, "1000000"], capture_output=True, text=True,
                         check=True).stdout
    print(out.strip(), flush=True)
    return float(out.split("eval/label slope ")[1].split()[0])


def selected(base, mode, fraction, net="/tmp/ke/nets/wb-ft-w3.bin"):  # surprise-ranked ("Einstein") or random subset
    if base not in ready: ready[base] = DATA[base]()  # prepare the full set once for both subsets
    src, d = ready[base], f"/tmp/{base}-{mode}{int(fraction * 100)}"
    os.makedirs(d, exist_ok=True)
    sh("make -C /tmp/ke -s loss NNUE_HIDDEN=256 NNUE_KB=10 NNUE_OB=8")
    for f in sorted(glob.glob(f"{src}/train_*.bin")):
        sh(f"/tmp/ke/build/nnue_loss_256 {net} {f} --{mode} {fraction} {d}/{os.path.basename(f)}")
    shutil.copy(f"{src}/val.bin", f"{d}/val.bin")  # validation stays unfiltered
    return d


DATA = {"lichess": lichess, "sp1": lambda: selfplay(("sp1", 6)), "sp2": lambda: selfplay(("sp2", 16)),
        "own": lambda: selfplay(("sp1", 6), ("sp2", 16)), "leela": leela, "leela2": lambda: leela_stream((1, 2)),
        "leela4": lambda: leela_stream((1, 2, 3, 4)),
        "sp2-hard25": lambda: selected("sp2", "select", 0.25), "sp2-rand25": lambda: selected("sp2", "random", 0.25),
        **{t: (lambda t=t: selfplay((t, 12))) for t in ("rl-none", "rl-sf19", "rl-leela", "sp3-25k")}}
# Labels from another engine or search depth: EVAL_SCALE maps them to the main net's cp (the same rule for every arm).
CALIBRATED = {"leela", "leela2", "leela4", "rl-none", "rl-sf19", "rl-leela", "sp3-25k"}
ready = {}
for net, hidden, sbs, wdl, data, init, lr in JOBS:
    if data not in ready: ready[data] = DATA[data]()
    if data in CALIBRATED and ("scale", data) not in ready:  # label units -> the main net's cp, so search margins fit
        ready["scale", data] = 400 / eval_slope("/tmp/ke/nets/wb-ft-w3.bin", f"{ready[data]}/train_00.bin")
        print(f"{data} EVAL_SCALE {ready['scale', data]:.1f}", flush=True)
    log = f"/kaggle/working/{net}.log"
    env = f"WDL={wdl} LR={lr}" + (f" INIT={T}/checkpoints/{init}" if init else "") + \
        (f" SHARDS={net.split('-s')[-1]}" if "-s" in net and net.split('-s')[-1].isdigit() else "")  # e.g. grid-w64-s8
    if data in CALIBRATED: env += f" EVAL_SCALE={ready['scale', data]:.1f}"
    # Full log to a file; one line per superbatch to stdout, visible live via `kaggle kernels logs -f`.
    sh(f"set -o pipefail; cd {T} && {env} target/release/train {hidden} {sbs} {net} {ready[data]} 2>&1 | tee {log} | "
       f"stdbuf -oL tr '\\r' '\\n' | grep --line-buffered -a 'running loss'")
    shutil.copy(f"{T}/checkpoints/{net}-{sbs}/quantised.bin", f"/kaggle/working/{net}.bin")
    shutil.copy(f"{T}/checkpoints/{net}.evals.txt", f"/kaggle/working/{net}.evals.txt")
