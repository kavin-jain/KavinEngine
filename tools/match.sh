#!/usr/bin/env bash
# Fixed-length match for rating estimates. Usage: tools/match.sh OURS OPPONENT GAMES TC NAME
# Hash is set only for our engine (the opponent's option names may differ).
set -euo pipefail
cd "$(dirname "$0")/.."
OURS=$1 OPP=$2 GAMES=$3 TC=$4 NAME=$5
mkdir -p results
.deps/fastchess/fastchess \
  -engine cmd="$OURS" name=ours option.Hash=16 -engine cmd="$OPP" name=opponent \
  -each tc="$TC" \
  -openings file=books/UHO_Lichess_4852_v1.epd format=epd order=random \
  -rounds $((GAMES / 2)) -repeat -concurrency "${CONCURRENCY:-4}" \
  -report penta=true -recover \
  -pgnout file="results/$NAME.pgn" | tee "results/$NAME.log"
