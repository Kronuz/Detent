#!/bin/sh
# Checks that the old managed layout is gone and that only AgentStore writes the agent directory:
# no old layout names in the agent's libraries, the deleted modules stay deleted, the only directory
# writes in the agent's libraries are agent_store.cc and the change file it owns, and DiskStore
# builds the one PosixIO, which it hands to AgentStore. Exits 0 when all hold.
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
fail=0
say() { echo "$1"; fail=1; }

old=$(grep -rn -e 'storage\.meta' -e DTSELECT -e GenerationOwnership -e StorageIdentity -e encode_descriptor \
	-e generation_ownership -e storage_identity "$root/src/detent_local" "$root/include/detent_local" \
	"$root/src/detent_agent" "$root/src/detent_testing")
[ -z "$old" ] || say "old layout names remain:
$old"

for f in collector inventory publication record_reader storage_session paged_certificate \
	paged_dependency_check paged_named_root paged_segment_collection paged_segment_marks paged_segment_read; do
	for p in "$root/src/detent_local/$f.cc" "$root/src/detent_local/$f.hh" "$root/include/detent_local/$f.hh"; do
		[ ! -e "$p" ] || say "deleted module is back: $p"
	done
done

writes=$(grep -rln -e O_CREAT -e '[^d]linkat' -e renameat -e unlinkat -e create_exclusive -e 'filesystem::rename' \
	-e 'filesystem::remove' "$root/src/detent_local" "$root/src/detent_agent" "$root/src/detent_testing" \
	"$root/include/detent_local" | grep -v -e '/agent_store\.cc$' -e '/change_file\.cc$')
[ -z "$writes" ] || say "directory writes outside agent_store:
$writes"

changes=$(grep -rln 'ChangeWriter [a-z_]*(' "$root/src" | grep -v -e '/agent_store\.cc$' -e '/change_file\.')
[ -z "$changes" ] || say "a change writer outside agent_store: $changes"

io=$(grep -rn 'make_shared<c::PosixIO>' "$root/src/detent_local" "$root/src/detent_agent" "$root/src/detent_testing")
count=$(printf '%s\n' "$io" | grep -c .)
[ "$count" -eq 1 ] && printf '%s' "$io" | grep -q '/disk_store\.cc:' || say "PosixIO owners: $io"

[ "$fail" -eq 0 ] && echo "agent store is the sole writer; old layout gone"
exit "$fail"
