# Detent review: namespace delivery design (preliminary)

- Date: 2026-11-03.
- Reviewed: `docs/design/namespace-delivery.md` and `docs/status/namespace-design.md` at `Detent:ee0fa78` (docs only over `Detent:d4abd4a`); the cited code at `Detent:d4abd4a`, `completion:576618d`, `consensus:61149a2`, `journal:cb66f3d4` and `strata:9d52824`; the attribution at `Detent:e8d4e59`; `reader.md` at `Detent:a3bb22f`.
- Checked against: D1 and D2 (rulings, not reviewed); the owner's rules (one replication mechanism, strata capabilities serve Xapiand, 5M knobs, 10k producers, 250k agents, 10^7 groups); `STORAGE_BARRIER_ARCHITECTURE.md`, `SCALE_TARGETS.md` and `SCALING.md`; strata's `REPLICATION.md`, `DISTRIBUTION.md` and `docs/design/track-c.md`; `calibration.md`.
- Checked by: probes in `~/code/Kronuz/Detent-wt/namespace-review/.scratch/namespace-review/`:
  - `idle_groups.cc`, `run_idle.sh`: N three-voter groups on consensus's `Core`, every message through the `DTC2` codec, in memory, instant persistence; AppleClang 17 `-O2`, i9-9980HK, one run per row under `quiet strict`. Output: `idle_groups.out`, `idle_groups_rss.out`.
  - `retention_model.py`: D6's retention as written against lagged retention. Output: `retention_model.out`.
  - `crash_model.py`, `crash_model_first.py`: visible and durable filesystem state; a cut at every primitive of a publication, then restart and republication with a power cut at every step. Output: `crash_model.out`.
  - Not run: Detent builds, tests or sanitizers (the branch changes docs only); `detentd`, relay or agent timings; Linux and FreeBSD; real power cuts; AWS leases.

## Verdict

**APPROVE WITH CONDITIONS: S1-S4 may start under L1 and L2. D3, D6, D8, D9 and the wire list need revising (L3-L7) before S5-S9 start.**

What holds, and reproduces:

- The decisions follow the measured causes: six hash passes and per-file `F_FULLFSYNC` at restart, 239 s of sync in bootstrap (attribution, "Profile").
- Strata's replication shape without its segments is right: a delete sets a live bit in an older segment and merges need no tombstones (`strata/include/strata/core/index.hh:1749-1754`), so segments carry no position range.
- No operation is relative (`state.cc:943-1000`): applying the applied transactions after R is exact.
- D11's invariant and single startup barrier hold in the model when every directory that gained an entry is synced and a reused name is verified and synced: 0 violations in 439 cases.

What blocks: L1-L7 below.

## Findings

Builds-on findings first.

**ND-R1. A group per namespace does not fit today's stack at 4,096 per voter (D3, T8; blocks S5).**
`idle_groups.out`: with ticks at the 100 ms heartbeat, one idle group costs 32.7 µs of CPU per second. 4,096 groups cost 6.3% of a core per voter, 16.6% with 10 ms ticks, and 115% with the authority's 1 ms owner loop (`service.cc:2528-2530`), exchanging 163,840 messages per second and holding about 35 MB of `Core` per voter. That floor has no sockets, TLS, Worker, Node or journal, and already misses T8's 2% by 3x. Also missing:

- Coalesced heartbeats and quiescence: strata Track C2 (`track-c.md:627-668`), designed, not built (`track-c.md:18`, `consensus/tools/mutants.sh:18-21`).
- The shared turn log: the authority runs one Node on a per-directory journal (`service.cc:1393`). Without it, each of T8's 2,000 transactions per second is its own barrier, at 18.9 ms per `F_FULLFSYNC` on the reference host.
- Disk-resident history: each `Core` holds its retained log in memory (`core.h:1186`, `retained_log.h:39`).
- A feasible latency: T8's p99 of 50 ms is below the 80 ms barrier p99 consensus measured on this host class (`types.h:110-112`).
- A row for the election storm after a voter restart.

D1 makes the namespace the unit of placement, not a group per namespace; `SCALE_TARGETS.md:118` and `SCALING.md:52` planned groups that each own a set of namespaces.

Fix: Q5 (d) below. If one group per namespace stays, name C2's D12 and D13, the turn-log host and disk-resident history as S5 prerequisites. Give T8 a platform and add a voter-restart row.

**ND-R2. Retention as written has a sawtooth (D6, Q2, T3, T4; blocks S5).**
The table keeps "the suffix after the newest checkpoint", and consensus compacts through the checkpoint on `Persisted` (`consensus/ARCHITECTURE.md:109`). So a consumer one entry behind a fresh checkpoint gets the checkpoint.

`retention_model.out`, at k = 1: a consumer X ≤ C behind gets the checkpoint with probability X/C, 30% between 0.1C and 0.5C and 75% between 0.5C and C. It receives 1.2 to 2.0 times the ideal bytes on average, C/X at worst, and a fleet started from one image gets the same answer together. Q2's "an image of any age gets entries whenever they are cheaper" is false as written; T3 and T4 pass by construction: neither cuts a checkpoint after R where it expects entries. Retention is also capped at 65,536 entries by default (`types.h:59`), forcing checkpoints for small writes.

Fix: keep entries back to the previous checkpoint (disk about (1+2k)C, or (1+k)C at k/2), on disk below the consensus compaction point and read by range, not in the `Core`; add a floor and a checkpoint-rate cap for small hot namespaces; add T3 and T4 cases that cut a checkpoint after R.

**ND-R3. The journal holds commands, not outcomes (D6; blocks S5).**
Apply can refuse a transaction: unknown or fenced session, retained retry, sequence gap, conflict, capacity (`state.cc:912-990`). Projecting entries without the outcome ships refused writes, and consumers diverge silently; "computed, never stored" needs the state before each entry.

Fix: record one applied bit per retained entry, rebuilt by the replay recovery already runs, or propose effects. Say who serves the tail once `ChangeHistory` goes; the `Core` has one owning executor.

**ND-R4. Content-derived names break the durability invariant (D8, D10, D11; blocks S7, S9).**
`crash_model.out`, with reuse by name: 14 violations in 283 cases. The interleaving: publication of g creates `ns/a/segment-g.owner`; the process dies before its fsync; restart issues one barrier and syncs no file; the republication derives the same names, finds the record and leaves it out of its sync group; the table replace and its barrier make g durable; a power cut leaves the durable table naming an owner record whose bytes never reached disk. Today's random ids (`coordinator.cc:211`) rule this out.

D10's repair at an unchanged head derives the id of the selected generation it repairs, H(incarnation, h, 0): the `O_EXCL` create (`completion posix.h:732`) refuses it, and removing the old files first opens a window in which the durable table names missing files. The id also omits the layout version and profile, so two agent versions give different bytes one name.

Fix: verify and sync any reused name in its group (0 violations in the model). Create under a temporary name and rename without replacing. Write repairs as new inodes. Add the layout version and profile to the id.

**ND-R5. A barrier per cycle must span directories (D8, D11, W12; blocks S1, S7).**
D8 puts each namespace in `ns/<incarnation>/`, but `sync_files_and_directory` syncs one directory (`completion io.h:222-235`) and the blueprint rejects groups spanning directories (`STORAGE_BARRIER_ARCHITECTURE.md:32`). With only the store directory synced, the model finds 32 violations in 287 cases. D11's "the way `IO::sync_files` does" syncs no directory at all. The amortization is macOS-only: Linux `files_sync` is one fsync per file (`posix.h:231-240`), each a device flush on ext4 or XFS; FreeBSD also syncs file by file.

Fix: S1's primitive takes files across a store's directories, syncs each directory once and flushes once per device, or D8 keeps one directory. State the Linux and FreeBSD costs; T2's Linux run decides whether S1 needs concurrent fsyncs.

**ND-R6. Adopting a seeded image copies it up (D9; blocks S9).**
Completion opens existing files `O_RDWR` (`posix.h:731`), and overlayfs copies a lower-layer file up on a write open: adopting the 1.51 GB store writes 1.51 GB per container and loses page-cache sharing between containers. `validate_file` requires the runtime uid (`posix.h:113-117`), so an image built for another uid is refused.

Fix: sync seeded files through read-only descriptors. Say where the store lives (image layer or volume), and report a uid mismatch as a health reason rather than a silent 722 s bootstrap.

**ND-R7. The wire list misses four changes (W1-W12; blocks S6).**

1. **Producers.** One contiguous sequence spans namespaces (`AUTHORITY_ARCHITECTURE.md:16`, `state.cc:938`); a group per namespace needs sessions, sequences and `/v2/result` per group, and the preconditions change meaning. Under Q5 (d) with one group, sessions and sequences stay.
2. **Relays as learners.** D7 puts every relay in every group's membership, against `SCALING.md:52`; P2's range reads serve relays with one mechanism.
3. **Incarnations.** The reader throws on any identity change (`reader.cc:58-61`), so a restore needs a stated reader status; if deletion arrives, restarted positions make preconditions ABA-prone unless positions continue.
4. **Immutable ranges.** Shared HTTP caches do not authorize per namespace, and a range ending at each caller's head is a different object per agent: end ranges at sealed boundaries and serve the tail live.

**ND-R8. A page verified earlier and corrupted later is served unchecked (D10, Limit 1).**
A long-lived reader keeps the tag of a page it verified (reader D3); a block corrupted after eviction is faulted back in under that tag and served until the scrub. This predates the design, but Limit 1 must say it. An hourly pass over 1.51 GB costs about 0.2% of a core at this host's 223 MB/s: pace the scrub continuously, not once per 24 h.

**ND-R9. Readiness must stay after the startup barrier (D11).**
The census calls `ready()` only after its barrier (`disk_store.cc:178-179`). D11 orders it only before garbage collection and publication; serving first lets a power cut revert a served table. Name the table and the change word in group B.

**ND-R10. The strata stance is sound; P1 and P2 merge (strata fit).**
P1 as written is a `RetainedStatePolicy` cap, not retention. The general capability is ND-R2's: journal history on disk below compaction, read by range by non-members, within strata's node budget (`track-c.md:669`); Xapiand would use it for change capture and remote replicas. Keep P3 out until a workload needs it.

**ND-R11. Gate rows and criteria.**
The ledger has no done criteria. T6 needs two platforms and must exclude `.lease` and `owner.lock`. T7 needs restart-then-republish and cycles that span directories. T8 needs a platform, memory, file descriptors and a restart row.

## Positions on Q1-Q5

- **Q1:** (b), limited to exact names, whole-component prefixes and `*`. The matcher goes to the security reviewer.
- **Q2:** (a), with ND-R2's lagged retention and floor. Not (b): its cost has no bound. (c) only if Xapiand takes P3.
- **Q3:** (a), because the image shares the binary's supply chain. Add the residual to Limits: keys that an altered image adds survive entries-only catch-up until a checkpoint answer replaces them.
- **Q4:** the owner's call, since it removes a mode. Options: (a) remove the endpoint and the mode; (b) keep both; (c) one session endpoint with a wait time, `poll` becoming a zero wait every `poll_interval_ms`, which keeps the behavior on one path. I recommend (c).
- **Q5:** (d), a new option. W1's position becomes the namespace's own counter, assigned at apply; `state.cc:1002` already keeps a revision per namespace. Cursor, frames, store, relay and image then do not depend on the group count. Build S6-S9 on today's group, and choose bounded groups or one per namespace once C2 exists and T8 is measured on Linux. The cost is that history retention stays per group until then.

## Blocking items

- L1 (before S1): settle ND-R5 (a primitive spanning directories, or D8 in one directory); allow read-only descriptors (ND-R6); state each platform's semantics; seam tests on every CI platform.
- L2 (before S2): T7's matrix adds restart-then-republish and the startup barrier's primitives, and readiness stays after the barrier (ND-R9).
- L3 (before S5): revise D3 and T8 (ND-R1).
- L4 (before S5): revise D6's retention, where history lives, and how outcomes are known (ND-R2, ND-R3).
- L5 (before S6): complete the wire list (ND-R7).
- L6 (before S7): the id discipline and the barrier that spans directories (ND-R4, ND-R5).
- L7 (before S9): adoption's open mode and uid handling (ND-R6).

## Conditions

- C1: the lead writes S1-S4 criteria before they start: S1, recorded call sequences per platform; S2, sync counters and T7 on today's store; S3, T5's byte flips, first-touch checks on the agent's base reads and a paced scrub; S4, bytes hashed per tier at most 1.2 times bytes built.
- C2: S2 and S3 keep today's random ids and single directory until L6 closes.

Not run: Detent builds, tests and sanitizers; `detentd`, relay and agent timings; Linux and FreeBSD; real power cuts; benchmarks; AWS leases.

## Re-check (2026-11-03)

- Reviewed: `docs/design/namespace-delivery.md` at `Detent:29a64fc` (docs only; code cited at `Detent:d4abd4a`, `completion:576618d`, `consensus:61149a2`; completion's `posix.h` lines re-read at its current head, unchanged for the cited lines).
- Scope: L1-L7 and the mechanisms the revision added (outcome records, group budget, H1, `seed_owner_mismatch`). Not a re-review of the design.
- Probes in `.scratch/namespace-recheck/` on this branch (untracked): `crash_model2.py` (+ `.out`, `wedge_first.py`), `retention_model2.py` (+ `.out`), `t3_wire.py` (+ `.out`). The ND-R2/R4/R5 originals stay in `Detent-wt/namespace-review/.scratch/namespace-review/`.

### Verdicts

| Item | Verdict | Evidence |
| --- | --- | --- |
| L1 (ND-R5, ND-R6 at S1) | CLOSED | One directory (D8), `SyncFiles`/`OrderFiles` over read-only or read-write descriptors (D11, W14, S1), platform costs stated (D11, Limit 6), seam tests per platform (S1). `crash_model2.py` has one directory and 0 violations |
| L2 (ND-R9, T7) | CLOSED | D11 orders barrier, `.tmp-` removal, `change` bump, then ready; T7 adds restart-then-republish and the startup primitives. Model: readiness after the barrier 0 reverted served tables, before it 1. One sentence to add: N6 |
| L3 (ND-R1) | CLOSED | D3 keeps one group, positions per namespace; T8 has platforms, memory, descriptors, a voter-restart row and an achievable p99 (barrier p99 + 20 ms); S10 gates the group count |
| L4 (ND-R2, ND-R3) | PARTIAL | Retention: CLOSED, no sawtooth (below). Outcomes: the mechanism closes the refused-write hole, but "a record per entry" is wrong for batched entries (N2), and the Replace at b has no stated source (N3) |
| L5 (ND-R7) | CLOSED | W4, W5, W6, W7, W8 and D7 cover producers, relays as readers, incarnations and sealed ranges; one gate inconsistency (N5) |
| L6 (ND-R4) | CLOSED | 0 violations in 643 cases, first publication and repair at an unchanged head, with republish after a crash. New wedge: N1 |
| L7 (ND-R6) | PARTIAL | Adoption's sync path and the uid health reason land. "`change` is the only seeded file written" is not established: completion's open of an existing file is `O_RDWR` (`posix.h:731`) and the collector and census open `.lease` through it (N4a). The fallback collides with derived ids (N4b) |

### L6: the ND-R4 model, rerun on D8's discipline

`crash_model2.py` models one directory; `.tmp-` files linked to the final name without replacing, then unlinked; reuse verified and put in the sync group; the group order of D11 (owners, payloads, table fsync, rename, directory sync); startup that syncs the directory and removes `.tmp-` names. A process cut at every primitive of the first publication, then restart, then republication of the same input with a power cut at every step.

| Scenario (design variant) | Cases | Durable table names a missing or wrong file |
| --- | --- | --- |
| First publication, crash, restart, republish | 643 | **0** |
| Repair at an unchanged head at ordinal r + 1 (g0 durable and selected), same cuts | 643 | **0**; g0's files are never touched |
| Control: reused names trusted without verification | 532 | 20 (the ND-R4 interleaving reproduces, so the model has teeth) |

The ordinal makes the repair write new inodes under new names, as D8.2 says. A repair at the old id does not need a model: its names exist and verify against the digest of the file being repaired. Wedge, readiness and the table order are below.

### L1 and L2: the ND-R5 barrier model

With one directory the cross-directory violations (32 of 287 in the review) cannot arise: nothing is created outside the directory the barrier syncs. Readiness: a power cut at the instant of ready reverts the served table in 1 of 643 cases when ready comes before the barrier, in 0 when it comes after (D11). Restart-then-republish is inside the 643 cases above.

### L4: retention, outcomes, ABA

- **No sawtooth.** `retention_model2.py`, k = 1, F = 1 MiB, S = 256, uniform head phase, consumers X ≤ min(W, C) behind. The share getting a Replace is flat in X/C and 0.0% for C = 100 MB and 1 GB with 1 KiB entries (bytes sent over ideal 1.00-1.02). It was 30% at 0.1-0.5 C and 75% at 0.5-1 C as written (`retention_model.out`). A 1 KiB namespace gets entries 99.6% of the time, and a Replace the other 0.4%, when the Replace is cheaper. Retention is the newest W bytes rounded down to a boundary, so every X ≤ W is retained by construction. Disk is W plus one range: 1.00 C plus a range for C ≥ 100 MB; the floor sets it for small namespaces (1.25 MiB for C = 1 KiB).
- **Granularity cost.** Whole ranges are sent, so a consumer a few entries behind gets up to S − 1 extra: 1.78 times the needed bytes for 64 KiB entries and X below 0.1 C. Bounded, not a sawtooth; see N5 for what it does to T3.
- **Refused writes.** `state.cc:912-990`, `:943-950`, `:1006-1007` match D3: refusals and `NoChange` leave `namespaces_` untouched (`:1002` runs only under `Applied && changed`), so giving them position 0 and leaving a gap-free counter is consistent with the code. D6's rule that a position gap is a source failure closes the silent-divergence case. T3 mixes 1,000 refused writes in. Not run: an implementation.
- **ABA.** Preconditions already compare per namespace (`:943-951`); adding the incarnation makes a restored or re-created namespace a `Conflict` for any old precondition. CLOSED on the text. The text does not say how three voters restoring separately agree on the new incarnation (an admin entry in the log would); a question for S5.1, not a finding.

### New findings

**N1. A mismatching reused name wedges the namespace (D8.4; medium, before S7).**
D8.4 fails the publication when a reused name's bytes differ from the expected digest. Interleaving, `wedge_first.py`: the process dies after `linkat` of `a-segment-g1.owner`; restart issues its directory barrier, which makes the name durable with unsynced data; power is lost; the durable store holds the name at zero length; the republication of the same input derives the same id, finds a mismatch and fails; every retry fails the same way until the head moves. 162 of 643 power-cut cases end in that state (0 violations of the durable-table invariant: it is liveness). A bootstrap of a quiet namespace never gets a first generation.
Fix, 0 wedges and 0 violations in the model: a mismatching name that the visible table does not reach is an orphan; unlink it and rebuild (or build at the next ordinal). A mismatch on a name the table does reach is corruption, and D10's repair applies.

**N2. A result record per command, not per entry (D6; medium, S5.1).**
The replicated entry is a batch of up to 64 commands (`state.cc:1019-1030`, `DTABAT02` in `AUTHORITY_ARCHITECTURE.md:34`), each in its own namespace with its own position and up to 128 operation bits. D6 sizes "a result record of at most 24 bytes for every entry". It holds per command; an entry needs up to 64 of them (about 1.5 KiB), and the position index maps to (log index, command offset). Not fatal; say so and fix the bound.

**N3. The Replace at b has no stated source (D6 table, D7, W6; medium, S5.2).**
A Replace is the namespace's state at the boundary b, an immutable object every voter and relay serves identically. Apply holds the state at head; entries carry new values only, so state at b cannot be rebuilt from head. It must be captured at apply of position b: C bytes written every S positions, which at C = 100 MB, 1 KiB entries and S = 256 is 400 times the entry bytes (arithmetic from D6's parameters, not measured), and a hot 1 GB namespace at 1,000 writes a second is about 4 GB/s. Also unstated: a voter that installed a checkpoint has no state at older boundaries, so voters could answer differently (T4 expects the same answers after a voter restart); and "W plus one range plus 10%" in T4 leaves the Replace out, though it is C more bytes (about 2C at k = 1).
Fix: say how the object is produced (a snapshot per boundary, at what cost, kept how long), who serves it after an install, and count it in the size bound; or name the Replace at h and give up its sharing across callers.

**N4. Read-only adoption: two holes (D9; medium, S9).**
- **a.** W14 adds read-only descriptors to the sync primitives only. Completion's existing-file open is `O_RDWR` (`posix.h:731`), and the collector and census open each generation's `.lease` through completion (`disk_store.cc:1269`, `PrimitiveKind::ExclusiveLease`, `operation.h:39`). Opened read-write on an overlayfs layer, a seeded `.lease` copies up (tens of bytes, not the packs), so "`change` the only seeded file written" is false unless S1 also gives the open a read-only mode and D9 says what happens to `.lease` (not seeded, or flocked through a read-only descriptor, which `flock` allows). I did not run an overlayfs copy-up test.
- **b.** On `seed_owner_mismatch` the agent bootstraps into the same directory. For a quiet namespace the Replace is at the image's own incarnation and position, so D8.1 derives the seeded generation's id and D8.4 finds the seeded files, verifies their bytes (they match) and reuses them: the table then names files `validate_file` refuses (`posix.h:116`). The fallback must build at the next ordinal or a fresh name set; D8.2 says an ordinal bumps for a repair only. It also needs a statement that the store opens at all with foreign-owned files present (`posix.h:453` records `foreign_owner`).

**N5. T3's wire bound fails by design at S = 256 (low).**
`t3_wire.py`: T3 applies 1,000 changes over 10 namespaces, 100 each. A consumer 100 positions behind gets whole sealed ranges, so it is above twice the projected bytes in 39% of runs (mean 1.81; at 50 behind 19.5%; at 500 behind 0%). Bound T3 as "twice plus one range", or let a range be fetched from an offset.

**N6. Two clarifications (low).**
- D11 group 3 says "the table replace and the `change` increments, barrier". With a startup that syncs no file, the temporary table must be fsynced before the rename. Mutant (rename, then fsync): the directory barrier at restart makes the renamed name durable with unsynced data, and a power cut reverts the served table (1 of 642 cases, with ready after the barrier). State the order in D11.
- reader.md A4 says a new incarnation "is a new namespace to the reader" (`reader.md:120`); the design's Reader alignment says a generation switch that reports the new incarnation. Pick one.

### Added mechanisms

- **Outcome records:** sound in principle (apply is deterministic, so every voter writes the same bytes; replay rebuilds records above the checkpoint; compaction waits for durable history). N2 and N3 apply.
- **Group budget:** consistent with Limit 3. Clamping the oldest windows hits cold namespaces first, as the design says. The default 2 · ΣW is large (the floors alone make ΣW at least 4 GiB at 4,096 namespaces); T4 measures it.
- **H1:** general enough in shape (an entry, a bounded result record, a horizon the state machine supplies, a byte budget, range reads by non-members). Three things its owners will ask: consensus's `Applied` event carries only `through` (`types.h:184`), so apply output is new interface; compaction is decided on `Persisted` alone (`ARCHITECTURE.md:109`), so "compaction waits" is a new gate in the core; and a voter that installed a snapshot has no history before it. All three belong in the proposal.
- **`seed_owner_mismatch`:** N4b.

### Remaining before the next step

- Before S5.1: N2. Before S5.2: N3 and the H1 points. Before S7: N1, N6. Before S9: N4. N5 with T3's next edit.
- S1-S4 are not blocked by anything here.

Not run: Detent builds, tests and sanitizers; benchmarks; Linux, FreeBSD and overlayfs (the copy-up in N4a and the reuse in N4b are read from code, not exercised); real power cuts; `detentd`, relay and agent timings; AWS leases.
