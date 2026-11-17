# transport-idle-cost: journal

State: waiting on jobs 261117-092707-067ee0 261117-092717-30ce96 261117-092732-0475d9 261117-092749-09ccf7 261117-093437-13c63f
Quest: .campaign/detent/quests/transport-idle-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on the exclusive jobs (queued behind light holds), at consensus:2c8e710:
261117-092707-067ee0 (A1, `cost`), 261117-092717-30ce96 (A2, `hop 7`), 261117-092732-0475d9
(Detent `run.sh hop_cost`, beside A2), 261117-093437-13c63f (feed-scan profile),
261117-092749-09ccf7 (suite). consensus:0264544 starts docs/perf/transport-idle-cost.md (host,
method); its A1/A2 tables come from those jobs.

What is known, from exact counts (strict-hold smoke at 2c8e710, `cost --quick`, not evidence until
the jobs land; OpenSSL 4.1.0 on this Mac):

- Per idle group: 0.0-0.2 bytes (OpenSSL 0, C++ <= 0.2), 0 fds, 0 timers, every frame delivered,
  0 dropped, at H = 2, 3, 5 and L = 1, 4. Zone statistics' +25..234 B/group is not live memory:
  `heap` walks 9,745 nodes in all after 40,004 frames.
- Per idle connection end, fit over H = 2, 3, 5 at L = 1: 44,454 B (OpenSSL 40,831, ~= conn_cost's
  40,732; transport C++ 3,623: Connection 464 B, Link 176 B, coroutine frames, deadlines) <= 48 KB.
  L = 4 fit: 48,393 B (per-thread OpenSSL state of each loop that runs TLS rides on the slope).
- Per host: 52,274 + 2P x 44,454 at L = 1: 24.2% / 16.4% / 11.6% over `29 KB + 2P x 41 KB` at
  P = 1 / 2 / 4; L = 4: 33.4% / 26.3% / 21.0%. Over budget (Blocked on 2). Besides: one 64 KiB
  timer wheel per loop (task::asio_scheduler's `timer_loop`), the scheduler's.
- Idle mesh CPU: under 2 ms/s for 3 hosts (`ps` time, local).
- 5 hosts, 1 loop, 10,000 groups: connections end mid-run, 3 of 3 attempts (job
  261117-092742-231632, exit 1); 1,000 groups pass. Blocked on 1.
- Hop smoke (1 round, strict): stream control +156 ns/frame (A/A 5), data +201 (A/A 25); one
  frame per 1 ms: +26.6 us per wake (A/A 3.3-7.7k), receiver 83-85 us CPU per lone frame.

## Next

1. On resume: read the five jobs' logs; fill the A1 and A2 tables and "where the bytes go" in
   docs/perf/transport-idle-cost.md (job ids, commits, host); commit; `campaign verify --for A1`.
2. Paste `campaign brief`'s Evidence table; State "ready for review" or Blocked.

## Blocked on

1. Lead: `HostTransport::feed` is O(groups) per frame. `next()` scans `lane.groups` from the
   cursor for a ready group and `erase_if` walks every group per feed; groups waiting on credit
   stay in the map, so 10,000 groups per lane pegs the loop (local `sample`: 11,387 of ~15k
   samples in `feed`) past `dead_after`, and connections end (job 261117-092742-231632). Fix is in
   include/consensus/transport/host_transport.h, outside this file set (and transport-identity's
   area). Recommend: per-lane FIFO ready lists (control-ready; data-ready within credit), a group
   joining when it gains a frame or credit and leaving when it empties, so `next()` is O(1); erase
   a group where it empties, not by `erase_if` per feed. The bench's `one 5 1 10000` is the check.
2. Lead: the per-host budget is not met. Where the bytes go, at L = 1: OpenSSL's context 29,472 B
   plus ~17.8 KB OpenSSL builds on the first handshakes (the design's 29 KB measured the context
   before any handshake), transport C++ ~5.0 KB (Links, listener task, loops' tables), and per end
   44,454 B (2.5 KB over the formula's 41 KB: the transport's own C++). Options: (a) amend the
   formula to `52 KB + 2P x 44 KB`, the 64 KiB timer wheel per loop counted as the scheduler's
   (recommended: the gap is OpenSSL's and coroutine frames, fixed cost); (b) shrink the
   transport's per-end C++ (at most 3.6 KB/end; leaves the 23 KB intercept).
3. Lead: CI (the Linux counts) needs the branch pushed; I do not push.
4. A2 says "on the Linux gate host": none exists; measured on this Mac by the lead's instruction.

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
  not find. Blocks >= 64 KiB (the scheduler's timer wheels) are set apart from the transport.
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
