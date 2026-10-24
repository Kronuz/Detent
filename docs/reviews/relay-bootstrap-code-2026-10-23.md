# SC-1 relay snapshot lifecycle code review

**Candidate:** `ceb2487..36addc1` in `.scratch/relay-snapshot-lifecycle`  
**Basis:** `docs/reviews/scaling-2026-10-23.md`, “SC-1 implementation blueprint recheck”; `docs/status/relay-snapshot-lifecycle.md`; `docs/reviews/calibration.md`.  
**Verdict:** **APPROVE WITH CONDITIONS**. The finite admitted B→C path has no identified code blocker. One required process-level acceptance case remains unproved.

## Findings

**CR1 — Required delayed TLS bootstrap coverage is absent (condition).** The native regression in `tests/detent_delivery/test_bootstrap_retention.cc:98-155` opens at B=42 with C=48, evicts the live cache through revision 57, and verifies raw and compressed delivery through C. The real coordinator/TLS modes in `tests/detent_delivery/CMakeLists.txt:30-55,133-143` exercise relay transport but do not delay a snapshot consumer while writers evict its catch-up edges. The blueprint's component acceptance test 6 explicitly requires that combination at the real TLS boundary. Add that case and show it passes before landing. This is an evidence gap, not an observed path failure.

**Nonblocking:** `UpdateCache::capture_snapshot_path` uses the bounded chronological walker for validation and capture, but traverses coverage up to three times through `offer`, `edges_to_head`, and `walk_to_head` (`src/detent_distribution/update_cache.cc:116-126`). Its work remains O(E), E≤256, and no owner-executor yield permits the path to change between walks. A single capture traversal would match the blueprint more literally but is not a landing blocker.

## Checks

1. Read the implementation diff and checked admission, exact-window acknowledgment, final C continuity, expiry, fencing, plan retirement, cache ownership, authorization indexing, HTTP mode/target, and opt-in cache behavior against the blueprint.
2. Ran the prebuilt `test_relay_bootstrap_retention` under `quiet light`; it passed for raw and compressed frames. The baseline counterexample log in `.scratch/relay-retention-baseline/result.log` records `exit=-6` at its retained-offer assertion.
3. Ran prebuilt `test_delivery_coordinator` with its TLS fixture and `--subscription-relay-sessions` under `quiet light`; the 500-session relay case passed and reported one shared encoded object. An initial invocation omitted the required fixture argument and stopped at the test harness's `argc` check; the corrected invocation passed.

**Blocking condition:** CR1.  
**Not run:** Sanitizers, independent full-product suite, new real TLS delayed-consumer case, benchmarks, other platforms.
