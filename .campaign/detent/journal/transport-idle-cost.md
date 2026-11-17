# transport-idle-cost: journal

State: ready for review
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: every row has evidence at consensus:0d92ef5; the doc is consensus:e0ae1b9 (review fixes).

## Next

1. Review; the lead merges main (peers-retry-flake) and lands.

## Blocked on

Nothing. A2's host: this Mac stands in for the Linux gate host (D111), stated in the doc.

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
- F3 follow-up: the wheel cut is by usable size; a transport block >= 40,640 B would vanish.
  transport-identity's per-connection `opened` vector crosses it near 2,540 groups per connection.
  Fix when it matters: set apart by requested size in [sizeof(timer_wheel), +1 KiB), or assert
  large blocks == loops with a wheel.
- F4 follow-up: A1/A2 are at consensus:0d92ef5, before transport-identity (in review, not landed
  at this writing). Once it lands: merge main, rerun 45a7bf and the three hop rows at the merged
  head, update the doc or record the miss.

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
