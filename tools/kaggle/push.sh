#!/usr/bin/env bash
# Push a training run to Kaggle (private GPU script kernel kavinjain/kavinengine-train).
# Usage: tools/kaggle/push.sh REF "NET HIDDEN SUPERBATCHES WDL" [...]   (REF must be pushed to GitHub)
#   e.g. tools/kaggle/push.sh main "m3-kb10-256 256 40 0.0"
# Status:  .deps/kaggle-venv/bin/kaggle kernels status kavinjain/kavinengine-train
# Results: .deps/kaggle-venv/bin/kaggle kernels output kavinjain/kavinengine-train -p train/checkpoints/kaggle
set -euo pipefail
cd "$(dirname "$0")/../.."
ref=$(git rev-parse "$1"); shift
jobs=""
for j in "$@"; do read -r net hidden sbs wdl <<< "$j"; jobs+="(\"$net\", $hidden, $sbs, $wdl, \"lichess\"), "; done
out=build/kaggle; mkdir -p "$out"
sed -e "s/__REF__/$ref/" -e "s|__JOBS__|[$jobs]|" tools/kaggle/train.py > "$out/train.py"
cat > "$out/kernel-metadata.json" <<META
{"id": "kavinjain/kavinengine-train", "title": "kavinengine-train", "code_file": "train.py", "language": "python",
 "kernel_type": "script", "is_private": true, "enable_gpu": true, "enable_internet": true,
 "dataset_sources": ["kavinjain/kavinengine-lichess"], "competition_sources": [], "kernel_sources": []}
META
.deps/kaggle-venv/bin/kaggle kernels push -p "$out"
