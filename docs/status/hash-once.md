> Status: CURRENT (2026-11-03)

# hash-once

- Design: none (an explorer's attribution, `docs/perf/agent-startup-2026-11-03.md` on `explore/agent-startup`); mechanism note `docs/mechanisms/recovery-digest-once.md`
- Branch / worktree: `phase/hash-once` at `~/code/Kronuz/Detent-wt/hash-once`; head: see the log
- State: implementing
- Done criteria (set by the lead):
  - [ ] K1 each digest of a pack is computed once per cold recovery, every comparison kept
  - [ ] K2 identity on the preserved 5M store (`selected`, 40/40 probes), before and after
  - [ ] K3 corruption tests, a mutant, ASan/UBSan and TSan
  - [ ] K4 5M and 100K restart A/B with an A/A under `quiet exclusive`
- Done: see the log
- Doing: implementation and tests
- Next: mutants, sanitizers, identity and timing
- Blocked on: nothing
- Findings folded in: none yet
- Design vs code: no design; the lead's brief said "hash each pack once". Three distinct digests exist per pack (segment checksum, page physical, page logical), each with its own comparison, so the floor with every comparison kept is three passes, down from six. Collapsing further changes what is checked (an owner-record-independent chain from `selected`), which belongs to the owner.
- Last verified: native Release `ctest -R "local|recovery|managed"`: 85/85 pass, 1 skipped (trusted paths)

## Log
- 2026-11-03: branch from main d4abd4a; baseline agent and the new test built; the new test on unchanged code gives `hashed=1760 pack=880` (twice), 880 plain flips all `invalid paged dependency graph`, 632 `invalid immutable page` + 233 `invalid paged local image` + 15 accepted (padding) with the owner record forged.
