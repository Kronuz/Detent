# hash-once: Digest each pack once during cold recovery

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status hash-once`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/hash-once.md`.

## Outcome

Cold managed recovery computes each of a pack's three digests once, keeping every comparison, which cuts the 5M restart by 15%.

## Acceptance criteria

- A1 [artifact] K1 each digest of a pack is computed once per cold recovery, every comparison kept: 6 passes to 3: `docs/status/hash-once.md`
- A2 [artifact] K2 identity on the preserved 5M store before and after: 0 probe failures in 9 runs: `docs/status/hash-once.md`
- A3 [artifact] K3 corruption tests over all 880 byte flips, plain and forged owner; 4 mutants killed; ASan/UBSan and TSan clean: `docs/status/hash-once.md`
- A4 [artifact] K4 5M and 100K restart A/B with an A/A under `quiet exclusive`: `docs/status/hash-once.md`

## Contracts

None: what is checked is unchanged.

## Decisions

1. Collapsing the three digests further changes what is checked, which belongs to the owner.

## May land early

None.
