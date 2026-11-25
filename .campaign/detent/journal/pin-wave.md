# pin-wave: journal

State: working
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

1. L2 CI (background wait), ff mains, then L3, L4, L5.
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
3. consensus-driver-pressure-rearm (review) holds consensus CMakeLists.txt; its branch adds
   lines but changes no GIT_TAG. The consensus pin commit (L5) is held for the lead's word.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: cascade |
| A2 | heads' CI listed in the cascade table below |
| A3 | docs/SHARED_LIBRARY_DECISIONS.md "The November pin wave"; asked msg-358 |
| A4 | pending |
| A5 | pending |
| A6 | pending |
| A7 | pending |
| A8 | pending |

### Cascade

| Repo | Pin moves | Commit | CI |
|---|---|---|---|

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
