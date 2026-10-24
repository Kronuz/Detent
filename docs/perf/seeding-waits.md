# Seeding wait investigation

## Question and identity

The 2026-10-23 capacity report records 100,000 mixed definitions seeded in 104.338 s on `0c1e5ad`, with four producers submitting sequential batches of 128 (784 transaction receipts). The 130,000 run took 138.714 s for 1,016 batches. The source and test diff between current `main` `ceb2487` and the reader-capacity build's `8b97fff` is empty; this investigation uses that already-built binary. No library setting or durability path changed.

## Profile of saved runs

The first `/v1/health` snapshot in each raw run is after seeding and includes a small amount of subsequent work, so its cumulative counters are upper bounds for the seed, not seed-only timings. Primitive order is `Write, Sync, Create, Replace, DirectorySync` (`completion/operation.h`). The local journal append performs data sync, then a manifest publication with another sync and a directory sync (`journal/mutation.h`). Its completion queue reported a maximum of one outstanding operation. `io_submitted_to_reaped_ns` spans queue admission, the syscall, notification, and owner reaping (`completion/asio_completion.h`); none of the sums below measures syscall time alone.

| Records | Seed wall s | First health revision | Sync count / sum s | Directory sync count / sum s | Write sum s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.697 | 10 | 38 / 1.31 | 19 / 0.45 | 0.26 |
| 10,000 | 10.997 | 82 | 180 / 5.23 | 90 / 2.31 | 1.09 |
| 100,000 | 104.338 | 786 | 1,608 / 49.62 | 804 / 22.23 | 10.62 |
| 130,000 | 138.714 | 1,018 | 2,140 / 65.31 | 1,070 / 30.19 | 13.76 |

At 100K, 804 storage publication cycles by the first health snapshot are close to 784 seed batches. Their two file sync completion intervals average 30.86 ms each; directory sync completion averages 27.65 ms. The combined 71.84 s of serial completion intervals is 68.9% of seed wall time, but some cycles occurred after seed end. At 130K, the corresponding post-seed bound is 95.50 s (68.8% of 138.714 s). The 100K voter consumed 36.52 core seconds across three processes by seed end, so CPU saturation is not supported by these samples.

## Controlled run on current native sources

Command, after `quiet check` returned 0:

```sh
PYTHONPATH=/Users/kronuz/code/Detent/tests/detent_system quiet exclusive detent-seed-probe 5 '10K mixed seed storage telemetry' -- /usr/local/bin/python3 .scratch/seed_probe.py
```

The probe uses the existing reader-capacity build, three voters, the same capacity profile, four producers, mixed definitions, and 128 operations per transaction. It samples leader health every 0.5 s during `prepare`, then runs the complete-state `verify` oracle. Raw evidence is in `.scratch/seed-waits/.scratch/probe-10000/events.jsonl` in the main checkout. Result: 80 acknowledged and applied transactions, 10,000 operations, seeding in 10.084 s; full verification passed.

The last snapshot before seed completion reached revision 74 and 9,472 knobs, and its sync completion interval sum had grown by 5.52 s from the initial snapshot. The first snapshot after seed completion reached revision 79 and 9,932 knobs, with 5.92 s of growth over the full 10.22 s snapshot interval. The leader reported one maximum outstanding storage operation. The 5.52 s in-seed observation is a lower bound; the 5.92 s observation includes work after the seed. This is a single run: no cross-run spread or performance improvement is claimed.

## Seed-boundary follow-up

The follow-up 10K seed applied 80/80 transactions and passed the complete-state oracle in 10.363 s. The harness records `time.monotonic_ns()` immediately before and after `prepare`; the leader health snapshots carry ages of 54 and 157 ms at their respective fetches. Those ages place the first snapshot about 65 ms before seed start and the last about 153 ms before seed end. During those snapshots, revision moved 0 to 79 and knobs 0 to 9,932. Seventy journal cycles completed: 140 file sync completion intervals summed 4.245 s and 70 directory sync intervals 1.719 s, for 5.963 s in all. The whole storage driver (`io_original_drive_ns`) summed 7.254 s across 1,385 operations. These are serial occupancy intervals on one leader; the last snapshot misses the final seed receipt. Raw evidence is `.scratch/seed-waits/.scratch/probe-sync-10000/events.jsonl` in the main checkout.

The same snapshot delta shows `publisher_admitted_to_ready_ns` at 83 completions totaling 39.50 s; the four producers' 80 client transactions total 38.68 s, or 483 ms per transaction. Those histograms cover concurrent requests and different counts, so subtraction does not isolate HTTP or replication. The HTTP worker activity records 98 completions, zero failures, a peak of four active workers, and 71 submissions queued at most about 2.1 ms for a thread. The harness opens a new TLS connection and sends `Connection: close` for every request (`authority.py`); connection setup and JSON work remain possible residuals specific to this harness.

An attempted `DYLD_INSERT_LIBRARIES` syscall shim did not record any calls in a standalone `fsync` smoke test, despite dyld reporting an interposed bind. The binary imports `_fcntl` and `_fsync`. No syscall timing from that shim is used. A successful syscall timer inside the completion fallback worker would be needed to divide the 5.963 s completion occupancy into worker/syscall and notification/reaping intervals.

## Conclusion and next experiment

Serial durable storage completion is the largest measured occupied interval in the seed. The path does two file syncs and one directory sync per journal append, and the 100K run observes roughly 90 ms from submission through reaping for these operations per publication. The evidence does not establish how much of that interval is inside the OS sync syscall. It does not assign the remaining wall time to HTTP, proposal admission, replication, or application; append response and publisher histograms overlap storage wait and cannot be added to it.

The smallest follow-up is a 10K three-voter seed with a diagnostic switch that times the actual file and directory sync calls in the fallback worker, plus request phase timestamps at client send, server admission, proposal acceptance, commit, and reply. Preserve bytes and durability. There is no code optimization or format change to finish from this explorer result alone.
