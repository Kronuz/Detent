#!/bin/sh
# Requeues what the Detent quests' next rounds need; each step retries a failure at most 3 times.
cd "$(dirname "$0")/../.." || exit 1
OUTAGE_NAME=detent . "$HOME/.scratch/campaign/outage/lib.sh"
for q in consensus-inline-sync-loop relay-mirror-idle-wake first-read-1ms persistent-map-adoption server-composition-adoption; do
	step "verify-$q" -- campaign verify "$q"
done
# Pushes that start CI on quest branches already published; a branch up to date is a no-op.
GH_TOKEN=$(gh auth token --user Kronuz) git -C /Users/kronuz/code/Kronuz/consensus-wt/detent/consensus-inline-sync-loop push -q 2>&1 | grep -v '^remote:$'
