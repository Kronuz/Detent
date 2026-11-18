#!/bin/bash
# Static checks for namespace-s7-ids. A1: no storage identity in `.read`, `.owner` or segment ids, and
# every name in src/detent_local, src/detent_testing and the coordinator is derived in one place.
# A4: PagedSemanticLimits has no default bounds and nothing spells `PagedSemanticLimits{}`.
set -uo pipefail
failed=0
fail() {
    echo "FAIL: $1"
    failed=1
}
segment=$(sed -n '/^struct SegmentOwnership {/,/^};/p' include/detent_local/ownership.hh)
test -n "$segment" || fail "SegmentOwnership not found"
grep -q "StorageIdentity\|storage" <<<"$segment" && fail "SegmentOwnership holds a storage identity"
grep -n "storage" src/detent_local/reader_metadata.hh && fail "ReaderMetadata holds a storage identity"
grep -n "StorageIdentity\|storage" src/detent_local/paged_segment_builder.hh && fail "SegmentBuilder takes a storage identity"
grep -rn "RAND_bytes" src/detent_local src/detent_testing src/detent_delivery/coordinator.cc &&
    fail "a generation name from randomness"
grep -n "random" src/detent_testing/generation_writer.cc src/detent_delivery/coordinator.cc &&
    fail "a generation name from randomness"
derivers=$(grep -rlE "derive_generation|derive_segment" src include | sort | tr '\n' ' ')
# The agent store verifies its namespace cursors' names with the same primitive.
test "$derivers" = "include/detent_local/derived_ids.hh src/detent_local/agent_store.cc src/detent_local/derived_ids.cc src/detent_local/paged_segment_builder.cc " ||
    fail "names derived outside derived_ids, the segment builder and the agent store: $derivers"
for header in include/detent_local/builder.hh src/detent_local/paged_snapshot_plan.hh \
    src/detent_local/paged_maintenance_plan.hh src/detent_local/paged_delta_plan.hh \
    src/detent_local/paged_compaction_plan.hh src/detent_local/paged_reuse.hh; do
    grep -n "GenerationId" "$header" && fail "$header: a builder takes a generation name"
done
grep -n "next_generation" src/detent_local/disk_store.cc >/dev/null || fail "DiskStore does not check derived names"
limits=$(sed -n '/^struct PagedSemanticLimits {/,/^};/p' src/detent_local/paged_graph_validator.hh)
grep -q "= *[0-9]" <<<"$limits" && fail "PagedSemanticLimits has default bounds"
grep -rn "PagedSemanticLimits{}" src include benchmarks tests tools | grep -v "^tests/detent_local/derived_names_grep.sh:" && fail "PagedSemanticLimits{} remains"
test "$failed" -eq 0 && echo "derived names and semantic bounds: every static check passed"
exit "$failed"
