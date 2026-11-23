# namespace-s5-2 review: durability

Commit: b6161f571f79 (re-review of d13c7054874c; b6161f57 changes only tests/detent_authority/test_history_layout.cc)
Verdict: APPROVE
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-review (b6161f57): the A3 model `compare` still runs after every seal in `churn`'s `follow()`,
and the cut-at-twice-the-cost mutant was killed there (`test_history_layout.cc:362`), not in
`check()`. Only the A6 answer check is sampled, at power-of-two seals, every 32nd and at the end
(`~Run`), about 13 full checks per churn run over every R, all three tiers asserted. That is enough
for A6. F8 (nit) notes that each namespace is always sampled at the same roll-up phase.

## Evidence I relied on

- Native gate 261112-220407-bcf0f5 at eb37a84a: 278/278. `test_authority_snapshot_catchup` passed
  (91.5 s), so the contributor's loaded local failure is not real at this code.
- 261112-220409-a2488f (cluster: identity, restart, install, ring tiling), 261112-220413-0e34b3
  (lazy eviction), 261112-220417-d8a94e (layout), 261112-220422-eb1426 (crash matrix): all exit 0.
- All seven mutants killed (`--fails-without` jobs exit 1 = test failed with the mutant):
  collect-above-table 261112-220450-f5e9a0, table-before-outputs 261112-221626-7487a8,
  trust-local-files 261112-221630-939181, checkpoint-between-seals 261112-221634-e3740a,
  eager-eviction 261112-221638-00d2bb, ring-by-group-revision 261112-220517-95cd37,
  cut-at-twice-the-cost 261112-221642-2dcc98.
- Asserts in the crash and cluster tests are live in Release: `tests/CMakeLists.txt:2`
  `add_compile_options(-UNDEBUG)`; the mutant kills above are assert or exception aborts.
- The crash model is journal's `MemoryIO`: a name is durable only at `sync_directory`, unsynced
  sectors survive at random (`journal/include/journal/memory_io.h:48-81`), removes and renames tick
  as primitives. So the 1,892 + 4,243 cuts cover write ordering, not only clean stops.

## What I checked myself

Risk 1, byte identity. Every input to `Layout::seal` / `apply` (`history_layout.cc`) is replicated
state: records and the accumulator are `PersistentMap`s ordered by `std::less`
(`persistent_map.hh:17`), first bases and sections are built in key order, namespaces sorted by
name, outputs and written files sorted, sizes canonical, policy and seal counters in the image
(`DTAIMG03`) and in each seal file. `close_entry(entry.payload.size())` runs once per committed
index on every voter (`node.cc:544`). No clock, randomness, hash container or directory order in
`history*.cc` (grep). Per-node state (`cuts_`, `covered_`, `ready_`, capture failures, local
`Limits::layout`) never reaches bytes: the ring is outside the seal's inputs, a stalled voter
writes nothing and replays the same seals on restart, and the log thresholds in the policy come
from the entry, not the local config. Nothing outside the four disturbances reaches the bytes.

Risk 2, crash safety. `Directory::publish` (`history.cc:62-95`): `.tmp-` write, `sync_files` on
the group, rename, `sync_directory`; a reused name is one an earlier publish synced before
renaming. `seal()` commits seal file, then outputs, then table, each its own synced group, before
it returns; the collector runs only after the table's directory sync and keeps seal files above
it and their outputs. Recovery verifies every reached file by digest (seal and table files carry
a SHA-256 trailer), chains seal files and recomputes each (`step.seal == recorded`), rebuilding
missing outputs. The crash test compares the whole directory byte for byte with the
uninterrupted run after every cut (`test_history_power_cut.cc`, `files(disk) == expected`), with
no peer. Install publishes table and files before `snapshot_activated` (`node.cc` install hunk).
The checkpoint gate (`checkpoint_source`) takes only `sealed_` and only when
`durable_through`; the node-level cut test recovers with no peer, so a checkpoint above an
undurable seal fails the chain; the checkpoint-between-seals mutant is killed.

Probe: a rotted `layout` table with a peer holding it
(`.scratch/review-namespace-s5-2-durability/probe_rotten_table.cc`, run by `run_probe.sh`), job
261112-222708-f46c7e: recovery refuses with "history file digest mismatch" instead of fetching
(F1). Rerun the job at a fix; it exits 0 when the table is repaired and the directory matches.

Risk 3, ring eviction. `change_history.cc` publish: eviction at the bound only while the head's
revision is at or below `max(durable, dead)`, else the new entry is refused (a ring gap); `covered`
is the newest durable seal's group revision, falls to 0 while history is not durable, and never
rises on a failed seal (`Node::cover`). The ring is attached before recovery (`service.cc`), reset
at the checkpoint and refilled by replay. Eager-eviction and ring-by-group-revision mutants are
killed.

Risk 4, scope rulings:

- (a) No enabling proposal in production: correctly later, namespace-s6-cutover (its A1 answers
  from S5.2's history, which needs the entry). Unreached, the production path is unchanged:
  `checkpoint_source` returns the live state and `covered` is unbounded before enabling.
- (b) No production peer fetch: no criterion of this quest needs it (A4 and repair are shown with
  the harness fetch), and it is unreachable while (a) holds. But once (a) lands, every snapshot
  install throws (`history.cc:210`, "an install needs a peer's history") and a lagging voter can
  never rejoin. It must land in or before namespace-s6-cutover, as a precondition of the enabling
  proposal (F2).
- (c) Synchronous history IO in apply's seal: not a byte hazard and no criterion of this quest
  (T4(7) is namespace-measure's); a full disk fails fast and apply continues as D6 says. It is a
  liveness hazard at scale (a slow device blocks apply; up to 177 flushes a seal on Linux at 4,096
  namespaces per D6 "Files"), reachable only after (a). Assign it beside (b), before
  namespace-s6-cutover enables positions (F3). Moving it makes `durable_through` wrong as written
  (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | A rotted `layout` table stops recovery even when a peer holds it; D6 "Recovery" fetches any file failing its digest. Only a missing table is fetched (`history.cc:141-159`). Fail-stop, never wrong bytes. A block that rots under a live seal likewise stalls history until restart instead of fetching. | job 261112-222708-f46c7e | Treat a table that fails `decode_table` as missing (fetch it, else the ordinal-0 path); add the probe's case to `test_history_power_cut`'s rot section. |
| F2 | no | gap | Production attaches history without `fetch` (`service.cc:1523`); after enabling, installs always fail and a rotted file is unrecoverable. | `history.cc:210` | Lead: make the peer-fetch endpoint a precondition of namespace-s6-cutover's enabling proposal. |
| F3 | no | gap | History IO (seal, outputs, table, collector, install fetch) runs synchronously in apply and in `service` (install); outputs are not paced across the period as D6 "Commit" (2) says. | `node.cc:547-551` | Lead: assign before namespace-s6-cutover enables positions. |
| F4 | no | nit | `durable_through` reads the in-memory layout's index (`history.hh:43`), advanced by `layout_.seal` before the publishes; correct only because the IO is synchronous and a throw clears `ready_`. | `history.hh:43`, `history.cc:225-233` | When F3 moves IO off apply, track the last durable seal index separately. |
| F5 | no | gap | `Layout::answer` returns a Blocks answer whose `through` is below the ring floor when a capture gap sits above the latest seal; the caller gets no signal and the test skips those R (`test_history_cluster.cc:150`). Not reachable until S6 serves. | `history_layout.cc:295-299` | State in the `namespace-blocks` contract that such an R has no answer until the next seal, or return a distinct tier; namespace-s6-cutover. |
| F6 | no | gap | `check_retained_bounds` compares only `scheduling.retained`; the leader's LogFull also uses its own consensus `log_entries`/`log_bytes` (`consensus/core.h:725-729`), not replicated (ruling 1 put them in the policy, not the configuration). A node configured below the policy's thresholds can reach LogFull before a seal. Liveness only. | `node.cc:63-72` | Also refuse when `options_.consensus` limits are below the policy's, or carry them in the configuration as D6 "Seal" says. |
| F8 | no | nit | With period 4 or 64 dividing the 32-seal stride, each namespace is sampled at one fixed phase of its periodic roll-up; only the power-of-two and end checks vary it. | `test_history_layout.cc:151-152` | Use a stride coprime to the period (for example 31) |
| F7 | no | nit | `test_authority_history_cluster` took 177.9 s of its 180 s timeout in the native gate; sanitizer CI will likely time out. | gate log, `tests/detent_authority/CMakeLists.txt:53` | Raise the timeout or shrink the run. |

## Not run

- Sanitizers and the Linux matrix (CI 43377651490 was in progress at review time).
- Real-device fsync behavior (APFS `F_FULLFSYNC`, ext4) of `sync_files`; I relied on the
  `MemoryIO` model.
- Full-disk / EIO injection on a live node (T4's "injected seal-file write failure"): read only.
  A failed seal leaves `ready_` false until a restart or an install; no automatic retry.
- T4(6) liveness and T4(7) measurements: not this quest's criteria.
