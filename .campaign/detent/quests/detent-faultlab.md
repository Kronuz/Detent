# detent-faultlab: Detent's tests onto faultfs and the recorder (faultlab step 7)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-faultlab`.

## Outcome

Faultlab's step 7 (cpp-libraries `docs/design/faultlab.md:357`; D117, D119) has no quest: Detent's stores still sit on MemoryIO, and five files include `memory_io.h` (`cluster_harness.hh`, `test_history_*`, `test_namespace_power_cut.cc`), which blocks journal-drop-memory-io A1. After this quest the authority harness and its four tests run on `journal::IO` with faultfs, the mirror and agent-store crash tests use the recorder, network.py's PeerMesh runs on the proxy tier, and one power-cut model serves `test_paged_publication_artifacts.cc` and `test_agent_store_crash`. In faultlab, every test target sets `FAULTLAB_CHECK_COUNTS`, not only `faultfs_test.cc:8` (faultfs-collect-linear journal:47), done as a faultlab commit.

## Acceptance criteria

- A1 Nothing in Detent includes `memory_io.h` (`git grep` empty) and the converted tests pass: jobs.
- A2 One shared power-cut model drives both power-cut tests, and each still fails at its durability mutant (--fails-without): jobs.
- A3 Every faultlab test target sets `FAULTLAB_CHECK_COUNTS`, and faultlab's suite passes: jobs.
- A4 Detent's suite and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U3, J8).
