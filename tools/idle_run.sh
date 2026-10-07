#!/usr/bin/env bash
# Runs the saved fastchess match (config.json) only at night (NIGHT_START..NIGHT_END, local hours; Kavin
# works remotely, so keyboard idle time says nothing), on AC power, and while other processes use
# < OTHER_MAX % CPU (e.g. no swarm sims). Pauses with SIGINT (fastchess saves config.json) when the window
# ends or another job starts; resumes when the Mac is free again. Exits when the match finishes.
# Usage: start the match with tools/sprt.sh, interrupt it once (Ctrl-C), then: tools/idle_run.sh LOGFILE
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=$1 NIGHT_START=${NIGHT_START:-1} NIGHT_END=${NIGHT_END:-9} OTHER_MAX=${OTHER_MAX:-350}
night()     { local h=$((10#$(date +%H))); if [ "$NIGHT_START" -lt "$NIGHT_END" ]; then [ "$h" -ge "$NIGHT_START" ] && [ "$h" -lt "$NIGHT_END" ]; else [ "$h" -ge "$NIGHT_START" ] || [ "$h" -lt "$NIGHT_END" ]; fi; }
other_cpu() { ps -Ao pcpu=,comm= | awk '$2 !~ /fastchess|build\/(dev|base)/ {s += $1} END {print int(s)}'; }
free_now()  { night && pmset -g batt | grep -q "AC Power" && [ "$(other_cpu)" -lt "$OTHER_MAX" ]; }

while :; do
  until free_now; do sleep 60; done
  echo "== resume $(date '+%F %H:%M')" >> "$LOG"
  caffeinate -i .deps/fastchess/fastchess -config file=config.json >> "$LOG" 2>&1 &
  pid=$!
  while kill -0 "$pid" 2>/dev/null; do
    sleep 15
    if ! night || [ "$(other_cpu)" -ge "$OTHER_MAX" ]; then
      pkill -INT -f "\.deps/fastchess/fastchess -config"; wait "$pid"
      echo "== paused $(date '+%F %H:%M') (other CPU $(other_cpu)%)" >> "$LOG"
      break
    fi
  done
  wait "$pid" 2>/dev/null
  tail -5 "$LOG" | grep -q "interrupted" || break
done
