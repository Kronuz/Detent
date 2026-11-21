# dense-distribution-build: journal

State: waiting on jobs 261120-161546-ecf083 (suite at Detent:2c0a7101), 261120-161540-0595dc
(positions-counted mutant), 261120-161518-53890f / 261120-161526-e8d2d5 / 261120-161517-fc6897
(managed failover x10 at the fix, the pre-fix head, main), and CI at Detent:2c0a7101.
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

This journal is the v2-sessions part (former namespace-s6-v1-removal, P4 of D94) and, after it,
the full copy as a page transfer (former DD3). The local layer (namespace-local-cursor) and
DD1/DD2/DD4 write their own journals on their branches; their branches merge in here.

## Doing

Suite failure at Detent:e9937a82 (test_system_managed_authority_failover, "oracle rejected agent
1"): this branch's harness, not the product (Detent:2c0a7101).
- Cause: Writers counted one position per applied receipt (Detent:ac278e50), but a write before
  positions are enabled takes the global revision (State::changed_namespace), so seeded namespaces
  sit far above the count (kept run managed-failover-J8wVsM: target 7, positions 7-22). The cohort
  waited for positions long passed and ran the oracle while agent 1 was one publication behind:
  load1 at 21 vs agent 0 at 22, the only difference key241 (0 vs 1), i.e. position 22's write.
  The earlier failure (job 261120-092532-58e85e, run bGv4iY, load2:key530) is the same: agent 1
  published after the oracle ran, and the oracle passes on that kept store now. Only this branch's
  jobs ever logged "oracle rejected" (main's one failure, 261111-155807-cb02cd, is "did not converge").
- Fix: positions come from each receipt's namespace_revision; Writers.verify checks them against the
  authority's read. Mutant tests/detent_system/mutants/positions-counted.patch.

Review-fix round (owner priority):
- F1 (Detent:7c82da19, Detent:dec05c43): TransferTarget names its namespace; every leaf is 'K' ns 0
  local 0 or 'W' ns 0; a full copy holds one watermark, a delta keeps one. Refused as Leaf ->
  records. Coordinator binds both pages paths; a delta failing with invalid_argument requests a
  repair (full copy). Tests: test_paged_transfer namespaces(), test_delta_transfer, the probe's
  foreign full copy and a foreign delta in test_pages_coordinator, an injected build failure in
  test_namespace_coordinator. Probe rerun 261120-125854-e45912.
- F3 (agent): pages frame [name, 5, after, cursor, root, offset, chunks]; a cut response keeps
  whole chunks, W5 next sends "resume": [name, root, after, offset], a frame at that offset
  continues the held prefix. test_pages_coordinator cuts the cold boot halfway (resumes from
  1,376,256 of 2,888,084 B). Relay half: fork.
- F5 (agent): records frames carry root | nil; a records build off that root counts
  transfers.diverged, leaves records_only and repairs. Tested in test_pages_coordinator. Relay
  half (fill root at head): fork.
- F7: "tree packed against D5" case in test_paged_transfer (unpacked()); the review's canonical
  mutant is tests/detent_local/mutants/f7-d5-packing-unchecked.patch.
- F2, F4, relay halves of F3/F5 (merged at Detent:b66d073b from
  quest/detent/dense-distribution-build--relay-window): the relay builds each head over the
  previous one (D7.2), serves the copy at s from DD2's canonical_copy, and its page window counts
  each segment once, bounded by the head's page bytes and 64 positions. A pages answer owns what it
  sends until encoded (F2; test_relay_pages answer_owns, mutant answer-borrows.patch under ASan:
  the review's probe uses the removed BuiltCopy, so the test is its port). Resume from offset and
  records root at head on the relay. 5M (10 x 500K, local quiet strict, docs/perf/relay-window.md):
  head-move CPU 63-912 ms against 1.2-1.5 s full builds (8 moves 2.7 s vs 11.2 s), live heap after
  8 moves 740 MB vs 787 MB, peak RSS 1,522 MB vs 1,640 MB; cold peak RSS 1,380 vs 1,263 MB (+9% at
  equal live heap, unattributed). Job 261120-134038-4ee64d reruns the after arm.
- F11 (Detent:39652e64): cause, CMake kept zlib's SDK/usr/include (MacOSX26.2.sdk via xcrun's link)
  as -isystem ahead of libc++ because brew clang's implicit SDK is MacOSX15.sdk; transfer_fuzz.sh
  configures with the compiler's own sysroot. Local 34 s smoke: 3,173 runs, 0 crashes. Hour: job
  261120-131414-94ce66. The target receives bound to "ns".
- F8, F9: not changed (rulings).
- F10, D139 (Detent:11c155ce, Detent:e7893553): a repair or D136-failed entry names the last
  published position as a W5 floor ([name, incarnation, position, true]); select_answer gives it a
  full copy, or Ahead while head is below it; the relay sends its canonical copy only at or above
  the floor, else records at the head; the agent skips any frame below it (repair.below). The store
  already refuses a repair below its selected base, so the agent check decides only for an entry
  the store no longer selects (D136). Tests: test_pages_coordinator (stale source two below,
  ignoring then honoring the floor, ends above it, never below), test_namespace_coordinator (D136
  path), test_relay_pages repair_floor, test_session_frames, test_namespace_session. Mutants
  d139-agent-publishes-below (killed locally, status 8), d139-relay-copy-below,
  d139-repair-takes-entries.
- F4 cold peak RSS: +117 MB (1,380 vs 1,263 MB, +9%) at equal live heap (821 vs 813 MB); steady RSS
  after 8 moves 1,522 vs 1,640 MB and head-move CPU 2.7 s vs 11.2 s both improve. Not attributed:
  both arms hold the build's segments and the copy's transfer, so freed memory the allocator keeps
  mapped is the likely cause; the cheap probe is the bench printing malloc_zone_statistics'
  size_allocated beside size_in_use. Recorded, not chased (lead message, D139 round).

Integration round:
1. Off-thread builds (Detent:251c6cc4): MirrorService runs builds on a builder thread; the mirror
   thread only reads the full copy's files and copies layout and ring. test_relay_pages' service
   case: at 100K a records answer goes in 3 ms while the build runs (pages answer at 250 ms; 60 ms
   when the records encode still ran on the mirror thread). Mutant
   tests/detent_relay/mutants/build-on-mirror-thread.patch (job 261120-114010-d77302).
2. test_system_relay_pages (Detent:22374016, run_relay_pages.py): 3 voters, a relay, a records agent
   on the authority and a pages agent on the relay. Local run: cold 100K boot in 1.83 s, one
   transfer of 2,168,898 B, 0 bytes hashed to build (records agent: 4,290,964 B), roots equal; a
   55,555-key clustered rewrite while the relay is SIGSTOPped arrives as one pages delta
   (1,205,282 B; relay deltas 0 -> 1). Forced root mismatch (forcing/relay-root-forced.patch +
   relay_pages_forced.sh; local run: refusal half passed, then the delta check was split for records sessions):
   refused 1, built 4,290,964 B from records, same root; the session's delta goes as records.
   Job 261120-114005-1bacdc.
3. Gate: suite 261120-113956-1e73f5 and CI at the head; then "ready for review".

## Done

- Steps 2-4: `distribution::FrameInput` (test_frame_input); DiskStore cycles of named plans,
  selection and recovery per table entry (D8); the coordinator on one W5 session with per-namespace
  records; readers attach by name (C3) (Detent:6906fb4a).
- Step 5: managed.json `namespaces` or all, `retry_interval_ms`; v1 client, probe and
  PublicationSink removed; host readiness per namespace (C4) (Detent:db87768a).
- Merged dd4's final branch (Detent:5f21ccba) and the relay helper's branch (Detent:26ab90b4).
- Tests: test_namespace_coordinator (cold start, entries, blocks, W7, repair, drop, offline and
  resumed restart) replaces the v1 coordinator tests; managed process test on v2 (Detent:26ab90b4).
- Step 6: system harness on per-namespace positions; the authority leader enables positions as it
  leads; GenerationUnavailable is fatal; a pooled malformed close fails closed (Detent:ac278e50).
  test_delivery_worker_lifetime holds the first full copy's build on the worker (Detent:514349e0).
  Local runs: 67/68 system and authority (relay_ipv6 then passed alone), worker and coordinator.
- Managed docs on W5 sessions, GCC `<vector>` include (Detent:1fadf5c5).
- Merged namespace-local-cursor at Detent:296228c3 (Detent:e52e8d31): the coordinator's v1 bridge
  goes, FrameInput sets a full copy's W1 target, single-plan `begin` publishes `disk_store_entry`
  (Detent:b8c2cf69). The v1 export seam (`detent::local_export`) and the agent's v1
  --generation-* identity flags go (Detent:410242d4). Local suite after the merge: 201/206, the
  five failures fixed and passing on rerun (startup and foreign-generation tests now expect C2's
  failed entry, not a fence; writers.py dropped a field the fakes lack).
- managed.json examples in the quickstart and guide carry v2 fields only; docs e2e passes
  locally against the build (23 blocks, 0 failed) (Detent:501f9c4d).
- Authority enables positions once its history is ready; the distribution service test runs W5
  first (its v1 sequence raced the enabling entry: 5/5 pass after) (Detent:4b0736e5). Relay
  mirror `reconnect_maximum_ms` and sync-failure logging (Detent:d9668727).
- Suite 261120-085935-8d23e7: 205/205 at Detent:d9668727.
- D136 idle seal: `AdministrativeKind::Seal`; the leader proposes it when positioned changes stay
  unsealed for `ServiceOptions::seal_after` (1 s); test_history_layout's seal_entry case
  (Detent:67a16fab). Design doc "Seal" rule 3, RELAY.md staleness line.
- D136 damaged entry: a selected entry's recovery failure (not pressure) calls `DiskStore::fail`
  and requests a repair; test_namespace_coordinator damages every `.owner` and both entries are
  replaced (fails without the fix: local mutant run timed out on the case) (Detent:be42ee82).
- Suite 261120-092520-b041d1: green at Detent:b6e0bea7.
- DD3 step 1: W6 `FrameKind::Pages` = 5, header [name, 5, after, cursor, root, chunk count], then
  bin chunks of 64 KiB (the last 1..64 KiB); the decoder raises the tokenizer's bin bound
  (`binary_limit`) to a chunk inside the frame only and bounds the transfer by `limits.bytes`;
  `PagesAnswer` on NamespaceRequest makes the encoder send one; FrameInput holds root and bytes
  (`root()`, `transfer()`), FramePreparer::Prepared and StagedFrames forward them.
  test_session_frames' pages_frames case (round trip at 3 feed sizes, zero root, short inner
  chunk, arity, body bound) (Detent:0e63b069).
- Agent reads credentials through `detent::internal::read_private_configuration`; README drops
  local_export, bundles and flat-v1; tls-fixture-diagnostics' coordinator mode repeats
  test_namespace_coordinator (Detent:407aed82). Docs link fix for the CI Docs failure at
  Detent:501f9c4d (Detent:c96dc63b).

## Next

1. Port to the W5 fixture what the retired v1 tests covered: hash-once (D12.2), scrub damage to
   repair, prepared startup; a W5 fleet load for the measurement arms.
2. (done, D136) Damaged entries.
3. `distribution::UpdateCache` is unused; ACTORS.md and RELAY_ARCHITECTURE.md describe bundles.
4. Step 7 (former DD3). Lead order: 1 pages frame (done), 2 relay generation and canonical copy,
   3 agent transfer -> plan with no packer through DD1, 4 fallback to records. Done means a 100K
   cold agent boots from pages with no packer, as a test job; then suite + CI, "ready for review".
   Open wiring decisions (mine to make, record under Rulings): the receiver opts in with
   `"pages":true` on W5 open (relays downstream and old agents get records); a next names
   `"records":[names]` to fall back after a refusal (D9: one other source, then records). The
   relay's RingSink (namespace_relay.cc:148) must refuse a pages frame (a relay mirrors records).
   Map:
   a. Local: a transfer plan builder (friend of PagedPlanState/PagedImage): name by
      `next_generation`, `derive_segment` as TransferSegmentId, `receive_transfer`, fill a
      PagedImage (Root needs records and namespaces counts the receiver does not return), then
      reuse PagedSnapshotPlanBuilder's CandidatePagedValidator tail for counts and histogram.
   b. Wire: W6 `FrameKind::Pages = 5` with encoding and 32-byte root commitment; the decoder's
      tokenizer caps bin at 32 bytes and buffers payloads whole (session_frames.cc:487), so pages
      go as one bin token per page (<= 64 KiB) with a raised per-token bound. FrameInput gains the
      transfer bytes.
   c. Sender: the relay (build tier, D7) holds no generation: it builds one per namespace from its
      mirror (full build at cold start, D7 after), keeps the canonical copy (DD2) and PageWindow,
      and answers a cold or below-cut session with Pages; the authority keeps records (Limit 6).
   d. Agent: build_frame takes Pages -> plan (a); a refusal asks records (one other source, then
      records, D9); delta pages through `receive_delta`.
   e. Tests: no packer is `hash.built.page_calls == 0 && pack_calls == 0` (CoordinatorStatus.hash)
      on a 100K cold agent; root mismatch falls back to records; a resumed fetch.

## Messages

- Lead message (D139 round): F10 is D139 option 2, done as above with the asked test
  (test_pages_coordinator, and the D136 path in test_namespace_coordinator). F4 cold peak RSS:
  recorded with its numbers (Doing), not chased. "ready for review" goes here when the gate is
  green; blocking findings' jobs are listed under Evidence, not the void ones.

- msg-1 (mine, to the folded namespace-local-cursor writer): its points stand as this branch's
  plan; the interim bridge (`namespace_cursor`, `distribution::frame_cursor`) is in
  Detent:6906fb4a, and the W1 local change deletes it when it merges here.

## Coordination with namespace-local-cursor

That branch moves the local types to the W1 cursor (Descriptor, paged::Root, builders take the W1
target from the caller). This branch's coordinator speaks W1 natively, so the coordinator's
v1<->W1 boundary mapping that branch plans (its journal, "Coordinator changes") is not needed:
builders taking a W1 target is all this branch needs from it. DiskStore's multi-entry change
(step 4) is this branch's; the single `scope` entry there is replaced here.

## Blocked on

Nothing. (Resolved by D136, kept for the record:) Lead ruling, relays and sealed history (the cause of test_system_managed_relay[_ipv6] failing
under load: 2 of 5 under `-j4`, 0 of 3 alone). The leader now enables positions as it leads, so
seal 0 is cut before the harness seeds; seals fire only every 2048 transactions (or 1/8 of the
log), and a relay mirrors sealed history only, so a quiet cluster's later writes never reach a
relay. Measured: in the failed run every voter cut one seal (seal 0, before `seeded`), and the
relay mirror stayed empty for 30 s with no sync failure. Before, a relay's first layouts
request triggered enabling, after the seed, which hid the gap. Options:
1. The leader proposes a forced seal when positioned changes stay unsealed for an interval
   (recommended: bounded relay staleness at any write rate; deterministic, as a log entry).
2. Relays also mirror the unsealed tail from W5 frames (larger; relay design change).
3. Enable on demand again (a W5 next or layouts request), which hides the gap for tests only.

The workflow and README items are fixed (Detent:407aed82); ci.yml keeps namespace-local-cursor's
removal of test_local_view, which no longer exists.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-085935-8d23e7: 205/205 at Detent:d9668727; 261120-081334-e4f6a8: 205/205 at Detent:501f9c4d (relay system tests flaky under load: see Blocked on; 261120-080129-538d87 failed them at Detent:410242d4) |
| A2 | <no evidence yet> |

Review fixes (confirm with these; mutant jobs 261120-125856-673c7a .. 261120-125908-7185f2 are void):

| Finding | Evidence |
|---|---|
| F1 | probe rerun 261120-125854-e45912 (refused); mutants killed: 261120-130720-4e5da1, 261120-130722-c1de00 (f1-unbound-transfer), 261120-130724-33d8ce (f1-stuck-delta-not-repaired) |
| F2 | 261120-141028-b54373 (test_relay_pages under ASan+UBSan), 261120-141032-1ff8f3 (answer-borrows mutant under ASan); the review's probe is ported as answer_owns |
| F3 | mutant killed 261120-130727-a5032b (f3-cut-prefix-dropped); relay resume in test_relay_pages |
| F4 | 261120-134038-4ee64d (5M strict, after arm); local before/after in docs/perf/relay-window.md |
| F5 | mutant killed 261120-130729-9a8e6e (f5-records-root-unchecked) |
| F7 | mutant killed 261120-130731-c646f9 (f7-d5-packing-unchecked) |
| F10/D139 | 261120-141019-07be70, 261120-141024-5b7b19, 261120-141026-8818b0 |
| F11 | 261120-131414-94ce66 (one CPU-hour) |
| Suite | 261120-134027-875c2b passed at Detent:820c6a8e; 261120-141018-363fb4 failed at Detent:e9937a82 (harness, fixed at Detent:2c0a7101); 261120-161546-ecf083 at Detent:2c0a7101 |
| Failover | mutant 261120-161540-0595dc; x10: 261120-161518-53890f (fix), 261120-161526-e8d2d5 (pre-fix head), 261120-161517-fc6897 (main) |

Accept map (ready for accept once suite 261120-161546-ecf083 passes; one confirming job each):
`campaign accept dense-distribution-build correctness --confirm F1=261120-125854-e45912
F2=261120-141032-1ff8f3 F3=261120-130727-a5032b F4=261120-134038-4ee64d F10=261120-141019-07be70`
(F1: the review's probe refused at the fix; F2: answer-borrows killed under ASan; F3:
cut-prefix-dropped killed; F4: 5M strict after arm; F10: d139-agent-publishes-below killed).

## Result

<none yet>

## Rulings I made

- The failover mutant lives at tests/detent_system/mutants/positions-counted.patch, a new file
  outside the listed file set (beside writers.py, which is in it); lead: add it to the set.
- (F8) The delta's wait for the build at the head stays on D6 rule 1's root-page bound: that
  build is now incremental over the previous head (63-912 ms at 5M, against 1.2-1.5 s for the full
  build the review priced), and the wait still ends when the head moves.
- (F9) damaged() stays broad: recovery reports damage as plain runtime_error, system_error (a
  missing or short file) and GenerationUnavailable alike, so narrowing it can leave a damaged entry
  failing forever (the stuck state F1 forbids), while a transient error misread as damage costs one
  full copy.
- (F1) An empty TransferTarget::name_space binds none: DD1's model corpora hold many namespaces in
  one tree. Every coordinator path names its namespace.
- (F1) A delta that fails to build against the namespace's base (invalid_argument) asks a full copy
  (D10.3) rather than retrying the same delta forever.
- (F3) Only W5 next carries "resume" (open starts a session, so nothing is held across it); the held
  prefix drops when any other frame of that namespace arrives.
- (F5) A records frame's root is nil unless the sender holds a build at that frame's head; the agent
  checks it only when present.

- Poll mode, `delivery_mode`, and the v1 identity fields (`group`, `epoch`, `principal`,
  `policy_epoch`, `reset_from`) leave managed.json: W5 sessions carry cursors, and a new
  incarnation is a full copy (W7), so no configured identity transition remains (greenfield).
- The relay's PublicationSink goes with its bundles: it had no other consumer.
- The v1-only tests (test_coordinator, test_repair, test_hash_once, test_prepared_startup,
  test_preparation_deadline, test_client, test_response, test_probe_*) and the v1 fleet load
  (benchmarks/fleet_load, test_system_fleet_*) are removed with the v1 client; their W5 ports
  are Next 1.
- `delivery::read_private_configuration` goes; the agent uses the shared reader (its messages
  now say "private configuration").
- D136's interval is 1 s (`ServiceOptions::seal_after`, no config key): the relay tests wait 30 s,
  relays sync every 250 ms, and no doc promises a tighter bound. A write rate below 2048/s now
  seals once a second, so seal files and roll-ups follow the clock on slow clusters.
- A recovery failure counts as damage unless it is StoragePressure, DiskPressure or bad_alloc.
- The authority leader proposes EnablePositions as soon as it leads (no longer on demand): a
  follower answers W5 only once positions are enabled, so on-demand enabling left followers idle.
- Lead message (namespace-local-cursor done, merge it with DD1 and DD4): merged at Detent:e52e8d31;
  DD1 (Detent:5b46a601) and DD4 (Detent:14b29e24) were already in. Its pause note holds: the
  coordinator's pause ends only on close, a new cancellation, or wake().
- Relay builds (integration round): a cold pages reader gets the newest build a delta still follows
  (same incarnation, not below the cut), not only the build at the head, and starts the build at
  the head in the background; without one it waits while a build is in flight (no frame, as
  Current). D7.4 sends the copy at s then the delta, and waiting for the head under writes starves.
- A pages delta below the newest build waits for the build at the head only when the records
  answer is at least a third of the newest build's root page (D6 rule 1 against a lower bound:
  every delta carries a root page), and only while that build targets the current head. Small
  deltas never wait; a head that moved during the build answers records (no starvation).
- A failed relay build answers records until the head moves; builds run one at a time, relay-wide.
- test_system_relay_pages widens the voters (4096 mutations a transaction; layout alpha 1, cap 4):
  at 128 a transaction seeding 100K took over 300 s, and the default cap (2 alpha = 1/4 copy) moves
  the cut past the agent's base for a 55% rewrite, which D10 then answers as a full copy.
- The forced root mismatch is a forcing patch job, not a suite test: the relay has no fault hook,
  and a frame's root comes from its own build.
- test_delivery_worker_lifetime moves under `if(TARGET detent::distribution_http)`: the agent-only
  configuration (mirror_mutant.sh) failed to configure on this branch (Detent:514349e0).
- The agent's v1 generation identity flags go rather than map through D88: a W5 generation's
  incarnation is the authority's, so a D88-derived identity never matches (greenfield).
- A worker build failure is a retried candidate failure, recorded in status, not a stop: the
  worker lifetime test's settled case closes after observing it.
