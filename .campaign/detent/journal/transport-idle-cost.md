# transport-idle-cost: journal

State: waiting on jobs 261117-171843-129613 261117-171843-41bde3 261117-171844-6513c0 261117-171844-b0ba7a 261117-171845-4f0a0a 261117-171845-d67111
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

transport-identity (consensus:361d5d2) and main are merged (consensus:005508c, the lead's). Every
job at 937441b aborted: `member_of is required`. consensus:73cd955: the bench names incarnation k
on node k, stores every group (its own open) on every host inside the per-group window, and sets
wheels apart by requested size and address (F3). Reran the six at 73cd955: A1 129613; A2 rows
41bde3 (control), 6513c0 (data), b0ba7a (wake), 4f0a0a (full hop); suite d67111.

Local smoke at 73cd955 (light hold, not evidence): A1's per idle group now 253-368 B, over 64 B
at every configuration (stored 206 B, then its frames 162 B at H = 5, L = 4); per end 44,667 B
(L = 1), 48,792 B (L = 4) <= 49,152; per host 99-100% (L = 1), 107-108% (L = 4) of D108, within
10%. So A1 129613 and the suite (ctest `consensus_transport_idle`) are expected to fail on the
64 B per group. Blocked on 1.

## Next

1. On resume: read the six jobs; put their numbers in docs/perf/transport-idle-cost.md (A1, A2,
   the per-group miss); paste `campaign brief`'s Evidence; State "ready for review" once the
   lead rules on Blocked 1.

## Blocked on

1. Lead: A1's "per idle group at most 64 bytes" fails after transport-identity. A stored group
   holds, per host (local, 1,000 groups, C++ block sizes): its `Stored` node 80 B, its
   `Opening` (make_shared, GroupOpen 80 B + serial) 112 B, `Stored::opened` (24 B per peer
   connection that opened it, capacity-rounded: 32/48/96 B at P = 1/2/4), plus 16 B per sending
   connection in `Lane::opened` and map buckets: 253 B (P = 1), 286 B (P = 2), 366 B (P = 4).
   The traffic itself leaves nothing (0.2 B before identity). Fix is in host_transport.h,
   outside my file set. Options: (a) amend A1 to count a stored group's identity state, about
   200 B + 40P B per group (D-row; what identity's design pays); (b) shrink it in a transport
   quest: GroupOpen inline in `Stored` instead of a shared `Opening` (-~32 B), `Stored::opened`
   as an inline bitmask of (peer, lane) slots instead of a heap vector (-24..96 B), which might
   reach ~100-130 B, still over 64; (c) keep 64 B for what traffic leaves and budget the store
   apart. Recommend (a) now, with (b) as a follow-up quest: no reading of D10's 64 B made room
   for R7's per-group open state, and 64 B is not reachable without dropping it. Not loosened in
   the bench: the check still fails.
2. A2's host: this Mac stands in for the Linux gate host (D111), stated in the doc.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261117-122812-45a7bf (passed at 0d92ef5c447a; ignores docs-only docs/perf/transport-idle-cost.md), 261117-122904-8a23d2 (passed at 0d92ef5c447a; ignores docs-only docs/perf/transport-idle-cost.md) |
| A2 | 261117-122822-488d1f (passed at 0d92ef5c447a; ignores docs-only docs/perf/transport-idle-cost.md), 261117-122836-fb319f (passed at 0d92ef5c447a; ignores docs-only docs/perf/transport-idle-cost.md), 261117-122848-92c892 (passed at 0d92ef5c447a; ignores docs-only docs/perf/transport-idle-cost.md); results doc docs/perf/transport-idle-cost.md (consensus:cbcd563) |

CI run 44169921897 (consensus:4c80573): `consensus_transport_idle` passes on every platform,
macos-x86_64 included (the wheel cut); the run's one failure is `consensus_transport_peers` on
macos-x86_64, the known retry flake (peers-retry-flake).

## Result

bench/transport_idle.cc counts the host transport's idle bytes exactly and times the hop;
docs/perf/transport-idle-cost.md has the results (this Mac, OpenSSL 4.1.0, exclusive jobs).
- A1 (45a7bf): idle group <= 0.2 B, 0 fds/timers; idle end 44,454 B (L = 1) / 48,394 B (L = 4)
  <= 48 KB; per host 1.4-1.5% under `52 KB + 2P x 44 KB` at L = 1, 5.8-6.9% over at L = 4.
- A2: hop adds 131 ns/frame control (band 77), 216 ns data (band 70), 15.3 us per idle wake
  [12.9, 20.8] (band 8.7); all clear their bands.
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
