# pin-wave: journal

State: waiting on jobs 261124-212116-6ccb33 261124-212116-9e90c2 261124-212150-61e8ff 261124-212152-a42d7d 261124-212154-1bc905 261124-212156-8c8c7c 261124-212158-4ce02b 261124-212200-05c6c6 261124-212201-b6e738 261124-212204-29d0cd; library CI http 45550470867, faultlab 45552204645
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

1. Jobs at Detent:d2031ebc (pushed for CI): native 261124-212116-6ccb33 (A4), pins
   261124-212116-9e90c2 (A1: expected red on consensus's five declares and faultlab's two until
   they re-pin, rulings 13-14); A5 w5 261124-212150-61e8ff, A5 syscalls 261124-212152-a42d7d; A6
   build 261124-212154-1bc905, rows durable 261124-212156-8c8c7c (5 rounds), update
   261124-212158-4ce02b, get 261124-212200-05c6c6, reader 261124-212201-b6e738, first-read
   261124-212204-29d0cd (8 rounds each, strict, 10 min). Read each into docs/perf/pin-wave.md
   (A5 w5 and A6 sections) and the Evidence table. A 124 row: rerun with fewer rounds.
2. Library CI: `sh .scratch/pin-wave/bin/wait_ci.sh http=24d24168aa110bf8dcc6167e1f2e0afc3c5fc277
   faultlab=535f606e5f0779fafa990ab030c44b14985d9a48`; green: fast-forward both mains. Then
   `sh .scratch/pin-wave/bin/repin.sh $PWD/.scratch/pin-wave/cascade/journal` (faultlab -> 535f606),
   its CI, fast-forward. Detent already pins http 24d2416 (on http's quest branch until then).
3. When the lead says consensus-recovered-over-limit-start landed: repin consensus (task,
   completion, journal, reactor, faultlab to heads), its CI, fast-forward; Detent pins consensus,
   journal and faultlab at those heads (CHANGELOG rows for faultlab, journal, consensus); rerun
   `campaign verify pin-wave` (A1, A4); push.

## Blocked on

1. consensus repin (L5): its pins live in its CMakeLists.txt, held by
   consensus-recovered-over-limit-start (in review). Lead ruling: consensus goes last, after that
   quest lands; the lead says when.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pins 261124-212116-9e90c2 at Detent:d2031ebc (consensus and faultlab declares remain, rulings 13-14) |
| A2 | heads' CI listed in the cascade table below |
| A3 | docs/SHARED_LIBRARY_DECISIONS.md "The November pin wave"; asked msg-358 |
| A4 | native 261124-212116-6ccb33 at Detent:d2031ebc; asan, tsan: CI at d2031ebc |
| A5 | jobs 261124-212150-61e8ff (w5), 261124-212152-a42d7d (syscalls) at Detent:d2031ebc. Earlier trace: docs/perf/pin-wave-syscalls/run.sh (local run under quiet light; job at final head pending). New full: 72 calls over 31 steps (Xapiand's count). Against Detent's old pin 560c288 with default options, on the 28 steps both have (66 vs 68 calls): NOT identical. (a) 7 ordering barriers F_BARRIERFSYNC -> F_FULLFSYNC: completion:5942250 (full mode follows the full flush in every sync; old default had full_flush_ordering=false). (b) +1 fsync before F_FULLFSYNC in the executor's Sync primitive (drive_synchronously, native queue): completion:d5c6fd4 (Sync runs IO::sync_files). Old pin with full_flush_ordering vs new full differs only by (b). tools/w5 diff pending a building tree |
| A6 | build 261124-212154-1bc905; rows 261124-212156-8c8c7c, 261124-212158-4ce02b, 261124-212200-05c6c6, 261124-212201-b6e738, 261124-212204-29d0cd |
| A7 | CHANGELOG.md "Pins: the November wave" (Detent:d2031ebc): every library old -> new, pin commit and CI run, guide sections, changed results, TLS cap, owner decisions |
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
| L5 | http | radix-router e67543d -> 67454c7, http-parser -> 38950a0, compressors 5566260 -> 54dd2f0, task 18eb516 -> 477a7d3, reactor 6fdd45f -> d28e5e0; stale reactor comment | http:24d2416 | 45550470867 pending | not moved |
| L4' | faultlab | completion d8b538c -> 3061516; fixture generator's journal fetch to test/fixtures/memory_io_gen.cmake (ruling 13) | faultlab:535f606 | 45552204645 pending | not moved |

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
