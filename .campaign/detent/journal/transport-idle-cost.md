# transport-idle-cost: journal

State: ready for review
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

D112 ruled and amended to `230 + 40P` (Detent:a7a2d5c0). All six jobs passed at consensus:76bacd2;
docs/perf/transport-idle-cost.md has the new numbers (consensus:edbb162, docs only).

## Next

1. Review. CI at 76bacd2: macos-x86_64 failed `consensus_transport_groups_upload` (worst stall
   111 ms) and `consensus_transport_connection` (aborted), neither in this quest's files;
   `consensus_transport_idle` passed there (run 44216299768).

## Blocked on

None. (1, A1's per-group bound: ruled D112, amended to `230 + 40P`. 2, A2's host: this Mac stands
in for the Linux gate host (D111), stated in the doc.)

## Rulings I made

- D112 says `200 + 40P`; the smoke measures 253.1/285.5/366.3 B (L = 1) and 255.0/287.2/367.9 B
  (L = 4) at P = 1/2/4, 13/6/6-8 B over it (a stored group 204.8-206.3 B, its frames 48-162 B).
  The check is `230 + 40P`: the formula plus 30 B, 22-30 B over the measured points. Lead may
  tighten it (e.g. `210 + 40P + 8` fits P = 1) or leave it. Descriptors, armed timers and frames
  stay at 0.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261117-172945-6b1b06 (cost, exclusive), 261117-172947-eb211c (suite), at 76bacd23065c |
| A2 | 261117-172946-981ce0 (control), 261117-172946-bbf981 (data), 261117-172946-10ce1e (idle wake), 261117-172946-7024b4 (full hop), at 76bacd23065c; results doc docs/perf/transport-idle-cost.md (consensus:edbb162) |

CI run 44169921897 (consensus:4c80573): `consensus_transport_idle` passes on every platform,
macos-x86_64 included (the wheel cut); the run's one failure is `consensus_transport_peers` on
macos-x86_64, the known retry flake (peers-retry-flake).

## Result

bench/transport_idle.cc counts the host transport's idle bytes exactly and times the hop;
docs/perf/transport-idle-cost.md has the results (this Mac, OpenSSL 4.1.0, exclusive jobs).
- A1 (6b1b06, at 76bacd2): idle group 253-368 B (206 B identity store + ~40 B per peer + 8), 0
  fds/timers, <= `230 + 40P` (D112); idle end 44,666 B (L = 1) / 48,792 B (L = 4) <= 48 KB; per
  host 0.5-0.7% under `52 KB + 2P x 44 KB` at L = 1, 6.8-8.1% over at L = 4 (within 10%).
- A2 (981ce0, bbf981, 10ce1e): hop adds 153 ns/frame control (band 62), 222 ns data (band 66),
  12.5 us per idle wake [10.3, 16.2] (band 10.1); all clear their bands. The 7-round full-hop job
  7024b4 puts the wake at 14.4 us, within its 15.7 us band (2,000 frames per run), as before.
Leaves open: `feed` scanning every group (transport-feed-scan, D109).

## Review fixes

Review CHANGES (Detent:5b7ee8b1, correctness, at consensus:cbcd563); statements only, no code.
- F1: doc paragraph in "How it is counted": the per-host count is the steady state after the
  unprinted warm-up absorbs ~281,016 B per host of one-time OpenSSL state (probe
  261117-133940-27f2ae, D111). consensus:e0ae1b9.
- F2: doc says this Mac stands in for the Linux gate host (D111). consensus:e0ae1b9.
- F5, F6, F7 (nits, doc wording): the group check is an average and frames are checked by
  delivery; the L = 4 attribution is marked inferred; counts are exact on this Mac only (glibc's
  aligned_alloc path unverified, probe 4b45cc). consensus:e0ae1b9.
- F3: fixed at consensus:73cd955: a wheel is a block requested in [sizeof(timer_wheel),
  + sizeof(steady_timer) + 72) and remembered by address; any other large block counts. Locally
  `Lane::opened` (16,384 B at 1,000 groups) and map buckets now show in the per-group blocks.
- F4: identity landed; the six jobs rerun at consensus:73cd955 (ids in State). A1's per-group
  64 B is missed (Blocked on 1).

## Rulings I made

- Bytes are counted exactly (CRYPTO_set_mem_functions + operator new, usable sizes), not by zone
  statistics, which move tens of KB per host between identical runs and count blocks `heap` does
  not find. Blocks >= sizeof(wheel::timer_wheel) (the scheduler's timer wheels) are set apart from
  the transport; no transport block comes near 40 KB. No local test can fail without this: only
  the older macOS allocator (CI macos-x86_64) gives wheels under 64 KiB, so CI is its check.
- The idle-wake row runs 8,000 frames per run, 9 rounds: the A/A band (worst round) does not
  shrink with rounds, only with longer runs. Rule unchanged.
- The 10,000-group arm is not in A1 (D109: transport-feed-scan's A2).
- Per end is the slope of bytes per host against 2P (H = 2, 3, 5), so per-loop and first-handshake
  state lands in the intercept, not on the ends.
- "Within 10%" is checked as an upper bound (over by more than 10% fails).
- ctest (`cost --quick`) enforces the group counts and bytes and the per-end 48 KB; the per-host
  budget fails only the full `cost` run (the A1 job), so the bench can land while 2 is open.
- 1,000 groups per host: exact counts need no more, and 10,000 trips Blocked on 1.
- The mesh comes up one pair at a time: all hosts share 127.0.0.1, and the listener's 2
  handshakes per source address would evict concurrent dials.
- A configuration whose connection ends mid-run runs again, at most twice (as groups_test does).
- The fixture gains nodes four and five (test/transport/tls_certificates.cmake) for 5 hosts.
