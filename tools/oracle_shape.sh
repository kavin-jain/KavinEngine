#!/usr/bin/env bash
# Resize the Oracle bot VM without losing a game: drain lichess-bot (bot_ops off on lynxS), change the A1 shape (the
# VM reboots), set the bot's Threads and Hash to match, switch the bot back on. Does nothing if the shape already matches.
# Always Free (Oracle docs, read 2026-10-28): 1,500 OCPU-h + 9,000 GB-h per month = 2 OCPU / 12 GB all month; more
# is billed on Pay As You Go. 4 OCPU / 24 GB fits only part of a month.
# Usage: tools/oracle_shape.sh OCPUS MEM_GB THREADS HASH_MB      e.g. tools/oracle_shape.sh 2 12 2 1024
set -euo pipefail
ocpus=$1 mem=$2 threads=$3 hash=$4
ID=ocid1.instance.oc1.ap-mumbai-1.anrg6ljr4pcl4gicrtmh4bluplkbkfl542eynif4gib7uvd3bofk3ddfumpq
export SUPPRESS_LABEL_WARNING=True PATH=/opt/homebrew/bin:/usr/bin:/bin
OPS='~/Projects/lichess-bot/venv/bin/python ~/Projects/KavinEngine/tools/bot_ops.py'
want="RUNNING,$ocpus.0,$mem.0"
shape() { oci compute instance get --instance-id $ID --raw-output \
            --query 'data.["lifecycle-state","shape-config".ocpus,"shape-config"."memory-in-gbs"]' | tr -d ' \n[]"'; }
echo "$(date '+%F %T') now $(shape), want $want"
[ "$(shape)" = "$want" ] && { echo "nothing to do"; exit 0; }

ssh s-remote "$OPS off"  # finishes the running game first; Kavin gets the Telegram messages
for i in $(seq 360); do  # up to 3 h for the last game
  [ "$(ssh -o ConnectTimeout=15 oracle systemctl --user is-active lichess-bot || true)" = inactive ] && break
  sleep 30
done
oci compute instance update --instance-id $ID --shape-config "{\"ocpus\":$ocpus,\"memoryInGBs\":$mem}" --force > /dev/null
for i in $(seq 60); do [ "$(shape)" = "$want" ] && break; sleep 20; done
[ "$(shape)" = "$want" ] || { echo "VM not back as $want: $(shape)"; exit 1; }
until ssh -o ConnectTimeout=10 oracle true; do sleep 15; done
ssh oracle "sed -i -E -e 's/^(    Hash: +)[0-9]+/\1$hash/' -e 's/^(    Threads: +)[0-9]+/\1$threads/' ~/lichess-bot/config.yml &&
            grep -E '^    (Hash|Threads):' ~/lichess-bot/config.yml && nproc"
ssh s-remote "$OPS on"
echo "$(date '+%F %T') done: $(shape)"
