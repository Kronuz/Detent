> Status: CURRENT (2026-11-03) ports the retained-state part of the archived `33d36bf` onto `main`.

# Retained-state envelope

- Design: `docs/RETAINED_STATE_ARCHITECTURE.md` ("Detent integration"); ruling: the owner adopts both the `retained_state` object and the stricter replacement-pool funding check (`docs/status/salvage-2026-11-03.md`, "Blocked on" 1)
- Branch / worktree: `phase/retained-state` at `~/code/Kronuz/Detent-wt/retained-state`; head: see `git log`
- State: implementing
- Done criteria (set by the lead):
  - [ ] R1 `retained_state` object parsed and validated like the other objects, defaults equal the operating defaults
  - [ ] R2 funding check refuses with a message naming each term, the shortfall and the field to raise; boundary passes
  - [ ] R3 the replicated-profile question answered from the code
  - [ ] R4 docs, mechanism note and CHANGELOG with the upgrade note
  - [ ] R5 each check has a refusal test with the exact message and a boundary test; revert check per behavior
  - [ ] R6 existing authority tests pass unchanged; ASan/UBSan and TSan builds of the authority tests
- Done: code and tests in the first commit; docs, mechanism note, CHANGELOG
- Doing: building and running the authority tests
- Next: revert checks, a mutant, sanitizers, push, CI
- Blocked on: nothing
- Findings folded in: none yet
- Design vs code:
  1. The design says Detent "retains its configured `maximum_size(o.consensus)` funding check" and adds an envelope. The code keeps one check, on the retained suffix: validation requires every retained field to be at least its operating limit, so the retained suffix is at least the operating one and the second check could not fail alone (`node.cc` `validate_replacement_funding`). Resolution: design updated.
  2. The design says the policy defaults "to the existing library ceilings". The Detent default is the operating defaults (`node.hh`), because the library ceilings imply a 16 GiB suffix no replacement pool funds; Detent "supplies one stable funded retained-state envelope". `docs/mechanisms/authority-historical-decoding.md` had called the old default "1 MiB, the library ceiling"; the ceiling is 64 MiB. Corrected.
  3. Effective change for a configuration without the object: the retained bounds drop from the library ceilings (64 MiB command, 16 GiB log, 8 Mi entries, 256 MiB batch, 65,536 batch entries) to the operating defaults. Every journal the configuration language can write fits the operating defaults (`consensus_limits` only lowers, command and RPC bounds are not configurable), so recovery of existing stores is unchanged. A store written by a library user with larger operating limits needs `scheduling.retained` raised.
- Last verified: not yet

## Log
- 2026-11-03: worktree from `github/main` (`Detent:74e07c9`); first commit with the code and tests.
