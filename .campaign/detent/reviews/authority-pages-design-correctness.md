# authority-pages-design review: correctness

Commit: Detent:28ca4c31de0c
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The harness measures what the seal, lag and window rows say, and every number I re-derived from
the job logs matches the doc. Two things do not hold: check.py's "95% CI" is a z interval over
4 pairs, so every interval and the A/A band in the doc are about 1.6 times too narrow (C1); and
the Recommendation and Decision attribute the paced pool arm's commit result to the recommended
pool, which never ran beside the journal (C2). Both are fixed from the existing logs and in text;
no conclusion moves.

## Evidence I relied on

- `git diff $(merge-base main 28ca4c31) 28ca4c31`: seven files, all under `bench/authority_pages/`,
  `docs/design/authority-pages.md` and the journal. Nothing under `src/`, `include/`, `tests/`,
  `test/`, the top-level `CMakeLists.txt` or `.github`; the quest added no tests. `bench/` is not
  wired into the top-level build (`CMakeLists.txt:132-133` adds `benchmarks/` only), and the
  harness's `CMakeLists.txt` forces its cache options inside its own build tree. Q3: no shipped
  behavior changes.
- Suite `261124-065709-635779` (passed at `3fc1c49d`, no src change since on this branch).
- The cited measurement jobs' logs under `~/.scratch/campaign/jobs/<id>/a1/log`, read directly.

## What I checked myself

Scripts under `/Users/kronuz/code/Kronuz/Detent/.scratch/detent/review-authority-pages-design-correctness/`:
`check_at_head.py` (check.py at `28ca4c31`) and `tci.py` (check.py's `deltas_of` with Student's t
beside its z interval): `python3 -I tci.py check_at_head.py <paced logs>`. No jobs submitted: every
probe reads existing job logs.

1. **Q1, harness vs option 2.** The `lag` mode (`authority_pages_bench.cc:835-1044`) is option 2's
   shape: THREADS workers over a shared round-robin cursor, at most one pass per namespace per
   INTERVAL (`:912`, measured from the pass start), each pass swapping out all pending changes
   (coalescing, `:943-950`), root lag from arrival to the pass's end (`:957-959`); cold start
   cold-builds while arrivals continue. Locking is sound (pool lock then space lock; the arrival
   thread takes only the space lock). The `paced` mode (`:748-827`) does not: its `pool` arm is
   one build per seal on ten fresh threads (`Corpus::seal`, `:256-265`), not four threads with
   250 ms passes (C2). Paced pairing (`check.py` `deltas_of`) subtracts the mean of the adjacent
   none blocks, and `check_paced` verifies the rotation; both are correct. The interval is not (C1).
   Nothing in the harness alters the commit path: the builder runs on its own threads, `paced` sets
   no `after_service`, and each builder block drains before the next none block starts (`:798`).
2. **Q2, fails-without pair.** `261123-102546-6e81cf` (at `ff2d21ab`) exits 1 with
   `transaction 52 rejected: status 2 (key 53748, value 53748, held 53748)`; `Status` 2 is
   `NoChange` (`src/detent_authority/state.hh:73-75`). The old value `salt * 64 + j` equals the
   seeded value of key 53748 at salt 839, j 52 (839 × 64 + 52 = 53748). `ff2d21ab..300f296b`
   changes only that line in the harness (values start at `values_.size()`), and
   `261123-102550-885bdd` runs the same seeded sequence to exit 0. It fails for the reason claimed.
3. **Q4, numbers vs logs** (more than five): `1ed7a8` (604.8 ms p50, 507.1-1,552.4; wall 1,024.3,
   sync 427.8; 11.21 / 19.03 MB; 20 relocations of 300 builds; (d) 96.7 ms, 85.4-141.8; 135.1 /
   243.1 MB heap, 618.8 peak; cold 9.00 s CPU, 12.04 s wall); `976f07` (every 100K cell);
   `519b4d` (every cell of the exclusive paced row, the 0.53-0.58 / 0.72-0.76 / 0.58-0.62 core
   shares, 133.4 ms none); `a9b30c`, `d6ce25`, `962367` (every lag row, including the burst's worst
   second p99 670 ms); `e60802`, `1675da` (every window row); the four 5M commit jobs' inline stalls
   (971, 1,104, 1,507, 2,650 ms) and p99s. All match.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | yes | defect | check.py's paced interval is labelled 95% but uses z = 1.96 over n = 4 pairs (`check.py` `stats`: `half = 1.96 * stdev / sqrt(n)`); with 3 degrees of freedom the 95% quantile is 3.182, so each interval covers about 85% and is 1.62 times too narrow. Every "95% CI" and the "A/A band of about ±1 ms" in the doc (lines 18, 85-90, 95, 166, 174, 211, 216) inherit it. Recomputed from the same logs: exclusive `519b4d` pool [-1.8, 1.3] in an A/A band of [-1.7, 1.3] (doc: [-1.2, 0.7] in [-1.1, 0.7]); thread [-3.3, -0.6]; write [33.3, 40.8]. Strict `65f196` write [-36.4, 185.7] (doc [6.2, 143.0]) and 100K `95a0ae` aa [-204.4, 42.3] (doc [-157.0, -5.1], "excludes zero") no longer resolve as stated. The conclusions stand: pool inside the band, write clear of it. | `tci.py` over `261123-193517-519b4d`, `-193507-f0eafa`, `-193510-65f196`, `-193513-95a0ae` | Use Student's t with n-1 degrees of freedom in `stats` (a small table for n ≤ 30 suffices) and regenerate the doc's intervals from the same logs; no rerun. Test: `check.py pairs` on the `519b4d` log prints pool `ci95` [-1.8, 1.3] and aa [-1.7, 1.3], which fails on the z interval. |
| C2 | yes | gap | The recommended pool's effect on group commit is stated as measured but was not. The paced `pool` arm builds once per seal on ten threads at 0.72-0.76 of a core (`authority_pages_bench.cc:780-781`, `:256-265`); the recommended shape is four threads with coalescing passes every 250 ms at 1.34 cores steady and 1.75 in the burst, and ran only in the `lag` rows, which have no journal. Limits 7 says so, yet the Abstract (line 18, "a builder beside the journal leaves group commit p50 inside ..."), the Recommendation (line 166, "leaves group commit p50 where it was: the pool's paired delta is -0.2 ms") and Decision option 2 (line 216, "no commit latency outside the A/A band") put the arm's number on the recommended shape. The only evidence at higher builder load is the reviewer's strict saturating probe `24d1dd`, where p50 moved -5 to +21 ms against adjacent none blocks, outside any ±1.5 ms band. The owner decided D156 on option 2's line. | doc lines 18, 166, 205, 211, 216; `261123-193517-519b4d`; `261123-145405-24d1dd` | Either (1) add a paced arm that runs the `lag` mode's pool (four threads, 250 ms passes over rc3-rate arrivals) beside the journal with the A/A arm, one exclusive job, and cite its delta; or (2) reword lines 18, 166 and 216 to name what ran (one build per seal on ten threads, 0.72-0.76 of a core) and give `24d1dd`'s -5 to +21 ms as the only bound at the recommended load, leaving the measurement to authority-builder-pool. (2) is a text fix confirmed by the suite; (1) by its job. |
| C3 | no | defect | `build_lag_ms_max` is the longest per-seal build wall, not the lag behind a seal: `Builder::run` stamps `posted` when it picks a seal up (`authority_pages_bench.cc:637`), not at `post()`, so time queued behind earlier seals is not counted. The doc's "builder's worst lag behind a seal" row (line 93) and "its builder trails by up to 1.9 s against 0.8 s in memory" (line 174) read it as lag. In `519b4d` the write arm's builder still had 3,168-3,463 ms of work when each block ended (`drain_ms`), because one seal with writes takes 1.7-1.9 s of wall against rc3's 1.02 s interval: it does not keep up, and its backlog grows for the whole block. Likewise "the clock held the builder at 0.91 seals/s" is the posting rate; the write builder completed about 0.6 seals/s. This strengthens the ruling against persisted pages; the number is wrong. | `519b4d` blocks (lagmax 1,706-1,905, drain 3,168-3,463 for write; thread drain 293-648) | Stamp each post time in a queue and measure from it, or relabel the row "longest build per seal" and state the write arm's backlog from `drain_ms`. |
| C4 | no | gap | The detection delay "at most the root lag" (line 135) omits the root entry's own commit: under option 2 a root reaches followers and relays only once its log entry commits, one group commit after the pass (none p50 133 ms, p99 309 ms in `519b4d`), and the change itself commits before the builder sees it. The lag rows start at arrival at the builder and end at the pass. | `authority_pages_bench.cc:957-959`; doc line 135, Limits 6 | One line: the delay adds a group commit for the root entry. |
| C5 | no | nit | The lag rows' "Builder CPU" is the process's CPU over the run (`cores`, `:1039`), arrival thread included; pool-only CPU is 1.30 cores for the recommended row (doc 1.34) and 0.1-0.3 less in the cold rows. Conservative. | `a9b30c`, `e60802`, `1675da` summaries (`build_cpu_ms / total_ms`) | Report `build_cpu_ms / total_ms`, or say "process CPU". |
| C6 | no | nit | `digest_ms` is wall time (`:242-249`, `since_ms`), labelled "(d) records digest CPU" (line 46). Single-threaded, so close under exclusive. | file:line | Relabel or time it with `thread_cpu_ms`. |
| C7 | no | nit | `commit` mode resets `worst_gap_ms` without `last_` (`:707`), so an arm's first gap spans the pause after the previous arm; `paced` uses `reset_gap()`. Every 5M commit job had `drain_ms` 0, so the cited inline stalls stand. | `afdabb`, `d23ae1`, `7ae2b2`, `0c4d8c` | Call `reset_gap()` there too. |
| C8 | no | nit | `check.py paced RATE` echoes the rate and never checks it against `seals / run_ms`, so a builder arm posting far below the asked rate passes. | `check.py` `check_paced` | Fail when the posting rate falls outside a band around RATE. |

## Not run

- No jobs: every probe reads the logs of jobs already run. No measurement of the recommended pool
  beside the journal (C2), since this review runs no strict or exclusive work.
- The eight reruns at the final head (`261124-104556-e167d6` to `261124-104600-0c7e60`) were queued
  or running; I did not compare them with the doc, so A1 has no evidence at this commit yet.
- The reviewer's probes `63b0f2` and `24d1dd` are the design reviewer's harness; I did not re-read
  their logs beyond the numbers C2 quotes from the design review.
- Not a session of my own titled `detent · reviewer authority-pages-design correctness`: I ran as a
  subagent of the lead.
