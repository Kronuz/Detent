# namespace-s5-3: journal

State: fixing F1, F2
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

1. Build and run the history tests locally; commit; submit history tests, suite, pins and
   mutants at the fixed commit; journal the confirm mapping; State "ready for review".

## Review findings not blocking

- F3 (cold read, executor bound): not measured; F2 raises the bound to 128 MiB + 247 by
  default, so the serving cost scales with it. Status doc Left open 5 says so.
- F4 (restore rounds re-verify everything): left as is; cheap at test sizes.
- F5 (stall then same-process restore skips a directory sync): left; needs an fsync error
  then an install in one process.
- F6 (design doc names files by namespace): `docs/design/namespace-delivery.md:157` is
  outside this quest's file set; the lead's to fix (name the namespace token there).
- F7 (service write path untested on PosixIO): before S6 enables positions.
- `file_bytes` covers a base; an extreme policy (huge `seal_bytes`, α or `carry_bytes`)
  could write a block or seal file above it. Its fetch now ends, reported. Status doc
  Left open 8; ranged fetches would lift it.

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
16. No write-side cap: refusing to write a file above `file_bytes` would stop every voter at
   the same seal; under an extreme policy a fetch now ends, reported (Left open 8).
