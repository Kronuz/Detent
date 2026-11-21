# detent-faultlab: journal

State: blocked on lead: the faultfs conversion needs a pin move (completion, journal, consensus, task) that is pin-wave's, and A3 needs a faultlab file set
Quest: .campaign/detent/quests/detent-faultlab.md (the lead's; never edit it)

## Doing

Nothing: every step waits on the rulings below. No code is changed on this branch.

## Next

1. On ruling 1: move the pins as ruled (cmake/dependencies.json), add faultlab, build the suite at the move alone (a job) before converting.
2. Convert cluster_harness.hh, test_history_large_base.cc, test_history_transport.cc to `fl::filesystem`/`fl::io` (consensus:5c25430 idioms: `make_directory` + `fl::io(fs, dir)`; `snapshot()` for disk copies; `list`/`view` for file maps).
3. One power-cut model for test_history_power_cut.cc and test_namespace_power_cut.cc: a cut is `power_off_after(cut - 1)`, a power loss `crash(seed, {.size = size_pick::sector})`, `powered_off()` after each driven job ends the run; enumerate cuts over `mutations()`. Rot (`inodes.at(id).durable[20] ^= 1`) becomes a write_at of the flipped byte plus sync on a snapshot. Where it lives is ruling 2.
4. Mutants (--fails-without, `tests/detent_authority/history_mutant.sh`, unchanged): table-before-outputs, collect-above-table, trust-local-files, durable-at-seal, trust-rotten-table, unchecked-fetch, table-unbounded (history_power_cut); old-fetch-bound, accept-low-fetch-bound, drop-*-bound (large_base); reader-serves-nothing, first-bytes-win, same-order, retry-failed-forever (transport). namespace_power_cut has no scripted mutant ("images that forget tombstones", namespace-s5-1 A5, was local): an inline job command.

## Blocked on

1. Lead: the pin closure. faultlab:a3b71d1's faultfs.h overrides `File::preallocate`, `File::map_shared` and `IO::link` (faultfs.h:1042, 1071, 1273), which Detent's completion pin 560c288 does not declare (completion:a14893a adds them; 560c288 is 28 commits behind completion:d8b538c). So faultfs does not compile in Detent at today's pins. Moving completion alone fails CI's `tools/check_pins.py` (ci.yml:86), since journal:e5f6ff1 declares completion 560c288. The smallest consistent move found:
   - completion -> d8b538c (declares task 18eb516), journal -> 9e11713 (97 commits; declares task 95798a0, completion d8b538c, faultlab a3b71d1), consensus -> main bb961b3 (295 commits; same task, completion, journal, faultlab), faultlab a3b71d1 new.
   - It is still not consistent: completion d8b538c declares task 18eb516 while journal and consensus declare task 95798a0, and faultlab a3b71d1 declares journal a412175 (`journal_memory_io`, sector_test's headers) against any Detent journal pin. Closing it needs a completion commit pinning task 95798a0 and a faultlab change (or a check_pins rule for faultlab's headers-only declare).
   Options: (a) recommended: sequence detent-faultlab after pin-wave (MAP: planned, high risk, needs reader-s6 and legacy-h), which moves every Kronuz pin to its head anyway; this quest then only converts tests. (b) Make this quest carry a partial pin wave (the four repos above plus the completion and faultlab commits): high risk, moves production consensus by 295 commits, overlaps pin-wave and dense-distribution-build's cmake edits. (c) Pin an older faultlab that predates the seam: no; it diverges from journal and consensus and drops the crash model they test on.
2. Lead: a file for the shared power-cut model. Neither power-cut test includes the other's headers (history.hh vs node.hh); one model wants a new header, e.g. tests/detent_authority/power_cut.hh, outside my file set. Alternative: put it in cluster_harness.hh (no new file, but that header is the three-voter cluster).
3. Lead: A3 is a faultlab commit (CMakeLists.txt sets `FAULTLAB_CHECK_COUNTS=$<BOOL:${FAULTLAB_TEST_CHECK_COUNTS}>` on each test target and faultlab_demo, an option defaulting ON so FAULTLAB_COLLECT_SCALING can still build with 0; faultfs_test.cc:7-9's #define goes; crash-model.md:31 says "as every faultlab test is"). Editing faultlab was refused: it is outside this quest's file set. It needs faultlab's CMakeLists.txt, test/faultfs_test.cc and docs/mechanisms/crash-model.md in a file set (and a word with Xapiand, who owns faultlab's strata work), and a faultlab worktree. A Detent pin of the new faultlab commit would again mismatch journal and consensus (a3b71d1) under check_pins, so Detent keeps a3b71d1 until they move.

## Evidence

No jobs: nothing is changed. The blocker is git facts: `git -C completion show 560c288:include/completion/io.h` has no preallocate, map_shared, link or SharedMapping; the declares above come from each repo's CMakeLists.txt at those commits.

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

Messages: the coordinator's constraint (no faultfs member added without asking first; no shim over memory_io.h) is noted; the plan in Next needs no new faultfs member.

## Result

None yet.

## Rulings I made

- No shim over memory_io.h and no MemoryIO-shaped adapter: the tests move to faultfs's own API (coordinator constraint).
