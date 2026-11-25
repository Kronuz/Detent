#!/bin/sh
# Requeues what the Detent quests' next rounds need; safe to rerun (verify reuses identical jobs).
cd "$(dirname "$0")/../.." || exit 1
for q in consensus-inline-sync-loop relay-mirror-idle-wake first-read-1ms persistent-map-adoption resource-adoption server-composition-adoption; do
	echo "== $q"
	campaign verify "$q" 2>&1 | tail -3
done
# Pushes that start CI on quest branches already published; a branch up to date is a no-op.
for w in /Users/kronuz/code/Kronuz/consensus-wt/detent/consensus-inline-sync-loop; do
	GH_TOKEN=$(gh auth token --user Kronuz) git -C "$w" push -q 2>&1 | grep -v '^remote:$'
done
campaign job list 2>/dev/null | grep -c queued | sed 's/^/queued: /'
