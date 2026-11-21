# detent-faultlab: Detent's tests onto faultfs and the recorder (faultlab step 7)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-faultlab`.

## Outcome

Faultlab's step 7 (cpp-libraries `docs/design/faultlab.md:357`; D117, D119) has no quest: Detent's stores still sit on MemoryIO, and five files include `memory_io.h` (`cluster_harness.hh`, `test_history_*`, `test_namespace_power_cut.cc`), which blocks journal-drop-memory-io A1. After this quest the authority harness and its four tests run on `journal::IO` with faultfs, the mirror and agent-store crash tests use the recorder, network.py's PeerMesh runs on the proxy tier, and one power-cut model serves `test_paged_publication_artifacts.cc` and `test_agent_store_crash`. In faultlab, every test target sets `FAULTLAB_CHECK_COUNTS`, not only `faultfs_test.cc:8` (faultfs-collect-linear journal:47), done as a faultlab commit.

## Acceptance criteria

- A1 Nothing in Detent includes `memory_io.h` (`git grep` empty) and the converted tests pass: jobs.
- A2 One shared power-cut model drives both power-cut tests, and each still fails at its durability mutant (--fails-without): jobs.
- A3 Every Detent test target that links faultlab sets `FAULTLAB_CHECK_COUNTS` (faultlab's own targets are faultlab's): jobs.
- A4 Detent's suite and CI pass: jobs.

## Notes

Starts after pin-wave (2026-11-20): faultlab:a3b71d1's faultfs needs completion past Detent's pin 560c288 (preallocate, map_shared, link), and a consistent move also takes journal and consensus forward, which is pin-wave's job.

Filed 2026-11-19 from the untracked-commitment audit (U3, J8).
- 2026-11-21, msg-194: faultlab:e3fcfe3 (faultlab-journal-oracle) removes faultlab's fetch of journal; the sector test checks recorded MemoryIO states (journal:a412175). Deleting memory_io.h from journal no longer breaks faultlab, so this quest can delete it once pin-wave moves faultlab to e3fcfe3 or later.
