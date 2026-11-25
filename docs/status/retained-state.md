> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning. Was: (2026-11-03) ports the retained-state part of the archived `33d36bf` onto `main`.

# Retained-state envelope

- Design: `docs/RETAINED_STATE_ARCHITECTURE.md` ("Detent integration"); ruling: the owner adopts both the `retained_state` object and the stricter replacement-pool funding check (`docs/status/salvage-2026-11-03.md`, "Blocked on" 1)
- Branch / worktree: `phase/retained-state` at `~/code/Kronuz/Detent-wt/retained-state`; head: see `git log`
- State: in review
- Done criteria (set by the lead):
  - [x] R1 `retained_state` object parsed and validated like the other objects, defaults equal the operating defaults
  - [x] R2 funding check refuses with a message naming each term, the shortfall and the field to raise; boundary passes
  - [x] R3 the replicated-profile question answered from the code
  - [x] R4 docs, mechanism note and CHANGELOG with the upgrade note
  - [x] R5 each check has a refusal test with the exact message and a boundary test; revert check per behavior
  - [x] R6 existing authority tests pass unchanged; ASan/UBSan and TSan builds of the authority tests
- Done: `Detent:b0471e9` code and tests; `Detent:2a916f0` docs, mechanism note, ledger, CHANGELOG; `Detent:6d3d61f` evidence
- Doing: nothing
- Next: the lead lands after the code review and a green CI run on the rebased head
- Blocked on: nothing
- Findings folded in: none yet
- Design vs code:
  1. The design says Detent "retains its configured `maximum_size(o.consensus)` funding check" and adds an envelope. The code keeps one check, on the retained suffix: validation requires every retained field to be at least its operating limit, so the retained suffix is at least the operating one and the second check could not fail alone (`node.cc` `validate_replacement_funding`). Resolution: design updated.
  2. The design says the policy defaults "to the existing library ceilings". The Detent default is the operating defaults (`node.hh`), because the library ceilings imply a 16 GiB suffix no replacement pool funds; Detent "supplies one stable funded retained-state envelope". `docs/mechanisms/authority-historical-decoding.md` had called the old default "1 MiB, the library ceiling"; the ceiling is 64 MiB. Corrected.
  3. Effective change for a configuration without the object: the retained bounds drop from the library ceilings (64 MiB command, 16 GiB log, 8 Mi entries, 256 MiB batch, 65,536 batch entries) to the operating defaults. Every journal the configuration language can write fits the operating defaults (`consensus_limits` only lowers, command and RPC bounds are not configurable), so recovery of existing stores is unchanged. A store written by a library user with larger operating limits needs `scheduling.retained` raised.
- Last verified (2026-11-03, macOS x86_64, Homebrew LLVM 23.1.2, RelWithDebInfo `-O1 -g`, asserts on):
  - Native `test_authority_{state,node,node_completion,service,cluster,cluster_completion,process}`: 7 of 7 pass (35.6 s). Existing cases pass unchanged.
  - CI run 41723121996 at `Detent:db1a389` (same code before the rebase onto the docs-only `Detent:d4abd4a`): linux-x86_64-gcc, linux-x86_64-clang, linux-arm64-gcc, macos-arm64, macos-x86_64, linux-asan-ubsan, linux-tsan and linux-benchmarks green; freebsd and headers are skipped by the workflow.
  - Mutants, each applied to `src/` alone against the new tests (`.scratch/mutants.py`; `.scratch/mutants.log`): M1 old operating-only funding check restored (node and service tests abort), M2 library-ceiling defaults (both abort), M3 retained validation removed (both abort), M4 `log_bytes` member ignored by the parser (service aborts, node passes), M5 wrapping sum (node aborts), M6 Worker handed the default envelope instead of the configured one (node aborts at `lowered-envelope-application`), M7 allowance 4095 (both abort). All seven die; the tree is restored after each.
  - Local sanitizer builds of the same seven authority tests (Debug `-O1`): ASan+UBSan with `-fno-sanitize-recover=undefined` and leak detection, 7 of 7 pass (34.6 s); TSan, 7 of 7 pass (49.3 s). Build dirs under `.scratch/`, removed after the push.
  - Replicated-profile question (R3): the retained values are not in `semantic_profile` (ten application limits, `state.cc:183`), the `DTABAT02` header (`state.cc:1064`), the checkpoint format or `profile_fingerprint` (application profile, configuration, operating limits). They stay local; the design agrees.

## Log
- 2026-11-03: worktree from `github/main` (`Detent:74e07c9`); first commit with the code and tests.
- 2026-11-03: native, CI, seven mutants, local ASan/UBSan and TSan green; rebased on `github/main` and pushed.
