#!/bin/bash
# lichess-bot's engine on lynxS (engines/kavinengine-pool in config.yml, concurrency 2). One game per machine: the first
# engine runs on the Oracle VM (ssh host "oracle", the same engine built there), a second simultaneous game here. The VM
# has 2 cores, so the Threads 3 that lichess-bot sends become 2 there. Playing first on the VM also keeps it busy:
# Oracle reclaims Always Free VMs that idle for 7 days. A third game (an arena pairing during two games) or an
# unreachable VM shares lynxS, where lower_engine_priority favours arena games.
here="$(dirname "$0")/kavinengine"
exec 8> /tmp/kavinengine-oracle.lock 9> /tmp/kavinengine-lynxs.lock
if flock -n 8 && ssh -n -o BatchMode=yes -o ConnectTimeout=5 oracle true; then  # -n: the check must not eat UCI input
  exec 9>&-
  exec ssh -T -o BatchMode=yes -o ServerAliveInterval=5 -o ServerAliveCountMax=3 oracle \
    'sed -u -e "/^quit/q" -e "s/^setoption name Threads value .*/setoption name Threads value 2/" | KavinEngine/kavinengine'
fi
exec 8>&-
flock -n 9  # the second game holds the lynxS slot; a third shares it
exec "$here"
