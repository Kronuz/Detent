# Detent review: namespace delivery, compacted blocks revision (preliminary)

- Date: 2026-11-04.
- Reviewed: `docs/design/namespace-delivery.md` (D6, D7, the D8-D10 wording, T3, T4, T9, Limits), `docs/design/namespace-delivery-blocks-model.py` and `docs/status/namespace-design.md` (owner ruling of 2026-11-03, lead calls B1-B3 of 2026-11-04) at `Detent:d190570`; `docs/design/reader.md` A4 and D4; the apply and history path at `Detent:d190570` (`src/detent_authority/state.cc`, `change_recorder.hh`, `change_history.{hh,cc}`, `node.cc`, `service.cc`); `consensus:61149a2` (`ARCHITECTURE.md`, `include/consensus/types.h`, `core.h`).
- Checked against: P1 and the owner's ruling (a client pays only for what it is missing; a full copy only when cheaper in time, or with no store; bounded disk); the earlier preliminary review and its re-checks (`namespace-delivery-preliminary.md`); `calibration.md`.
- Checked by: probes in `.scratch/blocks-review/` of this worktree, all `python3 -I`, run under `nice -n 19` (`quiet check` reported load 12 of 16, so no build ran):
  - `oracle.py`, output `oracle.out`: randomized oracle of D6's three tiers against st(h) with st(h)'s revisions, for a store at every position of every namespace, apply semantics from `state.cc:952-1004`, group-wide size-only seals, squash and cut as written, restores to a new incarnation. 300 seeds, 849,481 answers, plus seven mutants.
  - `cut.py`, outputs `cut-design.out`, `cut-cap1.out`, `cut-disk2.out`, `cut-live.out`, `cut-cap1-live.out`: key-level simulation of seal, squash and cut (real blocks, real base, 40,000 keys, one key per transaction, a seal every 256 positions) over the design's moderate shape, uniform key sets of 0.15-0.95 C, a bulk load from an empty base and a 90% mass delete; the design's rule and four variants. `cut.out` is the first run, whose cut-transient line over-counts; `cut-design.out` supersedes it.
  - `seal.py`, output `seal.out`: the seal trigger against consensus's checkpoint and `LogFull` limits for mixes of positioned and unpositioned log entries.
  - `b1.py`, output `b1.out`: B1's derived block against server bytes read, block-aligned answers and persisted blocks served whole, on `cut.py`'s moderate layout.
  - The design's model, run as committed: output matches the tables at `namespace-delivery.md:127-131`.
  - Not run: Detent builds, tests or sanitizers (the branch changes docs only); `detentd`, relay or agent timings; T9; Linux; real crashes or power cuts. The cut and B1 numbers are a scaled key-level model, not the 5M corpus.

## Verdict

**REJECT: D6 as written can stop a group's writes for good, its seal and layout inputs differ between voters, and its cut is decided by raw block bytes, not by cost. S1-S4 and S5.1 do not depend on it and may proceed; S5.2 and everything built on blocks wait for NB-L1 to NB-L4.**

What holds, and reproduces:

- The overlap proof (`namespace-delivery.md:118-123`). `oracle.out`: 0 failures in 849,481 answers (694,283 copies, 131,475 derived blocks, 23,723 ring answers) across deletes, re-inserts, NoChange puts, tombstones for keys the store never held, restores to a new incarnation, and stores at every position of namespaces whose positions differ. Mutants die: blocks without tombstones (712,018 failures), the older record winning a merge (736,654), skipping the block that holds R + 1 (92,636), a block carrying an unchanged op at the transaction's position (574,046), ignoring the incarnation (17,909). A4 is never violated.
- The revision filter is not load-bearing: the `filter-ge` and `no-filter` mutants survive with 0 failures. Applying a whole block [a, b] with a ≤ R + 1 to st(R) also yields st(b), so the filter only saves bytes. That gives B1 a safe fallback (NB-6).
- Every operation is absolute and a transaction is in one namespace (`state.cc:207-212`), so per-namespace positions and blocks need nothing from consensus. `State` is a persistent map (`state.hh:220-221`), so keeping the state root of the last seal for a checkpoint is cheap.
- Disk between cuts follows the design's bound in every shape: 1.19-2.33 C at α = 1 (`cut-design.out`).

What blocks: NB-1 (the checkpoint gate deadlocks), NB-2 (non-deterministic seal and layout inputs), NB-3 (the cost cut never decides; B2's premise is wrong), NB-4 (no commit protocol for squash and cut).

## Findings

**NB-1. The checkpoint gate deadlocks a group whose log entries mostly carry no position (D6 "Seal", "Checkpoints and restart"; B3).**
A seal fires only on 2,048 positioned transactions or 8 MiB of them (`namespace-delivery.md:101`), a checkpoint may only be captured at a seal (`:137`), and consensus compacts only through a checkpoint (`consensus/ARCHITECTURE.md:105-109`). Many log entries get no position: session Open and Close, every refusal (Conflict, SequenceGap, FencedSession, IdentityConflict, ResultUnavailable, Capacity), NoChange transactions (`state.cc:890-1015`) and leader NoOps. So the claim at `:137` that "32,768 entries hold at least 32,768 transactions, sixteen seals" is false. When the log reaches 65,536 entries or 64 MiB (`consensus/include/consensus/types.h:58-59`), the core refuses proposals with `LogFull` (`core.h:657-663`). No positioned write can then be admitted, so no seal, no checkpoint and no compaction follow: the group stops taking writes and cannot recover by itself. `seal.out`, with blocks durable at once (the best case):
- 3% positioned entries (a reconciler re-putting desired state, or refusals): `LogFull` at entry 65,537, no seal ever.
- A producer retrying a Conflict in a loop, 1% positioned, 2 KiB entries: `LogFull` at entry 32,769 (64 MiB).
- 20% positioned 4 KiB writes among 64 KiB refused batches: `LogFull` at entry 1,240.
- 10% positioned and 100% positioned stay live.
A quiet group with session churn and no writes reaches the same end. Today checkpoints are taken at any applied index (`node.cc:467-489`, `:519-530`), so this is introduced by D6.
Fix: also seal on log progress counted at apply, from the entry's index and encoded size (for example every 1/8 of `log_entries` or `log_bytes` since the last seal), with an empty seal allowed as a checkpoint point. That is deterministic and keeps B3's size-only rule for blocks.

**NB-2. The inputs that make every voter build the same blocks are not deterministic as specified (D6 "Seal", "Who builds", "The base"; D7; T4.1).**
1. The 8 MiB half of the seal trigger, if taken from the ring's bound, counts `storage_capacity()` (`change_history.cc:333-348`), which is the arena's used bytes (`include/detent_distribution/buffer.hh:64`), not canonical bytes. Different allocators or platforms seal at different indexes, so block ranges and names differ.
2. The ring is not a deterministic input. A capture fails with `StoragePressure` when the delivery ledger is full (`change_history.cc:25-28`; the gap at `state.cc:1481-1486`), and that ledger is shared with subscription requests, admissions and grants (`service.cc:541-553,1470-1471`), so gaps follow each voter's own delivery load. Under fleet load every voter can gap the same range, and then "fetches the block by name from a peer" (`:135`) has no source. A level-0 name also stops existing once the peer squashes it, so a fetch by name usually fails anyway.
3. The first base "comes from a checkpoint image ... once when S5 lands" (`:114`). Checkpoints are local: each voter's due point depends on its own compaction base (`node.cc:503-516`). Voters then hold different p, different block ranges, and different α · |base|, so their squash and cut diverge and T4.1 cannot pass.
4. α, the ratio 4 and the merge limit are configuration (α is "the lead's to tighten" after T9), yet voters and relays must compute the same layout (`:145`, "checks their digests against the upstream's list"). A rolling change of α gives two layouts.
Fix: count the seal trigger from canonical encoded bytes at apply, never from the ring. Make the journal the source of truth for a missing level-0 block: it is retained until the checkpoint gate passes, so a voter rebuilds the block by re-applying entries from its retained seal root, and a peer fetch becomes an optimization keyed by covered range. Take the first base at a log-defined index (the administrative entry that enables positions in S5.1, and the restore entry for a new incarnation). Put α and the layout policy in replicated state, or have relays adopt the upstream's layout list instead of recomputing it.

**NB-3. The cost cut never decides a cut; the disk rule always fires first, and B2's bound is wrong (D6 "Cut", Limit 2, B2).**
The cost rule fires when the deduplicated younger blocks reach α · |base|; the disk rule fires when all blocks exceed α · |base| (`:107-110`). Deduplicated bytes never exceed summed bytes, so whenever the cost rule holds, the disk rule holds too. `cut-design.out`: in the moderate and key-set shapes the cost rule holds at none of the 18-55 cuts, and in the bulk load at 2 of 46, each time with the disk rule. What P1 calls the cut, "where serving it costs more than the full copy", is in fact a raw-bytes budget, and blocks repeat keys whenever the merge limit α · |base| / 4 refuses a merge. Stores just past the cut get a copy while their deltas cost less:
- Moderate: copy up to 1.54 times the answer (the arithmetic model said 1.24).
- Uniform key sets of 0.30, 0.50, 0.75 and 0.95 C: 3.34, 2.23, 1.83 and 1.57 times. History reaches back only 36,000-76,000 positions, against unbounded history for a 0.15 C key set, where blocks merge.
- Bulk load: 1.59 times.
Limit 2 (`:316`) and B2 say a cut store pays "up to 1/α times its answer". With the /4 merge limit the ceiling is about 4/α, and 3.34 is measured at α = 1.
The variants show the trade-offs: a merge limit of α · |base| (`cut-cap1.out`) fixes the key sets up to 0.75 C but cuts the moderate shape in coarse steps (copy up to 4.13 times the answer). A disk budget of 2α · |base| (`cut-disk2.out`) brings the moderate shape to 1.14 times and doubles its reach, at 3.0-3.4 C on disk between cuts.
Fix: decide the cut on deduplicated answer bytes, with the disk bound as a separate, larger budget that only a pathological shape reaches. Let a merge proceed past the size limit when it deduplicates (the merged block is no larger than its inputs' maximum plus a small fraction), so key-set churn stops repeating keys. Then restate Limit 2 and B2 with measured numbers.

**NB-4. Squash and cut have no commit protocol (D6 "Squash", "Cut", "Checkpoints and restart"; T4.2).**
The ring "evicts only once a durable block covers" an entry, and the checkpoint is persisted "once that seal's blocks are durable" (`:101,137`). But a squash replaces two blocks with one and a cut replaces the base and the oldest block, and the design says neither in which order files are synced, published and unlinked nor how recovery picks among overlapping files. A crash mid-squash leaves [a, b], [b + 1, c] and a partial or complete [a, c]. A crash mid-cut leaves base(p), base(p'), and [p + 1, p']. Unlinking an input before its output is durable leaves a hole under a checkpoint that already compacted the journal. A voter installed from a snapshot must also fetch the base, not only "the blocks at or below the install point" (`:135`).
Fix: state the protocol, with D8's discipline as the model: the output is written under a temporary name, synced and linked, and the directory synced before any input is unlinked; recovery picks a tiling of (p, s] deterministically (the newest base and the coarsest durable blocks) and re-runs the squash and cut rules. State the gate as "a durable tiling of (p, S] for every namespace" rather than "that seal's blocks". Add a T7-style crash matrix over seal, squash and cut to T4.

**NB-5. The disk bound omits the cut's transient and the caches (D6 "Bounds", T4.3).**
During a cut the old and new base coexist with the blocks: 3.0-3.5 C at α = 1 against the stated base + α · base + one block = 2.25 times base (`cut-design.out`). Neither the derived-block cache nor the full-copy cache (`:116,139`) has a bound, and neither is counted. Base rewrites cost 0.3-0.5 C written per seal in the key-set shapes (28-55 C over 600,000 positions).
Fix: T4.3's bound includes one base rewrite and a stated cache budget; S5.2 states the cache eviction rule.

**NB-6. B1: a derived block per (R, s) costs a merge per distinct R and an unbounded cache (lead call B1; D6 "Answers"; D7).**
`b1.out`, 1,260 stores above the cut on the moderate layout: B1 sends exactly the missing bytes, but the server reads 1.51 times the answer on average and up to 11.6 times (a short answer whose R falls late in a large block). Each distinct R is its own cache entry (1,260 entries for 1,260 stores), so a fleet seeded from many images, or agents back from outages of different lengths, shares nothing. Persisted blocks served whole send 1.25 times on average and 1.45 at worst, cost no merge, and are shared by every R. Block-aligned answers ([a, s] for the block holding R + 1, correct by the surviving filter mutants) share well (19 entries) but send up to 11 times the missing bytes.
B1 is the right default for T3's wire-bytes target. It needs: a byte-capped cache counted in the disk bound (NB-5); an R used by one store streamed without being persisted; and, past a per-server merge concurrency limit, a fallback to persisted blocks served whole (correct by `oracle.out`). T4.6 then measures merge CPU and bytes read per distinct R.

**NB-7. S5.1 must state how existing record revisions become namespace positions (D3, A4).**
Records carry global revisions today (`state.cc:992-1002`). A reader refuses any record whose revision exceeds its cursor (`src/detent_local/paged_read_state.cc:27`) and any cursor regression within an incarnation (`reader.cc:59-61`). If a namespace's counter starts below its records' revisions, catch-up refuses those stores. Starting each namespace's position at its stored revision (`namespaces_`, at least every record revision in it) keeps both checks, and the first base and blocks start there (NB-2.3).

**NB-8. Per-namespace level-0 blocks at group-wide seals multiply files and syncs (D6 "Seal"; Limit 4).**
Each seal writes one durable file per namespace with unsealed entries. With T8's 2,000 transactions per second spread over 4,096 namespaces (`Limits.namespaces`, `state.hh:26`), a seal fires about every second and writes about 1,611 level-0 files (4,096 · (1 − e^−0.5)). On Linux each fsync is a device flush (`namespace-delivery.md:206`). Fix: one level-0 seal file per group seal, indexed by namespace, split per namespace at its first squash; or size the trigger per namespace once S10 settles groups.

**NB-9. Ring eviction is ambiguous: eager eviction loses the hot path (D6 "Seal", "Checkpoints").**
`:137` says the ring "holds only entries above the newest durable seal"; `:95-99` keep its 4,096-transaction bound and say most agents never touch disk. Eager eviction leaves the ring between 0 and 2,048 group-wide transactions, 0-2.9 minutes at the moderate churn, so an agent one seal behind always gets a derived block. Fix: evict lazily, only at the bound, and only entries a durable tiling covers.

**NB-10. B3: the replay claim holds; what a quiet group loses is compaction.**
A quiet namespace in a busy group seals with the group. A quiet group never seals, so after an authority restart the ring is refilled from the journal above the last checkpoint, which is exactly right, since nothing compacted past it. But the same fact makes the journal grow until NB-1's `LogFull`. After a relay failover, a relay whose newest block equals the upstream's gets exact entries (s, h]; one behind the upstream's newest seal gets a derived block for that span and cannot serve R inside it as an exact edge, so `:145` "any R inside its ring is an edge" needs that qualifier. A reader at R loses no correctness, only ring hits. A relay that is behind must fetch the upstream's current layout, since level-0 names vanish on squash. NB-1's log-progress seal closes the B3 risk.

**NB-11. Smaller points.**
1. The design's model seals at "half the ring, or an hour when that comes first" (`namespace-delivery-blocks-model.py:47-49`), which contradicts B3; and it keeps at least one block (`:97`), which D6 does not state.
2. The base is |st(p)|, but the copy sent is |st(s)|. After a 90% mass delete, block answers reach 3.62 times the copy (`cut-design.out`), against P1; comparing with |st(s)| fixes it (`cut-live.out`: 0). A namespace created after S5 has an empty base, so α · 0 cuts every block but one, and stores just below the ring get the copy. The authority can carry live bytes per namespace at apply (`bytes_`, `state.cc:963-1000`) and publish them with each seal, so relays have the number.
3. A store inside the oldest block can receive deltas up to one block (α · |base| / 4) beyond α · |base| before the cut fires; say so beside Limit 2.

## Blocking items

- NB-L1 (NB-1): seals also fire on log progress counted at apply (entries and encoded bytes since the last seal), and an empty seal is a valid checkpoint point. T4 adds a row: 97% NoChange or refused entries for 200,000 entries, and the group keeps compacting.
- NB-L2 (NB-2): the seal trigger counts canonical bytes; a voter rebuilds a missing level-0 block from its journal above its retained seal root, and a peer fetch is keyed by range; the first base is at a log-defined index; α and the layout policy are replicated state, or relays adopt the upstream's layout. T4.1 runs with an injected capture failure on every voter at the same position.
- NB-L3 (NB-3): the cut is decided on deduplicated answer bytes, with a separate disk budget, and merges that deduplicate pass the size limit; Limit 2 and the ledger's B2 carry measured ratios (moderate, key sets 0.3-0.95 C, bulk load). T4.7 adds the key-set and mass-delete shapes.
- NB-L4 (NB-4): a commit protocol for seal, squash, cut and install (temporary name, sync, link, directory sync, then unlink inputs), deterministic recovery of a tiling, and the gate stated as a durable tiling; T4 gains the crash matrix.

## Conditions

- NB-C1 (NB-7): S5.1 starts each namespace's position at its stored revision, and T3 or a unit test proves a pre-S5 store passes `paged_read_state.cc:27` after catch-up.
- NB-C2 (NB-5): T4.3's disk bound counts one base rewrite and the caches' budget.
- NB-C3 (NB-6): B1 ships with a byte-capped cache, streaming for an R used once, and the whole-block fallback past a merge limit; T4.6 records bytes read and merge CPU per distinct R.
- NB-C4 (NB-8): level-0 output is one file per group seal, or the design states the per-seal file and flush count at 4,096 namespaces and T8 measures it.
- NB-C5 (NB-9, NB-10): the ring evicts lazily at its bound; D7 says a relay behind the upstream fetches its layout and serves derived blocks for the gap.
- NB-C6 (NB-11): the model matches B3 and states the one-block floor; the cut compares with |st(s)|, carried with each seal.
