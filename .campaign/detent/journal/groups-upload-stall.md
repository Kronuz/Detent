# groups-upload-stall: journal

State: waiting on jobs 261118-182248-eb67f9 261118-182249-1c6fef 261118-182251-4f4a38 261118-182252-0ca070 261118-182254-e926dc 261118-182255-c111a2 261118-182256-aad2e3 261118-182311-d7590a 261118-182304-992a7b, CI on consensus:c782db1
Quest: .campaign/detent/quests/groups-upload-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A4 done at consensus:c5bde2f (CI 44398532950 green). A2 at consensus:c782db1: rate-scaled bound plus the on-failure summary line. All jobs moved once to c782db1 (the lead's ruling 2 of the second message): A2 x50 and TSan, fails-without old-bound and bulk-ahead-of-control, A4 x50 and TSan, the suite, and the sampling cost A/B (strict, 20 runs each).

## Next

1. Read the jobs and CI on c782db1 (rerun a connection_test TSan abort if it shows); fill A2-A4 rows and the cost table; mark ready for review.
2. The lead files the follow-up quest for the open class (Result), blocked on the first sampled CI failure.

## Blocked on

Nothing.

## Notes

- A4, the race (consensus main 42019eb, gcc run 44357306609): `feed` repays owed records only
  when the connection's queue is empty, and puts one 4 MiB bulk frame per feed, so host 2's queue
  holds at most one bulk frame. Node 1 "read slowly" (150 ms after each bulk frame) but read each
  frame at full speed in between, into kernel buffers that hold about a frame (Linux tcp_wmem and
  tcp_rmem maxima 4 and 6 MiB): host 2's queue emptied between frames, each drain's feed repaid
  every owed record (up to 64 KiB, about 3,855 records), and owed never reached 4,096 at an
  `owe`. 18,000 records, 18,000 credit frames. Reading 2 of 6 frames in that run means the
  reads, not the merge, set the outcome.
- A4, the fix (consensus:cad35e4, test only): `Gate`, a TCP relay on its own threads between node
  1 and host 2, passes node 1's bytes and, while held, reads nothing of host 2's (its upstream
  socket asks a 64 KiB receive buffer). Held before the bulk: host 2's queue empties at most once
  after its first bulk frame is put (two frames, 8 MiB, outsize host 2's send buffer plus 64 KiB),
  so at most one feed repays early (at most 3,855 records) and the other 14,000+ are owed with the
  queue full: `owe` merges at 4,096 and 8,192 records, duplicates certain past 6,000. The case
  waits for host 2's first bulk put (`stats.sent`), then sends the 18,000 data frames; opens the
  gate once all are delivered. Asserts node 1 read no bulk frame before (the gate held). Both ends'
  dead_after is 60 s, since a held gate silences host 2. RawPeer's `slow` mode is gone (backlog
  was its only user). Mutants: coalesce-drops-credit fc2c3dc (mutant_commit.py), no-merge 41e8fd1
  (owe returns before merging; made with git plumbing, on no branch).
- No macos-x86_64 `upload` failure carries samples yet: every run at or after the sampler passed
  `upload`. Failures without samples (CI logs in .scratch/ci/ of the code worktree): main
  44130104547 (0.0 MiB, 3 beats, gap 4044 ms), 44214025598 (6.0 MiB, gap 3748), 44216299768
  (18.0 MiB, gap 3012); consensus-close-fenced 44261476872 job 134655655042 (07:51Z, gap ~4 s) and
  44264616865 job 134665992662 (08:16Z, 2.0 MiB, gap 4019 ms, host 1's loops join 1.5 s after
  draining). Host stalls 83-111 ms, under the 250 ms retry bar.
- Probe: branch probe/groups-upload-stall (consensus:0156b46 = 5923b6c with ci.yml replaced; never
  lands; dispatch only). Three macos-15-intel shards, each 24 `upload` runs beside a looping
  connection_test, printing `netstat -s -p tcp` deltas and load around each failure. Script
  .scratch/probe/ci.yml in the code worktree. Dispatch 1: 44319694813, 72 of 72 passed (14:02-14:12Z).
  Main at 5923b6c (44318999208) passed too. Dispatch 2: 44324074784 (14:25Z).
- msg (lead, after dispatch 1): could transport-feed-scan itself have removed the stall? No:
  1. Credit already went first before it: 3ba44aa's feed() sends `lane.grants` before control and
     data, as 5923b6c's repay() does; both wait only for the connection's queue to empty.
  2. Control frames stalled with the bulk (3-5 beats in 4 s), and control needs no credit: the whole
     connection stopped, which no credit or scan order explains. Six groups make the scan cost moot.
  3. The tabulation (every macos-x86_64 job since 11-17, logs in .scratch/ci/all/, table.txt):
     ring-feed code failed too, 4c7666a (05:13Z, run 44242758901) and 5eacc91 (05:41Z,
     44245984014), both after 02800e5 and 01e2c06. Pre-feed-scan code passed 12 runs after its last
     failure (c5d15d9, 08:09Z): c20f3fd through abc01bb, 08:26-13:53Z. The 7 failures fall in
     16:49Z-08:09Z by time, not by code version.
  So no probe at 13b32d3 or 303cad1 would fail on the feed: dispatched probe 2 instead.
- Probe 2 (44324074784): 72 of 72 passed. Since 08:09Z: 144 of 144 probe uploads, plus every CI run.
- Time clustering (toward A2's runner branch), macos-x86_64 `upload` per CI job since 11-17
  (.scratch/ci/all/table.txt in the code worktree; feed = whether the commit has 02800e5):

  | Window (UTC) | Failed | Passed | Feed of the failures |
  |---|---|---|---|
  | 11-17 13:06-16:48 (test exists from 13:06) | 0 | 4 | - |
  | 11-17 16:49 - 11-18 08:09 | 7 (44130104547, 44214025598, 44216299768, 44242758901, 44245984014, 44261476872, 44264616865) | 28 | 5 scan, 2 rings |
  | 11-18 08:10-14:25 | 0 | 18 CI jobs + 144 probe uploads | - |

- Probe 3 (logs in ~/.scratch/groups-upload-stall/probe3/): runs 44359434896 (17:30Z), 44396894864
  (21:00Z), 44441165298 (02:00Z), each 3 shards x 24 `upload`. Not all passed: 44441165298 shard
  probe-3 iteration 17 (02:10:24Z) FAILED with samples: 32.0 MiB in 4013 ms, 55 beats, longest gap
  1092 ms, host stall 82 ms. Other 215 iterations passed. All 216: reader rate 5.9-26.0 MiB/s
  (median 17.5; nominal 38.1), gap 208-1092 ms (median 374, p95 537).
  | Run | Shard | Failed | MiB per 4 s (min/med/max) | Longest gap max |
  |---|---|---|---|---|
  | 44359434896 | 1/2/3 | 0/0/0 | 62-80 / 62-104 / 62-96 | 517 / 506 / 560 |
  | 44396894864 | 1/2/3 | 0/0/0 | 54-76 / 54-78 / 58-90 | 566 / 498 / 489 |
  | 44441165298 | 1/2/3 | 0/0/1 | 60-98 / 60-88 / 24-72 | 428 / 603 / 1092 |
  The failure's samples (250 ms): uploaded stuck at 22.0 MiB from 2432 to 3027 ms (reads 24 flat
  2180-3027 ms) while the receiving socket held 1.1-1.6 MiB unread with its window closed (rcvwnd
  about 8 KB), and the sender's socket kept 466 KB queued (sndwnd about 8 KB, rexmit 1-3, cwnd 4 MB).
  So the data path is full and host 2's reader loop is the slow party; no loss. That run's shard
  was a loaded runner: load averages 8.8/20.2/15.8, 10 `Abort trap` of the companion
  connection_test loop (none in the other 5 shards), iterations 17-24 at 24-66 MiB against 50-72
  before.
  Model: a control frame waits behind at most one window (8 MiB) of data, so gap ~ window / rate.
  Over the 216 runs gap/(window/rate) is 0.34-1.44 (median 0.80; corr of gap with 1/rate 0.67);
  the failure's is 1092 / (8 MiB / 8.0 MiB/s = 1000 ms) = 1.09. The failing run is the slowest
  reader of the 216 bar one. Probe 3's samples do not show a 3-4 s stall.
- Every recorded macos-x86_64 CI failure against the bound (the lead's ruling 2; window 8 MiB since
  ca70f3d; window/rate = 8 MiB x elapsed / uploaded; bound = max(1,000, 1.6 x that) ms, and a run
  under 16 MiB is "dead"; all seven have the same host stall 78-111 ms):

  | CI job (run) | MiB | beats | elapsed ms | window/rate ms | gap ms | gap / (window/rate) | bound | in the bound |
  |---|---|---|---|---|---|---|---|---|
  | 134258719921 (44130104547) | 0.0 | 3 | 4043 | no rate | 4044 | - | dead | no |
  | 134499287837 (44214025598) | 6.0 | 5 | 4125 | 5500 | 3748 | 0.68 | dead | no |
  | 134506934077 (44216299768) | 18.0 | 15 | 4008 | 1781 | 3012 | 1.69 | 2850 | no |
  | 134594372970 (44242758901) | 30.0 | 53 | 4007 | 1069 | 2442 | 2.29 | 1710 | no |
  | 134604960913 (44245984014) | 6.0 | 4 | 4134 | 5512 | 3776 | 0.69 | dead | no |
  | 134655655042 (44261476872) | 4.0 | 5 | 4012 | 8024 | 3676 | 0.46 | dead | no |
  | 134665992662 (44264616865) | 2.0 | 3 | 4020 | 16080 | 4019 | 0.25 | dead | no |

  None of the seven is inside the bound: the bound explains the sampled stall (probe 3, ratio 1.09)
  and not these. The factor stays 1.6 (ruling 2). The four under 16 MiB are a connection that
  delivered almost nothing (their rate-model ratio is 0.25-0.69, so only the floor rejects them);
  the 18 MiB and 30 MiB runs are over the model (1.69, 2.29) with data still flowing: 30 MiB with
  53 beats had one 2.4 s hole. Their cause is open (Result). These runs predate the socket
  sampler (transport-feed-scan), so no sockets exist for them.
- Sampling in the real test: `upload` in groups_test.cc already carries the sampler since
  transport-feed-scan (a thread, every 250 ms: progress, links, host 1's frames, host 2's reads, and
  per socket unread, sndq, windows, cwnd, rto, rexmit; origin/main has it) and prints only on
  failure, after `timed`'s "FAILED (worst stall N ms)". consensus:c782db1 adds one line before the
  samples: window, reader rate, window/rate, bound, gap, controls. The probe branch ran this same
  test, so main's CI captures the same samples.
  Cost when the test passes: A/B of 20 runs each, strict, the with-sampling binary
  (261118-182311-d7590a) against a mutant whose sampler loop never runs (68564a7, 261118-182304-992a7b);
  the table goes here when they finish. The sampler sleeps 250 ms between samples and does ~1,000
  failed `getsockname` calls per sample on a thread of its own, about 16 samples a case.
- A2 fix (consensus:9b731cb, test only): `upload_gap_ok`: gap < max(1000 ms, 1.6 x window x
  elapsed / uploaded) and uploaded >= 2 windows; the recorded runs asserted as values at the top of
  `upload` (1092 ms/32 MiB and 816 ms/24 MiB pass; 3012 ms/18 MiB, 4019 ms/2 MiB and 1000 ms at a
  healthy 20 MiB/s fail). 1.6 sits between the 1.44 of the 216 runs and the 1.69 of the nearest CI
  failure. Mutants (made with git plumbing, on no branch): the fixed 1000 ms rule 77c1ebb, control
  held behind data (`bulk-ahead-of-control`) 647f5d5.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] 44441165298 probe-3 iteration 17 names the cause of the sampled 1.09 s stall (Notes, Probe 3); the seven recorded CI failures all lie outside the bound (Notes, table), so the 3-4 s class is open (Result) |
| A2 | <pending at c782db1: x50 261118-182248-eb67f9; TSan 261118-182249-1c6fef; fails-without: old-bound 261118-182251-4f4a38, bulk-ahead-of-control (prints the summary and samples) 261118-182252-0ca070; cost A/B 261118-182311-d7590a vs 261118-182304-992a7b> |
| A3 | <pending at c782db1: suite 261118-182256-aad2e3; CI> |
| A4 | x50 261118-115335-bbf421, TSan 261118-115336-3f84ef (at 95bca86; rerun at c782db1: 261118-182254-e926dc, 261118-182255-c111a2), CI 44398532950 green at c5bde2f; fails-without at mutants of cad35e4 (same code): coalesce-drops-credit 261118-114620-92a85d, no-merge 261118-114638-5f511f |

## Result

<pending jobs and CI> Decided so far (lead's ruling: land A4 and the bound; the bound stays strict): the `upload` bound scales with the reader's measured rate
(consensus:9b731cb, 545b53d; docs/mechanisms/host-transport.md `upload` row). It covers the sampled
class: a stall of window / rate on a loaded runner (probe 44441165298).

Open, waits for the first sampled CI failure: the 3-4 s class (the lead files the follow-up quest, blocked on it). Seven macos-x86_64 CI failures (table in Notes) stay outside the bound:
connections that delivered 0-6 MiB in 4 s, and 18 MiB at 3,012 ms (1.69 of window/rate) and 30 MiB
at 2,442 ms (2.29) with data flowing. They fall in 11-17 16:49Z to 11-18 08:09Z; none since in 18 CI
jobs and 216 probe runs. An 18 MiB case at 3 s may be another cause; no socket samples exist for
any of them. The test fails them (it does not hide them); the next one prints the summary line and samples, which name it.

## Rulings I made

- A4: a gate (relay) in the test rather than a product hook: the file set is the test, and holding
  host 2's bytes unread is what "host 2 cannot send" means. The merge claim stays in `backlog`,
  now deterministic, rather than moving to a new case.

- Probe shape: one dispatch = 3 runner jobs x 24 uploads, so "about 6 runs" is two dispatches.
- A2: the runner branch with a rate-scaled bound, not a fixed one: the gap is window/rate and the
  rate varies 4x within one job, so a fixed 1,000 ms flakes at a reader under 8 MiB/s. A reader
  under 2 windows per run still fails. Factor 1.6 is a judgment between the two data sets.
- A2: the factor stays 1.6 although 44216299768 (1.69) and 44242758901 (2.29) lie outside it (lead's ruling 2).
- The lead's message said all three probes passed; run 44441165298 shard 3 did not (above).
