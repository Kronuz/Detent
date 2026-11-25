# pin-wave review: correctness

Commit: Detent:0b8a0df9ee23e0b6a016885bb251043a0987c273 (quest head Detent:7bfd8aaa adds only the journal)
Verdict: CHANGES
Date: 2026-11-25

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A4 native 261124-234933-d07efb at Detent:0b8a0df9: 220 of 220 tests pass (Release, authority,
  relay, managed agent, examples, benchmarks, tests, Python and Rust on). The faultfs ports run in
  it: namespace power cut 10.7 s, history power cut 47.2 s, installs 43.9 s, lagged 10.0 s, large
  base 18.4 s, history transport 2.9 s, cluster 17.0 s, relay chain 18.7 s; their coverage floors
  (`cuts - lagged > 200`, `cuts > 50`, `installs`) are asserted, so they still bite.
- Pins 261124-234933-f5eec6 at Detent:0b8a0df9: 30 libraries, 41 shared declarations, 0 mismatches.
- A5 w5 261124-234929-bde848 at Detent:0b8a0df9: snapshot JSON, wire frames and persisted images
  identical through the old and new builds (answer 3).
- Syscall trace 261124-234152-ea3a2e (completion 560c288 against 3061516; the script takes the
  revisions as arguments, so the Detent commit it ran at does not matter): every difference is the
  expected one, 7 `F_BARRIERFSYNC` to `F_FULLFSYNC` and 2 added `fsync`.
- A6 rows at Detent:75bb4ac1 (only a test link, tools/w5 and the journal changed since; the six
  bench targets and their sources did not): durable 261124-234153-bd757f, update
  261124-234154-1a26bd, reader 261124-234155-f7710c, first-read 261124-234155-00f17f, all `noise`
  against A/A. Durable ack 33.58 ms old, 33.81 ms new per transaction (old/new 1.013, range
  0.98-1.03, A/A 1.017); first read median 0.843 ms old, 0.893 ms new (1.037, range 0.78-1.32, A/A
  1.095). Get 261124-234154-693af5 failed (F2).
- Library heads: reactor:d28e5e0, faultlab:535f606 and 1ac0b17, consensus:770db9a and 72cc58d,
  journal:72bbc89 and f66c16e, http:24d2416 read diff by diff; their CI runs are the journal's
  cascade table (A2, not rerun).

## What I checked myself

Scripts under `.scratch/detent/review-pin-wave-correctness/` in the home checkout.

- `closure_pins.py` (job 261124-235858-b0c882 at Detent:0b8a0df9, and the same run inline): walks
  every Kronuz `FetchContent_Declare` reachable from the manifest, test directories reported apart,
  including libraries Detent does not pin itself (allocators, crc32c, ctrie): 33 repositories, one
  non-test revision each. The one other revision is faultlab's test-only MemoryIO fixture generator
  (`journal_src` at journal:a412175, under an OFF option, top-level builds only), never in Detent's
  closure. `git ls-remote` HEAD equals all 30 Kronuz pins today.
- `get_arms.sh` (job 261125-001335-330c58): the get bench in both built arms; both exit 134 on
  `400 ... "definition byte bound"`, so F2's failure is not the pins'.
- Diffs: every Detent call site of `http::Limits` against http:c813531's constructor, setters and
  `tls_connection_factory` defaults; every PosixIO site in Detent, faultlab and consensus; the
  faultfs ports against journal:e5f6ff1's MemoryIO; reactor's lag test; the w5 port.
- Where the 7 barrier flips land: the trace's marks precede their calls, so all 7 are
  `order_files_and_directory` or the `OrderFiles` primitive. No Detent source calls either, and in
  journal:f66c16e only `Host` and `TurnLog` do, which Detent does not use (grep), so on Detent's
  paths full mode changes only the `Sync` primitive's added `fsync` (N2).

Answers to the lead's questions:

1. Behavior: `FlushMode::full` at every PosixIO is stated, with its causes (completion:5942250,
   completion:d5c6fd4). The `http::Limits` migration keeps each value: old `maximum_connections`
   is `max_clients` (0 still means no bound in test_service_wire), `in_flight_limit` is
   `max_in_flight`, request timeouts, body caps and handshake deadlines are copied, grace is 5 s
   both sides (reactor:ab69cf2 `grace_{5000}`), and idle, header and inactivity are zero, as they
   defaulted at http:c813531. The 0-to-24 h timeout check existed before. The authority's TLS cap
   was 1024 = `kPublisherConnections`, so only a relay above 1024 connections changes, now matching
   docs/RELAY.md "Capacity"; stated. The relay still refuses a null TLS context, as
   `tls_connection_factory` did. The LagProbe change touches nothing in Detent, http, consensus or
   journal (grep); stated. faultfs is a superset of MemoryIO's crash states (per-sector coin kept,
   plus per-op namespace survival in order); its cut count counts the same operations. Not in the
   CHANGELOG: F1.
2. Library commits: reactor:d28e5e0 adds `clock.run()` after `start()` and `stop()` and one new
   check (`armed_timers() == 1`); no check removed or loosened. faultlab:1ac0b17 covers all 7
   PosixIOs of recorder_test. consensus:770db9a covers all 33 sites, the only other line being
   FaultIO's backend constructor; 72cc58d renames `parked` to `evicted` in three checks and
   DRIVER.md, journal's rename (journal CHANGELOG 2026-11-20). http:24d2416 is pins only. Detent's
   test migrations keep each bound; test_transport_https's capped case now proves `max_clients`
   bounds TLS at the accept, the mechanism that replaced the TLS cap.
3. A5: w5 261124-234929-bde848 (OLD = main bc86991a, whose code equals the quest base 18c5bd23:
   empty diff outside .campaign) prints `ok   snapshot JSON, wire frames and persisted images are
   identical`: w5_detent 71 cases (snapshot 20), store 20 files byte-identical, compressors 8722,
   http-parser 840, stox 1089, enum 14845, random 31, all 0 different. The one other difference,
   the logger transcript's line 437, is `__FILE__` depth (`../../../tools/w5` against
   `../../tools/w5`), the two checkouts' paths, not a library; http's own replay is skipped (N7).
   The w5 port is not a rename only (N3): what it compares changed in two ways, both forced and
   both applied to both arms.
4. A6: the rows are stated as measured, with A/A, under strict holds; durable is noise. The get
   row has no result (F2). N2 on what the durable row can show.
5. Pins: exact and consistent across the whole closure (probe above).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The CHANGELOG's "Results that changed" omits authority behavior the new journal and consensus pins bring, the two the wave waited for included. (a) consensus:d03a71b: a Worker recovered with its store above `hard - control_pool` runs a reclaim before Start instead of being refused with Pressure forever, so an authority reopened over its control headroom now starts. (b) journal:e57078d and journal:46a7a31: after a census, a Replacement may take a claim from control; while that cycle runs a Control permit can be refused instead (journal names consensus's install cutover, which may now answer Pressure). (c) consensus 2026-11-20: a leader paces each follower's appends from 64 KiB, not up to `rpc_bytes`; 2026-11-19: install starts only after a refusal at or below the base, and a checkpoint whose peak exceeds the replacement pool fails at once with `checkpoint_exceeds_pool`. (d) http 2026-11-18: a half-closed client is answered, EOF no longer cancels; 2026-11-19: a request enters admission at its first byte. The table and the guide link name the moves, not these results. | consensus CHANGELOG a197efb..72cc58d; journal `docs/mechanisms/admission.md` at f66c16e; http CHANGELOG c813531..24d2416; CHANGELOG.md "Pins: the November wave" | A line per item under "Results that changed" (A7); no code. |
| F2 | yes | gap | A6's get row has no result: `detent_authority_read_response_bench` exits 134 in both arms on `400 invalid_request "definition byte bound"` (src/detent_authority/state_codec.cc:210). The failure exists at the quest base and is not the pins', but A6 names "get". | 261124-234154-693af5 (failed); probe 261125-001335-330c58 (both arms) | Fix the bench's fixture to fit the default `definition_bytes` (a file outside the set, for the lead to widen), rerun the get row with A/A; or the lead names a get measurement that runs. |
| N1 | no | gap | "Linux makes the same calls in both modes" is argued from completion's `sync_call` table, not traced here (ruling 7). completion's own seam tests record each Linux call site in each mode (completion:5942250), so it is shown upstream, not in Detent. | docs/perf/pin-wave.md "Linux not traced" | Cite completion's sync seam test and its Linux CI run in that paragraph. |
| N2 | no | nit | The 7 barrier flips are all `order_files_and_directory` or `OrderFiles`, which no Detent path issues (grep in src and in journal's classic Journal), so on Detent's paths full mode adds only the `Sync` primitive's `fsync`. The CHANGELOG reads as if Detent's ordering points changed, and the durable row (authority journal) cannot show a cost for the refetchable stores' follow-up, since they use `sync_files` and `sync_files_and_directory`, which full mode leaves as the old pin made them. | trace marks of 261124-234152-ea3a2e; `git grep order_files` in Detent, journal, consensus | Say both in docs/perf/pin-wave.md and the CHANGELOG line. |
| N3 | no | nit | The w5 port changes what w5 compares, beyond renames: the snapshot fixture's value goes from `[1, i]` to `[i, nil, nil, []]`, and the image class from one flat image to the paged root plus segments sorted by id. Both are forced (the decoder takes the four-field value in both arms; the flat builder left before the wave, Detent:f6e8d3d6) and both apply to both arms, so A5 still compares like with like; ruling 20 calls it renames only. | Detent:0b8a0df9 tools/w5/w5_store.cc | Reword ruling 20 and A5's evidence line. |
| N4 | no | nit | The faultfs power-cut tests catch `...` once the power is off, so any exception after the cut counts as the cut. Needed (faultfs's EIO may reach Detent wrapped), and the asserted coverage floors still hold. | tests/detent_authority/test_history_power_cut.cc:323, test_namespace_power_cut.cc:238 | None needed. |
| N5 | no | nit | Stale `set_max_body`: src/detent_authority/service.cc:247 and docs/mechanisms/authority-reply.md:14 (outside the set). cmake/dependencies.json loses its final newline. docs/perf/pin-wave.md names the new completion `b4600a8`; the pin and the trace are 3061516 (pins only between them). | `git show 0b8a0df9:...` | One-line edits. |
| N7 | no | gap | A5 diffs no http input class: `http_replay` is skipped in both arms ("this detent-authority has no classic profile"), though http moved 73 commits (Limits, half-close, admission at the first byte, the TLS stream). The suite's service-wire and transport tests cover Detent's use; the logger transcript differs only by `__FILE__` depth. | 261124-234929-bde848 log | State both in docs/perf/pin-wave.md's input-class section, the skip with its reason. |
| N6 | no | nit | The quest note lists docs/design/async-drivers-bench as PosixIO sites; they build against their own pins (consensus:ab05507 in that CMakeLists), so leaving them unmigrated is correct. | docs/design/async-drivers-bench/CMakeLists.txt:40 | None. |

## Not run

- CI 45571227792 at Detent:7bfd8aaa (journal-only over the reviewed commit): linux-asan-ubsan,
  linux-tsan, both bindings jobs, pin-check, linux x86_64 gcc and clang, linux-arm64-gcc and the
  benchmarks are green; macos-arm64 and macos-x86_64 were still running when this was written.
- The Linux syscall trace (N1); the binding checks beyond the native job's Python and Rust targets.
- An old-pin run of the power-cut tests to compare cut counts by number; I relied on the asserted
  floors and the model comparison.
- A rolling-upgrade or mixed-pin cluster: Detent has no users to upgrade.
