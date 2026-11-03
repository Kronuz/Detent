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
