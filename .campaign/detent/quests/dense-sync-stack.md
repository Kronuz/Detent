# dense-sync-stack: Grouped syncs for Dense pages publication

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-sync-stack`.

## Outcome

With Dense pages and grouped syncs both on `main`, a DTPAGE03 publication syncs its files in D11's
grouped order, as every other publication does, and the stacked effect is measured. On 2026-11-07
Dense alone published 609 files at 5M in "0 groups" with 1,221 device flushes; S2 alone made 27.
The owner asked whether the two stack and to test it in a branch; this quest answers with
measurements and lands the wiring if it is needed.

## Acceptance criteria

Sources: the owner's ask of 2026-11-07 and the comparison in `docs/measurements/dense-cold-bootstrap-20261107/`;
the S2 criteria of `namespace-s1-s2` applied to the Dense publication path.

- A1 A Dense publication syncs through the grouped order (owner records, barrier; payloads, barrier; descriptor, rename, directory, `change`); the agent log reports more than 0 groups at 100K: a job
- A2 T7 (`test_local_publication_power_cut`) passes over DTPAGE03 publications, restart-then-republish cycles included: a job
- A3 The sync counters of Dense publications match the sync seam: a job
- A4 Cold bootstrap, restart, files published and device flushes at 100K and 5M, stacked, against Dense alone and S2 alone, with an A/A, under `exclusive` (one arm per job): jobs
- A5 The power-cut, disk-store and artifacts tests pass in separate ASan/UBSan and TSan builds: the `asan` and `tsan` gates
- A6 [artifact] The stacked table beside the two single-change rows: `docs/perf/dense-sync-stack.md`

## Contracts

Consumes `namespace-s1-s2`'s `PublicationOperation`, sync counters and log line, and `dense-pages`'
DTPAGE03 publication artifacts. Provides no new format or API.

## Decisions

1. Parked on 2026-11-08 as `wip/dense-s2-parked` (`Detent:f7bf999`): a merge of `namespace/s1-s2`
   into the Dense head with the `src/detent_local/disk_store.cc` conflict markers committed. It is a
   reference for the conflict, not a base: this quest starts from `main` once both needs land.
2. Open (lead): if `namespace-s1-s2` lands first (D25), `dense-pages` must resolve the same conflict
   when it merges `main`, and A1-A3 may then hold already; fold this quest into that merge, or keep
   it for the measurement (A4, A6) only.

## May land early

None.
