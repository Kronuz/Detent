# transport-idle-cost: journal

State: waiting on jobs 261117-122812-45a7bf 261117-122822-488d1f 261117-122836-fb319f 261117-122848-92c892 261117-122904-8a23d2
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged origin/main (transport-groups-ci, consensus:0ed76ef) at consensus:dc1e593. At
consensus:0d92ef5: the bench judges each host against D108's `52 KB + 2P x 44 KB`, sets apart
timer wheels as blocks >= sizeof(wheel::timer_wheel) (40,640 B), and the lone-frame hop arm takes
frames per run. consensus:4c80573 writes docs/perf/transport-idle-cost.md (A1 and A2 tables).

Old A1 job 261117-092707-067ee0 (exclusive, 2c8e710) failed only on the old formula; against
D108 its hosts are 1.4-1.5% under at L = 1 and 5.8-6.9% over at L = 4 (within 10%); slope 44,454
B (L = 1), 48,394 B (L = 4) <= 49,152. Local cost --quick at 0d92ef5: ok, same fits.

CI run 44139926700 (at 0264544): Linux gcc/clang/arm64, TSan, ASan, FreeBSD passed the idle ctest;
macos-x86_64 failed it: the older macOS allocator gives wheels 40,960/45,056 B, under the old
64 KiB cut, so they rode the L = 4 slope (56,317 B). Fixed in 0d92ef5; needs CI after a push.
macos-arm64 never got a runner.

A2 from 261117-092717-30ce96 (exclusive, 7 rounds, 2c8e710): control +139 ns/frame [95, 174],
band 73, clears; data +208 [182, 221], band 61, clears; idle wake +11.8 us [1.8, 19.3], band
15.3, within the band. Rows rerun at 0d92ef5, one exclusive job each: 488d1f (control), fb319f
(data), 92c892 (wake, 8,000 frames per run, 9 rounds). Feed profile 13c63f: `feed` is 11,690 of
~14k samples at 10,000 groups (transport-feed-scan's, D109).

## Next

1. On resume: read the five jobs; put their numbers in docs/perf/transport-idle-cost.md's A1 and
   A2 sections (replace 067ee0/30ce96 numbers with the 0d92ef5 jobs'); commit; paste `campaign
   brief`'s Evidence table; State "ready for review" if 92c892's wake row has an answer (clears,
   or within the band at 8,000 frames per run, stated so).
2. The lead: push for CI at the current commit (macos-x86_64 is the check of the wheel cut).

## Blocked on

1. Lead: CI at the current commit needs the branch pushed; I do not push.
2. A2 names the Linux gate host; none was used. Measured on this Mac by the lead's instruction,
   stated in the doc.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

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
