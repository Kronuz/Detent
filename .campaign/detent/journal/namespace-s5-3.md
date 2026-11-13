# namespace-s5-3: journal

State: ready for review (jobs at 887a97a5 pending: 261113-050345-3cc679 261113-050349-8092b0 261113-050356-89edba 261113-050403-ee2963 261113-050411-f57264 261113-050418-a797ea)
Quest: .campaign/detent/quests/namespace-s5-3.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Durability review CHANGES at 33576164 (`.campaign/detent/reviews/namespace-s5-3-durability.md`;
its probes failed as expected: 261113-034724-30bd1b, 261113-040148-932628). Fixing:

- F1: `Fetch` carries a `Check` (digest, or the seal file's or table's own); `PeerHistory`
  passes over rotten bytes like none, starts each fetch at the next voter in rotation, and
  ends a fetch once every voter is passed over with their answers (`Answer::why`).
- F2: `history::file_bytes(limits)` (base of `state_bytes` + 247-byte header); the config
  loader sets `peers.fetch_bytes` to it; `Service` refuses peers below it; `io_error` and
  `message_size` answers are lasting, so a file no voter can serve ends the restore with a
  reported error instead of a 200 ms retry forever.
- Tests: transport (voter two serves every file rotten, asked first for some; a file voter
  one fails to read ends its fetch), power cut `rot` (rotten first peer, good second, for a
  seal file and the table), new `test_authority_history_large_base` (73,260,051-byte base
  installs through the configured lane; 64 MiB ends the install).
- Mutants (history_mutant.sh): first-bytes-win, same-order, retry-failed-forever,
  unchecked-fetch, old-fetch-bound, accept-low-fetch-bound.

Before the review (at 42dc0361): suite 261113-033810-0c0530, history tests
261113-033759-fee988, mutants 261113-032601-1a5622 and 261113-033814-67ea69, pins
261113-033825-96867a, all passed.

## Next

1. Read the jobs at 887a97a5; fix what fails; then the lead runs
   `campaign accept namespace-s5-3 durability --confirm F1=261113-050356-89edba F2=261113-050356-89edba`.

## Fix jobs at 887a97a5 (code; later commits are journal only)

Lead rulings folded in after e3ac4162: F6 (design doc token), and write-side file bounds per
kind (rulings 16-18). The e3ac4162 jobs are superseded: pins 261113-042535-90d7d4 and history
tests 261113-042539-b20470 passed there; its suite and mutant jobs were cancelled.

Local (Release, this worktree, at 3960cd76 code): every `authority|history` ctest entry
passes; large base prints a 73,260,051-byte base, an 8,411,523-byte seal file and an
8,411,195-byte block installed through a 268,435,456-byte lane, default-policy bounds base
134,217,975, block 29,491,454, seal file 49,692,847, table 52,797,592.
`test_authority_snapshot_catchup` timed out twice in the first local run, under load (four
light jobs and a VM build); it passed twice alone at the same code, and twice at e3ac4162 in
261113-045723-04c9e0. A run with the lane at 64 MiB also passed. I read it as a load timing
flake; the suite job decides.

- Pins 261113-050345-3cc679.
- Full native suite 261113-050356-89edba.
- History tests (transport, large base, names, power cut, node power cut, cluster, change
  history, layout) 261113-050349-8092b0.
- Review mutants (`--fails-without`): first-bytes-win, same-order, retry-failed-forever
  (transport), unchecked-fetch (power cut), old-fetch-bound, accept-low-fetch-bound,
  drop-block-bound, drop-seal-bound, drop-table-bound (large base) 261113-050403-ee2963.
- S5.3 six mutants 261113-050411-f57264; A5, S5.2's seven unwrapped 261113-050418-a797ea.

Confirm mapping: F1 = 261113-050356-89edba (suite: transport, power cut `installs rot`;
mutants first-bytes-win, same-order, unchecked-fetch in 261113-050403-ee2963); F2 =
261113-050356-89edba (suite: large base, transport's unservable file; mutants
old-fetch-bound, accept-low-fetch-bound, retry-failed-forever and the three drop-*-bound in
261113-050403-ee2963).

## Review findings not blocking

- F3 (cold read, executor bound): not measured; F2 raises the bound to 128 MiB + 247 by
  default, so the serving cost scales with it. Status doc Left open 5 says so.
- F4 (restore rounds re-verify everything): left as is; cheap at test sizes.
- F5 (stall then same-process restore skips a directory sync): left; needs an fsync error
  then an install in one process.
- F6: fixed (design doc names the token), once the lead added the design doc to the file set.
- F7 (service write path untested on PosixIO): before S6 enables positions.
- Left open 8 closed by the lead's ruling: write-side bounds per file kind (rulings 16-18).

## Blocked on

Nothing. msg-46 (A1's endpoint): ruled D91, option 1, consensus-fetch-lane at consensus:a197efb;
done here.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1 | 261113-033759-fee988; fails without: 261113-032601-1a5622 |
| A2 | 261113-033759-fee988; fails without: 261113-032601-1a5622 |
| A3 | 261113-033759-fee988; fails without: 261113-032601-1a5622 |
| A4 | 261113-033759-fee988; fails without: 261113-032601-1a5622 (sanitizer CI needs a push) |
| A5 | fails without: 261113-033814-67ea69 |

## Result

History is production-ready for S6's enabling entry: writes leave apply's seal and run as
completion IO the service drives, `durable_through` counts settled writes, a rotten table is
fetched or stops recovery, the node refuses consensus limits below the policy, and voters fetch
history files from each other over consensus's fetch lane, serving only history names. How it
works and what is left open: `docs/status/namespace-s5.md` (S5.2 section, S5.3 paragraphs and
Left open 5-6).

## Rulings I made

1. History writes are a coroutine `IOOperation` (`history.cc` `run`) that the host drives like
   the worker's storage jobs: the cluster harness and tests synchronously, the service on its own
   completion queue and driver beside the journal's. One write per seal: seal file, outputs,
   table, collector, in that order; writes run strictly in seal order.
2. Apply's seal still computes the layout (CPU, and reads of files the layout reaches, served from
   queued writes first); only writes leave it. Apply waits once `Node::history_backlog` (4) writes
   are queued, so memory stays bounded when the device is slow.
3. `durable_through` is the newest settled write's seal (F4); the node keeps the states of queued
   seals and takes checkpoints at `durable_seal()`, the newest durable one; the ring covers through
   its revision.
4. The collector's keep set is computed in memory: what the write's table reaches, plus the seal
   files above it and their outputs that the layout holds. Seal files a pre-crash run left above the
   current seal are removed instead of kept; replay rewrites them with the same bytes.
5. `Fetch` is asynchronous (`done` on the owner). Recovery and install run their synchronous logic
   against local files plus what was fetched; a missing name aborts the attempt, every missing name
   found is fetched, and the attempt runs again once they land. Their writes queue as one write.
   Apply waits while a restore fetches; an install's activation waits until its write is durable.
6. A rotten `layout` table is treated as missing (F1): a peer's copy, else the ordinal-0 path,
   else recovery stops.
7. F6: `check_retained_bounds` refuses when `consensus_limits.log_entries/log_bytes` are below the
   policy's, beside the retained bounds.
8. F7: the history cluster test keeps journals and history directories in `MemoryIO`
   (`testing::memory_storage`); crash safety is the power-cut tests'. 52 s to 0.1 s locally
   (stubbing syncs alone left it at 25-47 s on this loaded host: unlink, rename and create).
9. The power-cut test splits into three ctest entries (`cuts`, `lagged`, `installs rot`) so each
   stays well inside a sanitizer job's per-test budget.
10. File names carry a namespace as the first 16 bytes of its SHA-256 in hex. A namespace may hold
   `/` and run to 199 bytes (knob-name rules), so the old names could not be files for such a
   namespace; found while confining the reader's names (F6). The collector removes only
   `history_name`s.
11. `PeerHistory` reports "no peer holds it" only when every other voter answers not-found; a round
   with an error (no lane yet, timeout, busy) retries after 200 ms, so a voter waits for a peer it
   cannot reach instead of failing its restore.
12. The service starts its peers before recovery (a restore fetches before replay ends);
   consensus frames still wait for `admit()`, which starts with the service loop.
13. Consensus review F3: the reader stays on the owner executor (its signature is synchronous),
   bounded to one file of at most `fetch_bytes`, refused unread above it; 64 MiB from the page
   cache costs 16-18 ms here. Serving off the executor needs an asynchronous reader in consensus
   (Left open 5). F6: names outside `history_name` are refused before the disk, with a test and
   the serve-any-name mutant.
14. Review F1: History hands `Fetch` a check per name, so a fetcher passes a rotten copy over
   and asks the next voter; History still verifies what it adopts. `PeerHistory` starts each
   fetch (and each retry round) at the next voter in rotation, spreading load and keeping one
   bad voter from always answering first.
15. Review F2 (the lead's ruling, no consensus change): the lane's `fetch_bytes` is
   `history::file_bytes(application limits)`, set by the config loader; `Service` refuses
   peers below it, and `file_bytes` refuses limits no size holds. `io_error` (the peer's
   reader threw, or the file is above its bound) and `message_size` are lasting answers: a
   fetch that every voter answered without a transient error ends with their answers, and
   the restore fails with a message naming the file and each voter's answer.
16. Lead ruling (write-side bound): `history::file_bounds(limits, policy)` bounds base,
   block, seal file and table from the policy and Limits; `file_bytes` is their maximum (also
   across a change: the wider of each parameter). The enabling and layout entries are refused
   at encode, decode and apply (`validate_administrative` with the limits), a SetLayout whose
   widened bound exceeds the lane applies as Conflict, and `validate_node_options` refuses a
   configured layout above it. Derivations: status doc "File bounds".
17. The lane, `history::fetch_bytes(limits)`, is max(image_bytes, the default policy's
   `file_bytes`): semantic (voters agree on validation), never below the base, and the default
   policy is always accepted. image_bytes alone refused the default policy in the 4 MiB-image
   test configurations: one entry may carry 64 x (request_bytes + 16 x mutations) = 16.9 MB
   of changes, which a seal file or block bound has to include.
18. Lead ruling on the count cut (derive the bound from the ratio rule, or keep the cut with
   a test that reaches it and a mutant): the ratio rule does not bound it usefully, so the cut
   stays, at `max_blocks` = 32. Derivation, for the blocks after the front at a roll-up:
   - A pair that breaks on ratio has older >= r x younger, so a ratio chain is at most
     log_r(max block / 19) long.
   - A pair that breaks on L was formed at a roll-up t with L_t < merged <= b_i + b_(i+1); the
     cost rule at t kept P = the younger blocks' bytes below alpha x live_t = 4 L_t, so
     P < 4(b_i + b_(i+1)) <= 4(r + 1) b_(i+1): each L-break grows P by at least 20/19 (r 4).
   - So the count is logarithmic, but with base about 20/19: hundreds per namespace, times
     4,096 namespaces, far above the lane.
   - It is reachable: insert-only roll-ups growing 1.17x per seal under alpha 1 keep every
     block (each merge passes L and grows; the blocks sum below live, so cost never fires).
     A simulation of the layout rules (`.scratch/countcut*.py`, this worktree) gives 93 blocks
     at alpha 1, 41 at alpha 2, 11 at 1/2 and 9 at 1/8 for geometric insert growth.
   Test: `test_authority_history_cluster`'s last phase grows a namespace that way after a
   SetLayout (alpha 1, period 1, no carry, one transaction a seal); the count cut fires after
   33 seals on every voter, files byte-identical; mutant no-count-cut. The layout test's
   oracle models the count rule too. Level-0 is bounded by 2P + 1 (a period change), assuming
   a namespace's directory position is fixed per incarnation.
