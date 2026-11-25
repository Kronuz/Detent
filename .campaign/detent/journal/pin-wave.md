# pin-wave: journal

State: waiting on jobs: cascade L3 CI (reactor, completion, strings, enum-reflection, logger at the commits in the cascade table); local port build (quiet light pin-wave-ports)
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

1. L3 CI: `sh .scratch/pin-wave/bin/wait_ci.sh reactor=40d1461f66e62d5189e2406ccb9a0f20b40f184e
   completion=30615161bebf6adb8297a8a16b338cb1952ce692 strings=d93f2c0f82ce20f9a3a153ecf2b539774da43060
   enum-reflection=c9a3f04f20ea9fccea9b833cc3cbfd8ae393ce37 logger=65994c94a0dac58827953e7bcfc364b24e868294`;
   green: fast-forward each main (`GH_TOKEN=$(gh auth token --user Kronuz) git -C
   .scratch/pin-wave/cascade/R push origin quest/detent/pin-wave:main`); then
   `.scratch/pin-wave/bin/repin.sh $PWD/.scratch/pin-wave/cascade/R` for L4 (journal, http-parser),
   then L5 (http; consensus per Blocked on 1). A red head: stop at that level.
2. Local check of the ports (quiet light, tests CMake patch applied uncommitted, managed agent
   OFF): all 16 targets build; 18 of 19 ctest entries pass, the nine faultfs ones included
   (history_power_cut 50 s, installs 43 s, namespace_power_cut 15 s). test_authority_distribution_service
   fails at line 94 (base(1) answered Delta, not Snapshot) because its enabling of positions sits
   under DETENT_TEST_MANAGED_DELIVERY, off in this build; the A1 suite job (full profile) decides.
3. Pin Detent to the cascaded heads; `pins` gate (A1).
4. At the final head, queue once: `campaign verify pin-wave` (A1, A4); A5 w5 job
   `docs/perf/pin-wave-w5.sh .scratch/w5` with `--ref OLD=18c5bd23` (light); A5 syscall job
   `docs/perf/pin-wave-syscalls/run.sh 560c288b40ba55a8d42ff048bcb1c7eaa3624645 <completion head>
   .scratch/sys` (light); A6: build job `docs/perf/pin-wave-ab.sh build ~/.scratch/detent/pin-wave-ab`
   (light, --ref OLD=18c5bd23), then rows durable, update, get, reader, first-read
   (`row KIND ~/.scratch/detent/pin-wave-ab ROUNDS`, strict, 10 min, --after the build).
5. A7 CHANGELOG, A8 docs/DEPENDENCIES.md and DEPENDENCY_UPGRADES.md with the final pins.

## Blocked on

1. consensus repin (L5): rearm landed (consensus:c724c58), but consensus pins in its CMakeLists.txt
   (no cmake/dependencies.json in any library), which consensus-recovered-over-limit-start now
   holds. Options: (a) that quest takes the one-line-per-pin repin into its own commit; (b) the lead
   rules the pin lines out of its hold and I repin after it; (c) consensus stays at fcf7f66 for this
   wave. Recommend (b): the repin touches only the FetchContent_Declare tags.
2. tests/detent_authority/CMakeLists.txt (held by voter-read-freshness via tests/detent_authority/**):
   journal's head deletes journal/memory_io.h (journal:9f5e846), so the authority's in-memory tests
   move to faultlab's faultfs (Detent:8a182163) and nine test targets must link faultlab::faultlab.
   The patch is ready: .scratch/pin-wave/faultfs/tests-cmake.patch (adds faultlab::faultlab to nine
   target_link_libraries lines and fixes one comment). Recommend widening pin-wave by that file.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: cascade |
| A2 | heads' CI listed in the cascade table below |
| A3 | docs/SHARED_LIBRARY_DECISIONS.md "The November pin wave"; asked msg-358 |
| A4 | pending |
| A5 | trace: docs/perf/pin-wave-syscalls/run.sh (local run under quiet light; job at final head pending). New full: 72 calls over 31 steps (Xapiand's count). Against Detent's old pin 560c288 with default options, on the 28 steps both have (66 vs 68 calls): NOT identical. (a) 7 ordering barriers F_BARRIERFSYNC -> F_FULLFSYNC: completion:5942250 (full mode follows the full flush in every sync; old default had full_flush_ordering=false). (b) +1 fsync before F_FULLFSYNC in the executor's Sync primitive (drive_synchronously, native queue): completion:d5c6fd4 (Sync runs IO::sync_files). Old pin with full_flush_ordering vs new full differs only by (b). tools/w5 diff pending a building tree |
| A6 | pending |
| A7 | pending |
| A8 | pending |

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
| L3 | reactor | task 18eb516 -> 477a7d3 | reactor:40d1461 | pushed | not yet |
| L3 | completion | metrics 9db73de -> 4cd64db, allocators 07de45d -> ae775ab, task 95798a0 -> 477a7d3 | completion:3061516 | pushed | not yet |
| L3 | strings | char-classify -> ee25a41, split d48a28e -> 1fc9c33, static-string -> a99260d, repr d2eb895 -> 46fc92e, term-color 76a400b -> 898710b | strings:d93f2c0 | pushed | not yet |
| L3 | enum-reflection | constexpr-phf -> 58653a7, hashes 03ab2d6 -> bae099b | enum-reflection:c9a3f04 | pushed | not yet |
| L3 | logger | wheel -> 075a40a, workpool 640c66b -> bdf8932, term-color -> 898710b | logger:65994c9 | pushed | not yet |

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
10. faultfs port (Detent:8a182163): powered_off() is true once cut - 1 operations ran, before the
   refused one is tried; the durable state is the same as MemoryIO's throw at operation `cut`. A
   run with the power off treats any exception as the cut (MemoryIO's PowerLoss passed through
   Detent's handlers; faultfs's EIO is a std::system_error they may wrap).
7. Linux syscall trace not run: no Linux host here (docker daemon down) and the CI workflow is
   outside the set; docs/perf/pin-wave.md states Linux by completion's sync_call table.
4. Each repin commit goes to quest/detent/pin-wave in its repo first (CI runs on quest/**), and
   main fast-forwards to that same commit once green, so main never carries a red pin.
