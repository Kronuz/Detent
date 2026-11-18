# hash-once: journal

State: landed
Quest: .campaign/detent/quests/hash-once.md (the lead's; never edit it)

History: landed at `Detent:9b24fb3` on 2026-11-03, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/hash-once.md`: "[x] K1 each digest of a pack is computed once per cold recovery, every comparison kept: 6 passes to 3" |
| A2 | `docs/status/hash-once.md`: "[x] K2 identity on the preserved 5M store (`selected`, 40/40 probes), before and after: 0 probe failures in 9 runs" |
| A3 | `docs/status/hash-once.md`: "[x] K3 corruption tests (all 880 byte flips, plain and forged owner), 4 mutants killed, ASan/UBSan and TSan clean" |
| A4 | `docs/status/hash-once.md`: "[x] K4 5M and 100K restart A/B with an A/A under `quiet exclusive`: 5M 121.1 to 103.1 s (A/A 121.2)" |

## Result

Landed `Detent:6cacd3e` through `Detent:71db312`, review `Detent:9b24fb3`, on 2026-11-03.
5M restart 121.1 to 103.1 s (A/A 121.2); 100K 3.27 to 2.87 s (A/A 3.26); `hashed=880` for an 880-byte pack, was 1,760.
Native 85/85 of the recovery set; CI 41748094042 green after one unrelated rerun.
Review: `docs/reviews/hash-once.md`, APPROVE WITH CONDITIONS; CR1 and CR2 folded at `Detent:71db312`.
Docs: `docs/mechanisms/recovery-digest-once.md`, `docs/perf/recovery-digest-once.md`.
Left open: none.

## Rulings I made

`certify_digests` private to `DependencyDiscovery`; `record_reachability` clears the certificate.
