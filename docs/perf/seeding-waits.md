# Seeding wait investigation

## Question and identity

The 2026-10-23 capacity report records 100,000 mixed definitions seeded in 104.338 s on `0c1e5ad`, with four producers submitting sequential batches of 128 (784 transaction receipts). The 130,000 run took 138.714 s for 1,016 batches. The source and test diff between current `main` `ceb2487` and the reader-capacity build's `8b97fff` is empty; this investigation uses that already-built binary. No library setting or durability path changed.

## Profile of saved runs

The first `/v1/health` snapshot in each raw run is after seeding and includes a small amount of subsequent work, so its cumulative counters are upper bounds for the seed, not seed-only timings. Primitive order is `Write, Sync, Create, Replace, DirectorySync` (`completion/operation.h`). The local journal append performs data sync, then a manifest publication with another sync and a directory sync (`journal/mutation.h`). Its completion queue reported a maximum of one outstanding operation.

| Records | Seed wall s | First health revision | Sync count / sum s | Directory sync count / sum s | Write sum s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.697 | 10 | 38 / 1.31 | 19 / 0.45 | 0.26 |
| 10,000 | 10.997 | 82 | 180 / 5.23 | 90 / 2.31 | 1.09 |
| 100,000 | 104.338 | 786 | 1,608 / 49.62 | 804 / 22.23 | 10.62 |
| 130,000 | 138.714 | 1,018 | 2,140 / 65.31 | 1,070 / 30.19 | 13.76 |

At 100K, 804 storage publication cycles by the first health snapshot are close to 784 seed batches. Their two file syncs average 30.86 ms each; directory sync averages 27.65 ms. The combined 71.84 s of serial sync completions is 68.9% of seed wall time, but some cycles occurred after seed end. At 130K, the corresponding bound is 95.50 s (68.8% of 138.714 s). The 100K voter consumed 36.52 core seconds across three processes by seed end, so these waits are not explained by CPU saturation.

## Controlled run on current native sources

Command, after `quiet check` returned 0:

```sh
PYTHONPATH=/Users/kronuz/code/Detent/tests/detent_system quiet exclusive detent-seed-probe 5 '10K mixed seed storage telemetry' -- /usr/local/bin/python3 .scratch/seed_probe.py
```

The probe uses the existing reader-capacity build, three voters, the same capacity profile, four producers, mixed definitions, and 128 operations per transaction. It samples leader health every 0.5 s during `prepare`, then runs the complete-state `verify` oracle. Raw evidence is in `.scratch/seed-waits/.scratch/probe-10000/events.jsonl` in the main checkout. Result: 80 acknowledged and applied transactions, 10,000 operations, seeding in 10.084 s; full verification passed.

From the initial to the last in-seed leader snapshot, revision moved 0 to 79 and knobs 0 to 9,932. Sync count moved 6 to 145 and directory sync 3 to 72. Their summed completion times increased by 4.24 + 1.68 = 5.92 s, about 59% of the 10.22 s sample interval. The leader reported one maximum outstanding storage operation. The final snapshot precedes the last seed receipt, so 5.92 s is a lower bound for that run's total sync time. This is a single run: no cross-run spread or performance improvement is claimed.

## Conclusion and next experiment

Serial durability syncs are the largest measured cause of the long seed. The path does two file syncs and one directory sync per committed journal append, and the 100K run observes roughly 90 ms in these completions per publication. This does not assign the remaining ~30% to HTTP, proposal admission, replication, or application; the existing append response and publisher histograms overlap the storage wait and cannot be added to it.

The smallest follow-up is a 10K three-voter seed with request phase timestamps at client send, server admission, proposal acceptance, commit, and reply. Preserve bytes and durability. That separates the residual without treating correlated append-response latency as independent work. There is no code optimization or format change to finish from this explorer result alone.
