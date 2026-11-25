# pin-wave: journal

State: waiting on jobs 261125-031954-f3baf1 261125-031956-84f5ff 261125-031958-0624b3 261125-032006-cda6ba 261125-032009-b3f010
Quest: .campaign/detent/quests/pin-wave.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Library cascade (lead's msg relayed 2026-11-24: cascade each library's own Kronuz pins to heads,
bottom-up, one commit per repo on main, each level's CI green first). Clones in
.scratch/pin-wave/cascade/<repo>; bin/repin.sh pins a repo's declares to heads and pushes
quest/detent/pin-wave; bin/wait_ci.sh waits on its runs; then main is fast-forwarded to that commit.

Levels (deps of a level are all at their final heads before it moves):
- L2: task (wheel), term-color (static-string), repr (char-classify), sha256 (endian),
  hashes (static-string, char-classify, constexpr-phf)
- L3: reactor (task), completion (metrics, allocators, task), strings (char-classify, split,
  static-string, repr, term-color), enum-reflection (constexpr-phf, hashes), logger (wheel,
  workpool, term-color)
- L4: journal (task, completion, allocators, crc32c), http-parser (enum-reflection)
- L5: consensus (task, completion, journal, reactor, faultlab), http (radix-router, http-parser,
  compressors, task, reactor)
Leaves (wheel, workpool, static-string, char-classify, constexpr-phf, endian, split, metrics,
allocators, radix-router, compressors, crc32c, faultlab) declare no Detent-set pins.

## Next

1. Read the get row 261125-032006-cda6ba (after build 261125-031958-0624b3, OLD = main dead7cab)
   into docs/perf/pin-wave.md and the Evidence table; slow-flush pair f3baf1 (must fail) and
   84f5ff (must pass); pins 261125-032009-b3f010 at Detent:9931c653; native reruns at the round end.

## Blocked on

None. F2's bench was widened and fixed (Detent:02159828): keys pad 38 bytes (definition 16,035 of
16,384), the bench creates its .scratch parent; the bound is unchanged. Root cause in ruling 23.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pins 261124-234152-d6560c at Detent:75bb4ac1: 30 libraries, 41 shared declarations, 0 mismatches; rerun 261124-234933-f5eec6 at Detent:0b8a0df9 queued |
| A2 | heads' CI listed in the cascade table below |
| A3 | docs/SHARED_LIBRARY_DECISIONS.md "The November pin wave"; asked msg-358 |
| A4 | native 261124-234933-d07efb at Detent:0b8a0df9 passed 220/220, asan and tsan green in CI 45571227792 (review); pins 261125-004230-dc7d45 at Detent:02159828 queued, native at the round end (261125-003938-bb7451 cancelled, superseded); asan, tsan: CI at the pushed head (run id below in State once listed). Earlier red (native 261124-212452-faeaad, CI 45553576282) was test_relay_chain's missing faultlab link, fixed at Detent:7553872a |
| A5 | w5 261124-234929-bde848 at Detent:0b8a0df9, OLD = main bc86991a (code = quest base 18c5bd23): snapshot JSON, wire frames and paged images identical, every class 0 different (docs/perf/pin-wave.md "Input classes"); http replay skipped in both arms (no classic profile); syscalls 261124-234152-ea3a2e at Detent:75bb4ac1 (2b8b12 at d63517c0 done). Earlier trace: docs/perf/pin-wave-syscalls/run.sh (local run under quiet light; job at final head pending). New full: 72 calls over 31 steps (Xapiand's count). Against Detent's old pin 560c288 with default options, on the 28 steps both have (66 vs 68 calls): NOT identical. (a) 7 ordering barriers F_BARRIERFSYNC -> F_FULLFSYNC: completion:5942250 (full mode follows the full flush in every sync; old default had full_flush_ordering=false). (b) +1 fsync before F_FULLFSYNC in the executor's Sync primitive (drive_synchronously, native queue): completion:d5c6fd4 (Sync runs IO::sync_files). Old pin with full_flush_ordering vs new full differs only by (b). tools/w5 diff pending a building tree |
| A6 | build 261124-234153-aeac1f; rows durable 261124-234153-bd757f, update 261124-234154-1a26bd, get 261125-032006-cda6ba at Detent:9931c653 queued after build 261125-031958-0624b3 (261125-004223-19cd2c failed the A/A arm's round 4: ruling 25; 261124-234154-693af5 failed in both arms: F2); slow-flush fails-without 261125-031954-f3baf1 at Detent:7c67c484 (tag pin-wave-unfixed-probe), fixed 261125-031956-84f5ff at Detent:9931c653; the reviewer's 21c94b (fails) and 475fb0 (passes) show the same, reader 261124-234155-f7710c, first-read 261124-234155-00f17f (Detent:75bb4ac1) |
| A7 | CHANGELOG.md "Pins: the November wave" (Detent:d2031ebc; operator-visible results per library commit, Detent:a45ad238): every library old -> new, pin commit and CI run, guide sections, changed results, TLS cap, owner decisions |
| A8 | docs/DEPENDENCIES.md (35 entries, 30 Kronuz at 2026-11-24 heads), docs/DEPENDENCY_UPGRADES.md "The November pin wave" (Detent:d2031ebc) |

### Cascade

Heads before the cascade, CI green (latest run per workflow at the head): wheel 075a40a #45040570944,
workpool bdf8932 #45040663845, static-string a99260d #45037446607, char-classify ee25a41 #44770293049,
constexpr-phf 58653a7 #45033506993, endian 5634a08 #44788033295, split 1fc9c33 #45037160963,
metrics 4cd64db #44927897459, allocators ae775ab #44763564672, radix-router 67454c7 #44935362952,
compressors 54dd2f0 #44771163868, crc32c 52889db #44775302241, faultlab 83fbde7 #45451339080,
atomic-shared-ptr ff50fae #44763580466, flat-map 06fc29e #41853472641, random db90603 #44936014336
(MSVC #45466487251), strict-stox aebfcd9 #45454496449, varint efed272 #45040473588. journal a1d039a
and consensus fcf7f66 had runs in progress (they move in the cascade).

| Level | Repo | Pin moves | Commit | CI (branch run) | main |
|---|---|---|---|---|---|
| L2 | task | wheel bfd72bc -> 075a40a | task:477a7d3 | 45531185813 success | fast-forwarded |
| L2 | term-color | static-string 7eaa8f9 -> a99260d | term-color:898710b | 45531190808 success | fast-forwarded |
| L2 | repr | char-classify 25decb5 -> ee25a41 | repr:46fc92e | 45531194490 success | fast-forwarded |
| L2 | sha256 | endian 6b223e2 -> 5634a08 | sha256:e41c586 | 45531199999 success | fast-forwarded |
| L2 | hashes | static-string -> a99260d, char-classify -> ee25a41, constexpr-phf 4b669b6 -> 58653a7 | hashes:bae099b | 45531138947 success | fast-forwarded |
| L3 | reactor | task 18eb516 -> 477a7d3; lag test runs the posted arm (ruling 11) | reactor:d28e5e0 (was 40d1461, red #45541868647) | 45547295801 success | fast-forwarded |
| L3 | completion | metrics 9db73de -> 4cd64db, allocators 07de45d -> ae775ab, task 95798a0 -> 477a7d3 | completion:3061516 | 45541875670 success | fast-forwarded |
| L3 | strings | char-classify -> ee25a41, split d48a28e -> 1fc9c33, static-string -> a99260d, repr d2eb895 -> 46fc92e, term-color 76a400b -> 898710b | strings:d93f2c0 | 45541886004 success | fast-forwarded |
| L3 | enum-reflection | constexpr-phf -> 58653a7, hashes 03ab2d6 -> bae099b | enum-reflection:c9a3f04 | 45541892260 success | fast-forwarded |
| L3 | logger | wheel -> 075a40a, workpool 640c66b -> bdf8932, term-color -> 898710b | logger:65994c9 | 45541900094 success | fast-forwarded |
| L4 | journal | task 95798a0 -> 477a7d3, completion b4600a8 -> 3061516, allocators -> ae775ab, crc32c 0d5460d -> 52889db | journal:72bbc89 | 45546842361 success | fast-forwarded |
| L4 | http-parser | enum-reflection a7202ad -> c9a3f04 | http-parser:38950a0 | 45546809878 success | fast-forwarded |
| L5 | http | radix-router e67543d -> 67454c7, http-parser -> 38950a0, compressors 5566260 -> 54dd2f0, task 18eb516 -> 477a7d3, reactor 6fdd45f -> d28e5e0; stale reactor comment | http:24d2416 | 45550470867 success | fast-forwarded |
| L5 | consensus | superseded: ff87da4 (on be18ad9) red, PosixIO without flush_mode | consensus:ff87da4 | 45553333430 failure | not moved |
| L4' | faultlab | completion d8b538c -> 3061516; fixture generator's journal fetch to test/fixtures/memory_io_gen.cmake (ruling 13); recorder test's 7 PosixIOs take FlushMode::full (faultlab:1ac0b17, local ctest 18/18) | faultlab:535f606 + 1ac0b17 | 535f606 45552204645 failure; 1ac0b17 45558784182 success | fast-forwarded to 1ac0b17 |
| L4'' | journal | faultlab 83fbde7 -> 1ac0b17, on main d999ae2 (lead's recovery-refill) | journal:f66c16e | 45558989609 success | fast-forwarded |
| L5' | consensus | task -> 477a7d3, completion d8b538c -> 3061516, journal 9e11713 -> f66c16e, reactor ab69cf2 -> d28e5e0, faultlab 5d4df9b -> 1ac0b17, on main b4516ee (pipelined-sim); FlushMode::full at all 32 PosixIOs (tests, benches, example, README); driver_close_test reads evicted() (journal renamed parked) | consensus:770db9a + 72cc58d (branch quest/detent/pin-wave-2) | 770db9a cancelled (superseded); 72cc58d 45563577936 success; local ctest 179/179 | fast-forwarded to 72cc58d |

## Result

<pending>

## Rulings I made

0. Lead rulings (relayed 2026-11-24): file set widened by CMakeLists.txt and 17 test and benchmark
   files; FlushMode::full everywhere for this wave, its macOS cost recorded (A5 doc) and measured
   (A6 durable row decides a follow-up for the refetchable stores); consensus waits for rearm.

1. service_runtime.cc builds four PosixIOs (586, 598, 600, 602), not one: all four take one
   `constexpr PosixIOOptions full_flush{FlushMode::full}`, a single added line and one token
   per site.
2. The cascade also moves the declares that lag only by ci/docs commits (hashes, repr, sha256,
   strings, term-color, http-parser, enum-reflection's constexpr-phf, completion's metrics,
   journal's crc32c): the pins gate compares commits exactly.
3. faultlab's own pins (completion, journal) stay: journal pins faultlab, so moving faultlab
   would cycle; faultlab is not in Detent's manifest. consensus's faultlab pin moves to head.
5. Linux `replace_sync_calls` gained an fdatasync seam (completion:5942250); the agent store tests
   pass nullptr there (the real call, as the old pin made it unseamed), one token per call, in
   test_agent_store.cc (held by first-read-1ms) as the same class as the FlushMode token.
6. The A6 and A5 old arm is the quest base 18c5bd23: main since then changed only campaign files.
8. Lead widened test_relay_mirror_crash.cc and test_paged_publication_artifacts.cc: one `nullptr,`
   each (Detent:4fd2ab2a).
9. http::Limits migration (Detent:5515c9f5) keeps every value: in-flight and connection caps,
   request timeouts (authority's configured timeout, relay 65 s, tests 40 ms), body caps
   (authority request_bytes, wire test 1 MiB, others the unchanged 16 MiB default), TLS handshake
   deadline and cap, grace 5 s (unchanged default); idle, header and inactivity set to zero, as
   they were before http turned them on. No Limits field: the TLS factory's own connection cap
   (1024 by default), now max_clients alone. Only the relay had it below its service cap, so a relay
   configured with connections above 1024 now admits that many TLS connections, as configured.
   The relay keeps refusing a null TLS context (the new constructor would serve plain HTTP).
   test_transport_https's queue-delay case drives http::detail::serve_tls_connection with a
   detail::ServiceState: http's own tests do not cover it. Stale comment outside the set:
   src/detent_authority/service.cc:247 names set_max_body.
10. faultfs port (Detent:8a182163; nine targets link faultlab at Detent:5cf3ba59, the widened tests CMakeLists): powered_off() is true once cut - 1 operations ran, before the
   refused one is tried; the durable state is the same as MemoryIO's throw at operation `cut`. A
   run with the power off treats any exception as the cut (MemoryIO's PowerLoss passed through
   Detent's handlers; faultfs's EIO is a std::system_error they may wrap).
11. reactor_lag red at reactor:40d1461 is reactor's test, not a task regression. Bisect (local,
   quiet light, .scratch/pin-wave/reactor-bisect/run2.sh, reactor 40d1461's lag_test): task f51aafb
   (627aca3's parent; differs from 18eb516 only in README) PASSED; task 627aca3 and a30bb0f FAILED
   the same five checks, busy 0 wakes; wheel bfd72bc vs 075a40a makes no difference (477a7d3 on
   bfd72bc fails too). Cause: task:627aca3 gives inline_scheduler a same-thread post_call; it has
   no runs_here by task's ruling (docs/reviews/cancellation-c2-code.md item 4: an inline drain's
   honest answer is false), so LagProbe::start()'s `!runs_here() && post_call()` now posts the arm
   to the clock's next run(), which [F] never called: the probe never armed. task never promised
   inline_scheduler lacks post_call. Fix in reactor (reactor:d28e5e0, amending the repin into one
   commit): [F] runs clock.run() after start() and stop(), plus one added check that the arm is
   filed; no check loosened. reactor_lag.h's start() comment and CHANGELOG 2026-11-24 state the
   posted arm on a scheduler without runs_here. With the fix: PASSED at task 477a7d3 + wheel
   075a40a and at task 18eb516 + wheel bfd72bc. Downstream users of inline_scheduler with
   LagProbe: none in Detent, http, consensus or journal (grep).
12. repin.sh's pin loop ended on an unmoved last pin with status false, so set -e killed it after
   sed but before the commit (journal's first run); the loop body now ends with `true`.
13. faultlab is in Detent's manifest since the faultfs port (Detent:8a182163), so ruling 3's
   premise is gone and check_pins reads faultlab's declares: completion d8b538c and journal
   a412175 (the MemoryIO fixture generator, OFF by default). journal pins faultlab, so following
   journal's head would cycle; and a412175 is a reference to a journal that still has MemoryIO
   (journal:9f5e846 deletes it), so it never moves. faultlab:535f606 moves that test-only fetch
   into test/fixtures/memory_io_gen.cmake (check_pins skips test/, as for every library's test
   declares) and pins completion to head. The generator rebuilds the checked-in fixture byte for
   byte (local, quiet light). Then journal re-pins faultlab to that head (journal J2), and Detent
   pins faultlab and journal there.
14. consensus at c724c58 (its main head: e61ac8f and c724c58, the rearm, driver.h and a test; same
   declares as fcf7f66), not re-pinned: its pin lines wait for consensus-recovered-over-limit-start
   (lead, msg 2026-11-24). check_pins fails on consensus's five declares until it re-pins; that
   bump and its A4 rerun are what remain after it lands.
7. Linux syscall trace not run: no Linux host here (docker daemon down) and the CI workflow is
   outside the set; docs/perf/pin-wave.md states Linux by completion's sync_call table.
4. Each repin commit goes to quest/detent/pin-wave in its repo first (CI runs on quest/**), and
   main fast-forwards to that same commit once green, so main never carries a red pin.
15. Round 2 commits stack (faultlab 535f606 + 1ac0b17, consensus 770db9a + 72cc58d) rather than
   amend: a force push was refused, so consensus re-pins on b4516ee under quest/detent/pin-wave-2.
16. Libraries' PosixIO migration (faultlab, consensus) follows ruling 1's shape: one file-scope
   `constexpr PosixIOOptions full_flush{FlushMode::full}` per file; consensus's README passes
   `{kronuz::journal::FlushMode::full}` inline. Test runtime: faultlab_recorder 1.56 s; consensus's
   slowest, driver_close_bytes (600.7 s Debug, loaded host), runs on HeapIO, no PosixIO, so the mode
   does not touch it; no test nears a timeout from the mode.
17. journal:72bbc89 renamed TurnDriver::parked to evicted (journal CHANGELOG 2026-11-20); consensus's
   three driver_close_test checks and DRIVER.md's close row follow the rename, no check loosened.
19. test_relay_chain is the one target missing faultlab: every target compiling cluster_harness.hh
   (test_cluster, test_history_cluster, test_namespace_cluster, test_namespace_history,
   test_lagging_incarnation, test_relay_chain) and every file including faultlab headers
   (history_power_cut, namespace_power_cut, history_transport, history_large_base) now links it;
   offline_runtime.hh includes no faultlab header (grep, Detent:7553872a).
20. w5_store port (Detent:0b8a0df9) changes what w5 compares, forced in both arms (review N3): flat ImageBuilder (deleted, Detent:f6e8d3d6)
   -> detail::PagedSnapshotBuilder as benchmarks/paged_snapshot_bench.cc drives it; the persisted
   image is now its root plus segments sorted by segment id, each saved as a file. The snapshot
   fixture's value is test_encoded_update.cc's four-field tuple (the three-field one throws "tuple
   arity mismatch" in both arms: Detent's own wire, not a pin). Local build at the head (quiet light):
   all eight drivers link and exit 0; w5_store writes 20 files (10 frames, 4 roots, 6 segments).
   paged_snapshot_builder.hh is identical at 18c5bd23, so the old arm compiles the same driver.
21. A5's OLD arm is main (bc86991a), whose code equals the quest base 18c5bd23 (empty diff outside
   .campaign), as ruling 6.
22. A6's rows at Detent:75bb4ac1 stand: since then the head changed a test CMake link, tools/w5/w5_store.cc
   and the journal; pin-wave-ab.sh builds only its six bench targets and runs ab_rounds.py and
   bench_rows.sh, none of which changed.
23. Review pin-wave-correctness (CHANGES at Detent:0b8a0df9), folded in:
   - F1 (Detent:a45ad238). Class: a library commit in the wave that changes what an operator of
     the authority, relay or agent sees. Swept every moved library's log old..new pin (CHANGELOG
     diff, and commit subjects where a library keeps none: task, logger, http-parser,
     radix-router, strict-stox, sha256, wheel, workpool and the header-only ones), keeping what
     reaches Detent's paths (Worker, Core, Peers, SnapshotChannel, the owned journal Store,
     PosixIO, http and reactor listeners, logger, strict-stox). Beyond the review's list: the late
     append success (consensus 2026-11-15), the lone voter's campaign at Start (2026-11-16), exit
     waiting for peer lookups (2026-11-19), idle TLS memory (http 2026-11-16), reactor's wheel
     timers, thread names, stop order and 16 MiB TLS file cap, the logger's exit drain
     (logger:cfcec69, 70e3c36, db35b6c) and strict-stox's libc path. Not operator-visible to
     Detent, so not listed: the Driver, host transport, seeds, quiescence, turn log and Host;
     follower reads (Detent's Start carries no nonce); journal's flush-mode bullet (stated).
     No test: a CHANGELOG has none; the class is checked by that sweep.
   - F2 (Detent:02159828, widened): one-knob-type made each override row key + 17 bytes, so the
     fixture's 256 overrides of 52-54-byte keys encoded to 18,083 bytes, over the default 16,384
     `definition_bytes`; keys now pad 38 bytes (16,035), plus create_directories for .scratch.
     Out of tree, both arms' unchanged binaries exit 134 and the fixed copy exits 0 plain and
     --escaped. Class: fixtures sized for the bool override; the other authority benches and tests
     pass at 0b8a0df9 (native 220/220), so no other one overflows. The get row is the test. The A/B
     builds the get bench from the head in both arms (Detent:dda447e3), since main's copy fails too.
   - N1, N2, N5 (perf doc and manifest newline), N3 (ruling 20), N7 (perf doc input classes):
     Detent:a45ad238. N5's stale `set_max_body` (service.cc:247, docs/mechanisms/authority-reply.md:14)
     is outside the set: left for the lead. N4, N6: nothing to do.
24. msg-1 (lead widened tests/detent_relay/CMakeLists.txt and tools/w5/**): done, Detent:7553872a
   and Detent:0b8a0df9; A4 and A5 passed at 0b8a0df9.
18. A/B script (Detent:eb2ce062): first_read_phases linked detent::local_client, removed with
   detent::Client (Detent:1c878402) before both arms, so A6's build never generated; it links
   detent::local_view and detent::testing_store now (both targets build locally at the new pins).
25. Get row 261125-004223-19cd2c died in the A/A (old) arm's round 4, 1 s after start (state dir
   created 00:51:55, journal 112 bytes: only the raft init record, no open committed; no stderr
   transaction line). Cause: the bench's setup budgets, not state or paths. The state dir is per
   pid and fresh (its .history and format stamp new); rounds share nothing. The first /v2/open is
   rejected NotLeader at once until election completes, and the bench gave it 100 tries 10 ms
   apart (~1 s); every later command had a 10 s service timeout. Beside 8 light holds, flushes
   (F_FULLFSYNC) slow election past 1 s; the old arm is the more exposed (no lone-voter campaign
   at Start). Class: setup waits sized for a fast disk: the open budget and the per-command
   timeout. Deterministic run: tools/w5/slow_flush_get.sh delays every fsync and F_FULLFSYNC
   400 ms (DYLD_INSERT_LIBRARIES); locally the 02159828 bench exits 134 under it (a transaction
   503 outcome_unknown, 280 s), and passes at 0 ms (74 s, loaded) and 200 ms (old 247 s, new 371 s).
   Fix (Detent:68398f85, the check unchanged): open retries until a 60 s deadline and prints its
   last reply; service timeout 60 s; neither is timed. Also removes the `<dir>.history` it leaked
   every run (40 left by the review's 40-run job). Jobs: ce01c0 at Detent:6a0bdd97 (probe on the
   unfixed bench, --fails-without), a34941 at Detent:68398f85.
26. The get row runs 4 rounds, not 8: the failed row took ~110 s a round in strict beside light
   holds (runs 22-55 s), so 8 rounds (~15 min) exceed the 10-minute strict cap and `--long`
   proves no criterion; 4 rounds (~7.5 min) fit. A rerun at 8 needs a quieter host or a split.
27. Review round 2 (CHANGES at Detent:68398f85), folded in:
   - R2-2 (Detent:511381cd): the probe ran its relative bench path after cd, so ce01c0 and a34941
     exited 127. Class: paths the probe uses after its cd (the bench; slow_flush.c is already
     absolute via $here); the bench path now resolves before it, and the work dir falls back to
     ./.scratch, not /tmp. Resubmitted: fails-without f3baf1 at Detent:7c67c484, a commit off
     Detent:6a0bdd97 (unfixed bench) carrying only the probe fix, tagged pin-wave-unfixed-probe
     locally; fixed 84f5ff at Detent:9931c653.
   - R2-1: build 0624b3 and get row cda6ba no longer wait on the probes (the row waits on the build).
   - R2-3 (Detent:9931c653): the timed reads print code and body before their check. No test:
     a diagnostic line, not behavior.
