# namespace-s5-3: journal

State: waiting on jobs 261113-033825-96867a 261113-033810-0c0530 261113-033759-fee988 261113-032601-1a5622 261113-033814-67ea69
Quest: .campaign/detent/quests/namespace-s5-3.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Every criterion is in code at 42dc0361 (main merged at e4a7e81d, consensus pinned at a197efb).
The history-tests job at d52bbc1b (261113-032554-b6d693) failed only in
`test_authority_history_layout`, which pinned the old block-name format; 42dc0361 pins the token
name. Jobs:

- Gate: pins 261113-033825-96867a; full native suite 261113-033810-0c0530.
- A1-A4: history tests (transport install, names, power cut, node power cut, cluster, change
  history, layout) 261113-033759-fee988.
- A1-A4 mutants (`--fails-without`, at d52bbc1b; 42dc0361 changes only the layout test, which
  none of them runs): reader-serves-nothing, durable-at-seal, trust-rotten-table,
  retained-bounds-only, serve-any-name, namespace-in-file-names 261113-032601-1a5622.
- A5 (`--fails-without`): the seven S5.2 mutants unwrapped 261113-033814-67ea69 (after the history
  tests).

Earlier, at e67233f0: pins, history tests, suite, both mutant runs passed (261113-013222-0492b8,
-013224-9d40a2, -013247-78f1d4, -013251-a8f47b, -013300-94782d).

Local at d52bbc1b: `test_authority_history_transport` passes in 2.5 s (voter three installed at
index 37 with an empty history directory, 12 files fetched, 11 files identical, 15 answers
equal); `test_authority_history_names --measure`: serving 64 MiB from the page cache, 16 ms mean,
18 ms slowest over eight reads.

A4's "CI green on every sanitizer job" needs the branch pushed, which is the lead's.

## Next

1. Read the jobs; fix what fails; paste the evidence table; State "ready for review".

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
