#!/usr/bin/env bash
# SPRT: is NEW stronger than BASE?  Usage: tools/sprt.sh NEW_EXE BASE_EXE [ELO0] [ELO1] [TC]
# MacBook Air M3 is fanless with 4 P-cores: concurrency 4, run plugged in.
set -euo pipefail
cd "$(dirname "$0")/.."
NEW=$1 BASE=$2 ELO0=${3:-0} ELO1=${4:-5} TC=${5:-8+0.08}
mkdir -p results/sprt
.deps/fastchess/fastchess \
  -engine cmd="$NEW" name=new -engine cmd="$BASE" name=base \
  -each tc="$TC" option.Hash=16 \
  -openings file=books/UHO_Lichess_4852_v1.epd format=epd order=random \
  -rounds 30000 -repeat -concurrency "${CONCURRENCY:-4}" \
  -sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 model=normalized \
  -report penta=true -recover \
  -pgnout file="results/sprt/$(date +%Y-%m-%d_%H%M%S).pgn"
