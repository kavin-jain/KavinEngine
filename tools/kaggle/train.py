# NNUE training on a Kaggle GPU, so no training runs on the Mac. tools/kaggle/push.sh fills in REF and JOBS,
# then pushes this file as a private script kernel. Outputs (/kaggle/working): <net>.bin, <net>.evals.txt, <net>.log.
import glob, os, shutil, subprocess

REF = "__REF__"
JOBS = __JOBS__  # [(net_id, hidden, superbatches, wdl, data_dir or "lichess")]


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
# The mount path of an attached dataset varies across Kaggle images: locate it by its val.bin.
LICHESS = os.path.dirname(glob.glob("/kaggle/input/**/val.bin", recursive=True)[0])
print("lichess data:", LICHESS, flush=True)
for net, hidden, sbs, wdl, data in JOBS:
    data = LICHESS if data == "lichess" else data
    log = f"/kaggle/working/{net}.log"
    sh(f"cd {T} && WDL={wdl} target/release/train {hidden} {sbs} {net} {data} > {log} 2>&1; s=$?; "
       f"tr '\\r' '\\n' < {log} | grep -a -E 'superbatch|loss' | grep -av Estimated | tail -n 5; exit $s")
    shutil.copy(f"{T}/checkpoints/{net}-{sbs}/quantised.bin", f"/kaggle/working/{net}.bin")
    shutil.copy(f"{T}/checkpoints/{net}.evals.txt", f"/kaggle/working/{net}.evals.txt")
