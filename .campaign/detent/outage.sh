#!/bin/sh
# Requeues what the Detent quests' next rounds need; each step retries a failure at most 3 times.
cd "$(dirname "$0")/../.." || exit 1
OUTAGE_NAME=detent . "$HOME/.scratch/campaign/outage/lib.sh"
for q in consensus-inline-sync-loop relay-mirror-idle-wake first-read-1ms persistent-map-adoption server-composition-adoption; do
	step "verify-$q" -- campaign verify "$q"
done
# first-read-1ms: A1's profile and the warm A/B ran at older code; rerun them at the head.
step first-read-a1-profile -- campaign job rerun 261125-031306-9e9536
step first-read-warm -- campaign job rerun 261125-031221-f08ca9
# consensus-inline-sync-loop: evidence reruns at consensus:80540ca (the ctest entry for R4-F1).
step isl-2ffaad -- campaign job rerun 261125-022326-2ffaad
step isl-3106af -- campaign job rerun 261125-022327-3106af
step isl-f2273d -- campaign job rerun 261125-051305-f2273d
step isl-8a339c -- campaign job rerun 261125-051308-8a339c
step isl-fd91ad -- campaign job rerun 261125-051312-fd91ad
step isl-0412f3 -- campaign job rerun 261125-051313-0412f3
step isl-1359b6 -- campaign job rerun 261125-051315-1359b6
step isl-363c84 -- campaign job rerun 261125-051316-363c84
step isl-738a52 -- campaign job rerun 261125-051318-738a52
# Pushes that start CI on quest branches already published; a branch up to date is a no-op.
GH_TOKEN=$(gh auth token --user Kronuz) git -C /Users/kronuz/code/Kronuz/consensus-wt/detent/consensus-inline-sync-loop push -q 2>&1 | grep -v '^remote:$'
