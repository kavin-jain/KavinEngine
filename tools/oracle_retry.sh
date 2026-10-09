#!/bin/bash
# Retries creating the Always Free A1 VM until Oracle has capacity (Mumbai A1 is usually full). Size is the whole
# Always Free A1 allowance, 2 OCPU / 12 GB (Oracle docs, checked 2026-10-09: 1,500 OCPU-hours a month; it was 4 / 24).
# A bigger VM made during the trial is disabled and deleted after the trial unless the account is upgraded. Backs off on TooManyRequests. Tells Telegram when a VM exists, then stops. Runs as a systemd user
# service on lynxS (always on), not on the Mac (sleeps, changes networks).
set -u
export SUPPRESS_LABEL_WARNING=True OCI_CLI_CONFIG_FILE=$HOME/.oci/config
source "$HOME/.oci/launch.env"
OCI=$HOME/.venvs/oci/bin/oci
KEYS=$HOME/.oci/authorized_keys
cat "$HOME/.oci/mac_ssh_key.pub" "$HOME/.ssh/id_ed25519.pub" 2>/dev/null > "$KEYS"
n=0
while true; do
  n=$((n + 1))
  ocpus=2; mem=12
  out=$($OCI --no-retry compute instance launch --compartment-id "$COMPARTMENT" --availability-domain "$AD" \
    --shape VM.Standard.A1.Flex --shape-config "{\"ocpus\":$ocpus,\"memoryInGBs\":$mem}" \
    --image-id "$IMAGE" --subnet-id "$SUBNET" --assign-public-ip true \
    --boot-volume-size-in-gbs 100 --display-name "kavin-engine-${ocpus}c" \
    --ssh-authorized-keys-file "$KEYS" 2>&1)
  if echo "$out" | grep -q '"lifecycle-state"'; then
    id=$(echo "$out" | grep -o '"id": "ocid1.instance[^"]*"' | head -1)
    echo "$(date '+%F %T') attempt $n (${ocpus} OCPU): CREATED $id"
    "$HOME/.local/bin/hermes" send -t telegram -q <<< "🎉 Oracle VM created after $n attempts: ${ocpus} OCPU / ${mem} GB (kavin-engine-${ocpus}c). Ask Claude to migrate the bot."
    exit 0
  fi
  if echo "$out" | grep -qE 'Out of (host )?capacity'; then reason="Out of capacity"
  else reason=$(echo "$out" | grep -oE '"code": "[A-Za-z]+"' | head -1 | cut -d'"' -f4); fi  # Oracle's error code
  echo "$(date '+%F %T') attempt $n (${ocpus} OCPU): ${reason:-other error}"
  case "$reason" in
    Out*) sleep $(( 90 + RANDOM % 60 )) ;;
    TooManyRequests) sleep $(( 300 + RANDOM % 120 )) ;;
    "") sleep 120 ;;  # no answer from Oracle at all: the home network is down, try again later
    *) echo "$out" | head -20; "$HOME/.local/bin/hermes" send -t telegram -q <<< "⚠️ Oracle retry stopped: ${reason:-unexpected error}. Ask Claude to check (journalctl --user -u oracle-retry)."; exit 1 ;;
  esac
done
