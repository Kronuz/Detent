# pin-wave: journal

State: waiting on jobs: cascade L2 CI (task 45531185813, term-color 45531190808, repr 45531194490, sha256 45531199999, hashes 45531138947); build probe 261124-181616-ba7156
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

1. L2 CI: `.scratch/pin-wave/bin/wait_ci.sh REPO=SHA ...` (or ci.sh). When every job is green,
   fast-forward each main: `GH_TOKEN=$(gh auth token --user Kronuz) git -C .scratch/pin-wave/cascade/R
   push origin quest/detent/pin-wave:main`; then `bin/repin.sh $PWD/.scratch/pin-wave/cascade/R`
   for L3 (reactor, completion, strings, enum-reflection, logger), and so on.
2. Pin Detent to the cascaded heads; `pins` gate (A1).
3. Keep-going build job 261124-175149-a51919 (pins at the pre-cascade heads) lists what breaks.
4. A5 trace, A6 A/B, A7 CHANGELOG, A8 DEPENDENCIES.md.

## Blocked on

1. completion b4600a8 makes PosixIOOptions required at every PosixIO constructor (`PosixIOOptions{}`
   does not compile). Outside this file set: ~45 constructions in tests/ (detent_agent,
   detent_authority, detent_relay), 2 in benchmarks/ (detent_authority_durable_bench.cc:62,
   relay_window_bench.cc:139), ~15 in docs/design/async-drivers-bench/. The native gate builds
   tests and benchmarks, so it cannot pass until the lead adds them to the file set (or a
   quest holding them makes the edit). Recommendation: add tests/**, benchmarks/** PosixIO
   lines to pin-wave's set; FlushMode::full at each.
2. Lead's note (2026-11-22): load completion before consensus in CMakeLists.txt (journal's plain
   `set(COMPLETION_ASIO OFF)` still at journal a1d039a). CMakeLists.txt is outside the set.
3. FlushMode::full is not Detent's old behavior on macOS: its ordering barriers become full
   flushes (A5 (a)); plain would weaken File::sync to fsync. Full is the stronger superset; A6
   measures the cost. Lead's call whether that stands (recommendation: keep full; record it).
4. Top-level CMakeLists.txt: configure fails at the heads (job 261124-175149-a51919:
   `completion::asio` not found, src/CMakeLists.txt:69, src/detent_authority/CMakeLists.txt:27)
   until completion loads before consensus. Probe job 261124-181616-ba7156 applies that edit
   in its checkout and builds with -k 0 to list every other break.
5. consensus-driver-pressure-rearm (review) holds consensus CMakeLists.txt; its branch adds
   lines but changes no GIT_TAG. The consensus pin commit (L5) is held for the lead's word.

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
| L2 | task | wheel bfd72bc -> 075a40a | task:477a7d3 | 45531185813 running | not yet |
| L2 | term-color | static-string 7eaa8f9 -> a99260d | term-color:898710b | 45531190808 queued | not yet |
| L2 | repr | char-classify 25decb5 -> ee25a41 | repr:46fc92e | 45531194490 queued | not yet |
| L2 | sha256 | endian 6b223e2 -> 5634a08 | sha256:e41c586 | 45531199999 queued | not yet |
| L2 | hashes | static-string -> a99260d, char-classify -> ee25a41, constexpr-phf 4b669b6 -> 58653a7 | hashes:bae099b | 45531138947 queued | not yet |

## Result

<pending>

## Rulings I made

1. service_runtime.cc builds four PosixIOs (586, 598, 600, 602), not one: all four take one
   `constexpr PosixIOOptions full_flush{FlushMode::full}`, a single added line and one token
   per site.
2. The cascade also moves the declares that lag only by ci/docs commits (hashes, repr, sha256,
   strings, term-color, http-parser, enum-reflection's constexpr-phf, completion's metrics,
   journal's crc32c): the pins gate compares commits exactly.
3. faultlab's own pins (completion, journal) stay: journal pins faultlab, so moving faultlab
   would cycle; faultlab is not in Detent's manifest. consensus's faultlab pin moves to head.
4. Each repin commit goes to quest/detent/pin-wave in its repo first (CI runs on quest/**), and
   main fast-forwards to that same commit once green, so main never carries a red pin.
