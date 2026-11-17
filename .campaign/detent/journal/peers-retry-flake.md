# peers-retry-flake: journal

State: waiting on jobs 261117-100413-28c01b (probe 2: drain time and lane state)
Quest: .campaign/detent/quests/peers-retry-flake.md (the lead's; never edit it)

## Doing

Probe 261117-085956-c4843d: 4 of 200 retries fail at once with not_connected (error 57), 0 ms. peers.h documents that fetch() "fails at once without a bulk lane to the peer", so the product behaves as documented. A lane is gone when the second fetch_deadline (await_reply, `late`) closes it, or the peer closed it. The test's `occupancy().fetches == 0` wait is satisfied by either the drain or that close, so it cannot tell them apart. Hypothesis: under load the 8 MiB late reply does not drain within the second deadline, the lane closes, and the retry finds no lane. Probe 2 (consensus:e1f2b0e) prints the drain time, connections, admitted and reconnects before the retry. Next: if confirmed, fix in the test (check that the lane survived the drain; give the drain a deadline sized for the reply), no Peers change. Then force it, and rerun A1-A3 with --for.

lead msg: A2 (5d9c03) and suite (2d5307) failed at peers_test.cc:1203 with the 2 s deadline, so the deadline is not the whole cause. consensus:78a7ab2 prints the retried fetch error, bytes and time; probe 261117-085956-c4843d runs fetch-failures x200 4-way and shows the log. Next: read its output, fix the cause in the test (or Blocked on a Peers defect), then resubmit A1-A3 with --for at the new head.

Fix is committed in consensus (branch quest/detent/peers-retry-flake): 228235f (knob), 0366b02 (fix). Jobs queued; waiting.

## Next

1. When the jobs finish, fill the Evidence rows; if all pass, set State to ready for review.
2. Lead: `campaign` finds no criteria in the quest file ("## Criteria" heading; it parses "## Acceptance criteria"), so `--for A1..A3` and `--fails-without` were refused. The jobs ran as plain quest jobs; rename the heading and rerun with `--for` (rerun the same ids) so they count.

## Blocked on

Quest file heading (see Next 2); the lead's file.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | mutant 261117-081315-99430b (consensus 228235f: 500 ms deadline kept, `PEERS_RETRY_LAG_MS=700`; wrapped in `! ...` because `--fails-without` needs `--for`; must exit 0 = the test failed). Fixed 261117-081320-25df88 (0366b02, same lag, must pass). |
| A2 | 261117-081325-5d9c03: `fetch-failures` x200, `xargs -P4`, at 0366b02. The frozen-fetch checks (timed_out, no bytes, waited >= deadline) and the same-lanes check are unchanged in the diff. |
| A3 | suite check 261117-081330-2d5307 |

## Result

Test-only, test/transport/peers_test.cc. The F3 scenario set `fetch_deadline = 500ms` so the frozen peer's fetch times out, and the same
deadline then bounded the whole retried 8 MiB reply. Now:
- 228235f adds `PEERS_RETRY_LAG_MS` (unset: no change): the retry's first read sleeps that long, which forces the cause.
- 0366b02 sets the deadline to 2 s (frozen-fetch wait 4 s, retry wait 5 s) and the checks use it: `waited >= 2000`.

## Rulings I made

- Deadline 2 s over a smaller reply: a smaller reply would not help a reader delay, and the deadline is fixed in `Peers` options at start, so changing it for the retry would restart Peers and break the same-lanes check. A 2 s deadline proves the same things: the fetch times out at its deadline holding its slot, the late reply drains over the same lanes once the peer resumes, the retry succeeds with identical bytes. Cost: 1.5 s more on this scenario (test timeout is 30 s).
- The forcing sleeps in the reader on the single pump thread, so it stalls the whole test, as a loaded runner does.
