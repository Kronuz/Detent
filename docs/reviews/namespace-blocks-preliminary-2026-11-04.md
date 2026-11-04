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

## Re-check of the blocks fold (2026-11-04)

- Reviewed: `docs/design/namespace-delivery.md` and `docs/design/namespace-delivery-blocks-model.py` at `Detent:bec722c` (the fold is `Detent:c4922ed`; `src/` is unchanged since `Detent:d190570`), against the findings above. Citations below are lines of the revised design unless a file is named.
- Probes, all `python3 -I` under `quiet light` at `nice -n 19` in `.scratch/blocks-recheck/` (the exclusive holds of other agents delayed them for about two and a half hours; no `quiet exclusive` run was needed or taken):
  - `model-seal.out`, `model-layout.out`: the design's model as committed (`seal`, `layout`).
  - `seal2.py`, `seal2.out`: adapted from `seal.py`. The trigger is the revised one (8,192 entries or 8 MiB of entry bytes since the last seal, beside 2,048 transactions or 8 MiB of changes), entry sizes are random up to `command_bytes` (1 MiB), runs are bursty, and LogFull is `core.h:728` as written (`entries >= log_entries - control_entries`, `bytes > log_bytes`, `size > log_bytes - bytes`). Checkpoints fall due at half the limits (`node.cc:511-513`), are captured at the next seal and compact after a delay D.
  - `cut2.py`, `cut2-design.out`, `cut2-small.out`, `cut2-shrink.out`: adapted from `cut.py` (an independent key-level port, not the design's model). Rules are the revised ones: squash passes L when it only deduplicates, cost is the raw sum of the younger blocks against α · |st(s)|, cap is 2α · |st(s)|, the newest block stays. New: the design's 16-position seal beside the model's 64, 1,200-key namespaces where one seal touches a large share of the keys, and counters for the claims at `:134`, `:171`, `:183`.
  - `oracle2.py`, `oracle2-*.out`: adapted from `oracle.py` to whole-block answers, lazy ring eviction (tier 1 reaching below the newest seal), the dedup-passing squash and the cost-and-cap cut. Mutants rerun except `skip-containing`, two new (`whole-untrimmed`, `base-keeps-tombstones`). `b1.py` is not rerun: B1 is gone from the design.
  - `crash.py`, `crash.out`, `crash2.out`: a crash matrix over the stated seal, table and collector order (`:157-161`) in an abstract file model (names, fsynced bytes, directory operations pending until a directory sync, any subset of pending operations surviving a crash). It models the document's protocol, not code. `crash2.out` runs the collector after the next seal's commit, as a background collector would.
  - `packing.py`, `packing.out`: a flag-level toy of the seal-file packing rule, for the last section only.

### Rulings

| Finding | Ruling | Settled by |
| --- | --- | --- |
| NB-L1 | CLOSED, with condition NR-1 | `:109-114,125,165,307`; `model-seal.out`, `seal2.out` |
| NB-L2 | CLOSED, with NR-2 | `:102-107,129,191`; read against `state.hh:25-31`, `state.cc:183`, `node.cc:378` |
| NB-L3 | PARTIAL: the cut is closed, three stated bounds are not | `:131-136,171,183,366`; `model-layout.out`, `cut2-design.out`, `cut2-small.out` |
| NB-L4 | CLOSED, with NR-3 | `:149-169,307`; `crash.out`, `crash2.out` |
| NB-C1 | CLOSED | `:62,307` |
| NB-C2 | CLOSED, with a note | `:171,307` |
| NB-C3 | CLOSED (superseded; the lead confirms the reversal of B1) | `:140,335` |
| NB-C4 | CLOSED, subject to the packing flag below | `:151,155,307` |
| NB-C5 | CLOSED | `:167,191`; `oracle2-none.out` |
| NB-C6 | CLOSED | model `:99,132-135,162-174`; `:136` |

### NB-L1: CLOSED, condition NR-1

The log-progress seal (`:112`) counts index and encoded size at apply, so every entry advances it whatever its outcome (`node.cc:378` applies a NoOp through `advance`), and an empty seal is a checkpoint point (`:125`).

- `model-seal.out` reproduces the table at `:118-123` exactly: 1,152 / 1,152 / 1,152 / 451 / 1,152 / 1,152 entries above the checkpoint with log progress; `LogFull` at entries 65,537, 1,240, 32,769 and 65,537 for the cases the changes-only trigger fails.
- `seal2.out`, the stronger probe: bursty random mixes of positioned and unpositioned entries of 16 B to 1 MiB, 30 seeds of 140,000 entries, instant layout durability: 0 `LogFull`, worst live log 32,773 entries and 39.7 MiB. The same workloads without rule 2: `LogFull` in 30 of 30 seeds, so the probe can see the failure.
- The arithmetic matches: a seal lands within 8,192 entries or 8 MiB plus one entry (at most 1 MiB, `command_bytes`), a checkpoint falls due at half the limits, so the log peaks near 32,768 entries / 40 MiB against a `LogFull` bound of 65,520 entries (`control_entries` = 16 reserved, `core.h:728`) / 64 MiB.
- The slack for the gate's wait (layout durable after the capture) is therefore about 32,700 entries or 24 MiB of admissions. Past it writes stall on `LogFull` until the layout is durable and the checkpoint lands; that is backpressure, not a wedge, and `:165` says so. `seal2.out`'s D > 0 rows all hit `LogFull` because my generator includes a mode of 1 MiB entries (64 of them fill the log); they show only that the slack is in bytes.

**NR-1 (condition).** The thresholds are constants (`:112`, "one eighth of consensus's `log_entries` and `log_bytes`", and the policy list at `:105`), not derived from the limits in force. `consensus.log_entries` and `log_bytes` are configuration (`node.cc:24-25`, checkpoint due at half of them, `:512-513`; `validate_retained_state` ties them only to the retained-state bounds). `seal2.out`, thresholds fixed at 8,192 entries / 8 MiB: `log_entries` 8,192 gives `LogFull` in 14 of 20 seeds, 4,096 in 16 of 20, `log_bytes` 16 MiB in 2 of 20 and 4 MiB in 20 of 20; one eighth of the limits in force: 0 of 20 in every row. The reviewed failure returns for any operator who lowers a consensus limit below eight times the policy. Fix: derive the progress thresholds from the consensus limits in force (or refuse a configuration where `8 * progress > limit`), and say so at `:105`.

### NB-L2: CLOSED, note NR-2

Every seal, squash and cut input is now canonical or replicated; I found no clock, memory budget, arena size or file-system quantity feeding them.

1. Sizes are canonical encoded bytes and the live size is counted at apply (`:100,104`); the ring's `storage_capacity()` is out of the path.
2. The changes a seal writes come from apply's own accumulator, not the ring (`:106`), so a capture gap (`change_history.cc:25-28`) costs ring hits only. The accumulator is bounded by the live log (checkpoints wait for the seal). T4(1) injects a capture failure on every voter at one position (`:307`).
3. The first base is taken at a log-defined index, with the persistent root retained until it is durable (`:107`); a namespace created later starts empty at 0.
4. Seal thresholds, ratio, L, α and the cap are in `Limits`, carried in each seal file and changed only by a replicated entry at one log index (`:105`); `Limits` is already in `semantic_profile` (`state.cc:183-186`), and recovery rejects a changed profile (`docs/SCALE_TARGETS.md:48`).
5. The only local quantities left are the table's "files it would free exceed 64 MiB" (`:160`) and the ring and full-copy caches. The first decides when `layout` is published, which changes how many seal files recovery chains and nothing about any block; the others change answers' composition, never their bytes. Fine.

**NR-2 (note, three one-liners).**
1. α is fractional (0.5, 1, 2 in the tables). `Limits` holds integers (`state.hh:25-31`, `write_limits` uses `w.number`); state α as a ratio of integers or integer thousandths so L and the cost test are exact on every platform.
2. The "set limits" entry the policy needs does not exist (`grep` finds it only in `docs/SCALE_TARGETS.md:59` and the design); S5's dependencies (`:400`) do not list it, and T4(1) "with a policy change mid-run" and T9's "α enters the policy" need it. Until it exists, changing α means exporting into a new authority (`SCALE_TARGETS.md:48`). Add it to S5.2's prerequisites.
3. The seal-file packing thresholds (1 MiB inline, half live, `:151`) are not in the replicated list at `:105`. They decide placement only, never block bytes, so correctness holds either way; but "the layout at any seal is a function of the layout at W, the seal files above W and the policy" (`:157`) is only true of placement if they are replicated, or if recovery verifies placement by digest and never by recomputation. State which.

### NB-L3: PARTIAL

**Closed: the cost rule decides every cut.** By construction the cap fires only when the oldest block alone exceeds α · |st(s)| (`:134`: with younger < α · |st| the cap needs oldest > α · |st|). The runs agree:

- `model-layout.out` reproduces the table at `:175-181` row for row (moderate 33.1-42.1 days at α = 1, disk 2.87 C (4.12 C), sent 1.25x (1.47x); key sets 78-95K / 53-65K / 47-59K / 46-57K positions; bulk 1.09x (1.29x); mass delete 4,560-5,408 positions, 1.16x (1.44x)) and the 1/α statements at `:183,366` (copy / answer below the cut 0.99-1.02 at α = 1, worst 1.98 and 2.01 at 0.5, 0.49 at 2). Cuts by cap: 0 in all 16 runs.
- `cut2-design.out`, the independent port, at the model's 64-position seal and the design's 16: cuts by cap 0 in all 16 runs (8 shapes at each seal size); copy / answer below the cut at most 1.03; answer above the cut at most 1.16 times the copy; disk peak 2.25-2.66 C for the shapes that cut, 3.2-3.8 C in a cut (moderate: 2.87 C in the design's model against 2.65 C here, so the design's figure is the higher). The seal size does not move any of it.
- The shapes the first review used to break the old rule (copy at 3.34, 2.23, 1.83, 1.57 and 1.59 times the answer) now give 1.02, 0.99, 0.95 and 0.91 at the port (bulk load 0.99 in the model; the port samples no store below its cut). Merges that deduplicate pass L as written.

**Open: the stated bounds are not honest outside the model's shape.** `cut2-small.out` runs 1,200-key namespaces (the 5M corpus over 4,096 namespaces averages about 1,200 keys), with a seal touching 60 to 2,000 positions of the namespace and α = 1:

- Uniform rewrites and the moderate skew: cost makes every cut (cap 0), answers stay at or below 0.91 α · |st|. Fine.
- A 90% mass delete: the cap fires (once in the 600-position run), blocks after the cut loop reach 1.64x and 3.44x the 2α · |st| cap (the newest block is never cut, and its tombstones outweigh the shrunken live size), and an answer above the cut reaches 1.98 and 2.00 times α · |st(s)|, which is 2.0 times the copy.
- `cut2-shrink.out`: a quiet 1,200-key namespace after a 90% delete holds one block of 0.344 C, 1.7 times its own cap, indefinitely.

Statements that need to change (wording and one table row, no mechanism):

1. `:134` "a hard cap on what a namespace keeps" and `:171` "the blocks (below 2α · |st(s)| by the cap)": true only apart from the newest block, which `:136` already exempts. Say "apart from the newest block".
2. `:171` "so at most about 1.25 α · |st(s)|": holds only while the oldest block is at most L. Measured 1.98 and 2.00 at small namespaces after a mass delete, 1.30 for a 60-position seal. The cap's honest ceiling is 2α · |st(s)|; say that, and the 1.25 as the typical case.
3. `:183` and `:366` quote "no answer exceeded 1.15 times the copy" and "0.91 after the mass delete" for the model's shapes; qualify them as the 40,000-key model, and add a small-namespace row (a 1,200-key namespace, a seal touching half of its keys, a 90% delete) to T4(8), which today scales to 100K keys only.
4. `:171` "a block is at most the larger of L and one seal's changes": L falls when the live size falls, so an older block can exceed the current L (the mass delete above). Say "when it was built".

These are edits to text and T4(8); the PARTIAL closes when they are in. No architect pass is needed for them.

### NB-L4: CLOSED, note NR-3

I looked for a crash point where a durable `layout` reaches a missing or unsynced file and did not find one in the stated order (`:157-161`):

- Every file (seal file, large output) is fsynced, linked, and the directory synced before the checkpoint is persisted or the table can name it (`:159,165`); `layout` is tmp, fsync, rename, directory sync (`:160`); the collector runs only after that directory sync and only on files at or below W the table does not reach (`:161`). Recovery recomputes from W and the chained seal files and rebuilds an unreached output from inputs the collector has not removed (`:163`).
- `crash.out`: 0 violations in 400 scenarios (14 seals each, large outputs replacing earlier ones, copy-forward, tables every second seal, crashes after every primitive with random subsets of pending directory operations surviving). Mutants die 400 of 400: collector before the table's directory sync (224 reach-a-missing-file, 176 chain gaps), a large output linked and never fsynced (400), a table renamed before its fsync (400), no directory sync before the checkpoint (400). `collect-all` (unlink anything not reached, not only at or below W) survived the sequential model; in `crash2.out`, where the collector runs after the next seal's commit, the stated order still has 0 of 400 and `collect-all` dies 400 of 400, so "at or below W" is load-bearing and the model sees it.
- The overlap case in `:163` (squash or cut outputs beside their inputs after a crash) is covered by the same matrix: an output without a durable name is rebuilt from inputs the table still reaches.

**NR-3 (note, spec gaps, none a defect in the order).**
1. Recovery starts from `layout`; before the first table exists there is no W. Say that the chain then starts at the enabling entry's first seal and the first bases are found by name.
2. A large output's digest is not in the seal file (`:151` indexes sections, `:152` names files), so for an output above W recovery can rebuild it but cannot verify a surviving file by digest without recomputing it. List large outputs with their digests in the seal file that produced them.
3. A seal file that rots after its journal span was compacted cannot be rebuilt from the journal. State the repair: fetch it by name from a peer, or install.
4. `:169` says an install folds none of its own seals at or below W; recovery needs the same rule for the journal replay from a checkpoint older than the table's W (`:163,165`), or replay re-folds seals already in the table.
5. The install's order against the consensus snapshot acknowledgement is not stated: files and table durable first, then the install is acknowledged. T4(5) covers "an install" in its matrix, so the test will catch it.

### NB-C1 to NB-C6

- **NB-C1: CLOSED.** `:62` starts each namespace's counter at its stored revision (`namespaces_`, `state.cc:1002`, written on every applied change, so at least every record's revision) and keeps `paged_read_state.cc:26-28` and `reader.cc:58-61` true; a pre-S5 store takes one full copy per namespace, and T3 asserts it passes the watermark check after (`:307`). Citations checked against the code.
- **NB-C2: CLOSED, note.** `:171` and T4(3) count the base rewrite, the squashed inputs until the next table and one cached full copy per namespace per server. The figures in the table at `:175-181` are base plus blocks plus the cut transient only; they exclude the copy cache (up to 1 C) and the inputs awaiting a table. Add one line saying so.
- **NB-C3: CLOSED as superseded.** Whole persisted blocks replace B1 (`:140,335`): nothing is merged or cached per R. The paragraph's comparison is fair ("both read the same blocks"; `model-layout.out` reads 2.33x the missing bytes on average, worst 6.93x, moderate, α = 1). This reverses the lead's B1 of 2026-11-04, so the lead confirms it.
- **NB-C4: CLOSED**, subject to the packing flag. One seal file per group seal, about one file a second against about 1,611 (`:155`), and T4(7) records files and flushes per seal at 4,096 namespaces (`:307`).
- **NB-C5: CLOSED.** Lazy eviction at the bound and only for covered entries (`:167`); D7 qualifies the edge claim and has a late relay fetch the layout (`:191`). `oracle2-none.out`: 825,955 answers (585,088 copy, 116,837 block, 124,030 ring, the ring reaching below the newest seal through lazy eviction) and 0 failures.
- **NB-C6: CLOSED.** The model seals by size alone (no hour cap, `seal_run` and `Namespace.seal`), keeps at least one block (`:166`, "the newest block is never cut", `:136`), and compares with the live size (`budget`, `:132-135`).

### The overlap proof, rerun on the revised design

`oracle2-none.out`: 0 failures in 825,955 answers with whole-block answers, a dedup-passing squash, the cost-and-cap cut and a lazy ring. Mutants: older record wins 196,667 failures, blocks without tombstones 183,236, an unchanged op at the transaction's position 163,134, incarnation ignored 6,799. The mutant that skips the block holding R + 1 crashed my first adaptation (an index past the last block), and its rerun stayed queued behind other agents' holds; it is not rerun (it died with 92,636 failures against the earlier design, and the answer path that it breaks is unchanged). Survivors, as before and by the proof at `:142-147`: `filter-ge`, `whole-untrimmed` (R's block served untrimmed) and `base-keeps-tombstones` (0 failures each).

### New findings

- **NR-1** (condition on NB-L1): derive or validate the progress thresholds against the consensus limits in force.
- **NR-2** (note on NB-L2): α as a ratio of integers; "set limits" is a missing S5.2 prerequisite; the packing thresholds are outside the replicated policy list.
- **NR-3** (note on NB-L4): five spec gaps in recovery and install, listed above.
- **NR-4: the seal-file packing mechanism needs the architect.** Copying live sections forward from seal files under half live, the 1 MiB inline threshold and the 64 MiB table trigger (`:151,160-161`) are new since the review and the model does not cover them: `layout` simulates one namespace and no files. A flag-level toy (`packing.out`: 4,096 namespaces, 2,048 transactions per seal on a Zipf weight, ratio-4 ladders, no dedup, no cut, a table every 40 seals) puts the copy-forward at 0.54 to 0.80 times the level-0 plus squash bytes written (Zipf 1.3 to 0.8), with 31 to 46% of the retained seal-file bytes dead. Nothing in `:171-185` counts either: the written-bytes figures (1.52 C a day moderate) exclude copy-forward and the disk bound excludes dead sections in files not yet under half. The toy is crude (no dedup or cut, so its absolute numbers are not the design's), but it says the mechanism costs the same order as the squash itself, determinism of placement rests on file structure (an install must fetch reached files whole, `:169`; NR-2.3), and it adds a second garbage collector beside the table and collector. It needs a short architect pass (a model with dedup and cut, a write-amplification and dead-bytes bound, and the placement-determinism rule) before S5.2 builds it. It does not block the other rulings.

### Not run

Detent builds, tests and sanitizers (the branch changes docs only); `detentd`, relay or agent timings; T9; Linux; real crashes or power cuts (the crash matrix is a model of the document's order); a real consensus `LogFull` (the probe replays `core.h:728`); the 5M corpus; any benchmark; `quiet exclusive`. The original `seal.py`, `cut.py`, `b1.py` and `oracle.py` were not rerun unchanged: each is superseded by its adapted version above, and `b1.py` tests a call the design dropped.

## Architect pass: the roll-up (NR-4), 2026-11-04

- Date: 2026-11-04.
- Reviewed: `docs/design/namespace-delivery.md` D6 "Roll-up", "Squash", "Cut", "Files", "Commit", "Recovery", "Checkpoints and the gate", "Install", "Bounds" and the `files` paragraph (`:130-190`), D7 (`:196`), D11 (`:255-263`), Limit 8 (`:381`), at `Detent:245e67e` (the fold is `Detent:59a024e`); `docs/design/namespace-delivery-blocks-model.py` (`files`, `layout`); `.scratch/blocks-fold/files.out`, `layout2.out`; `completion:ab0f551` `docs/PLATFORM_FINDINGS.md` §1 and `docs/mechanisms/sync-files.md`.
- Checked against: NR-4 above; P1 and the owner's principle (a client pays only for what it is missing; time, CPU, disk and writes count, not only bytes); NB-8 and NB-C4; D11's per-platform costs.
- Checked by: probes in `.scratch/rollup-arch/` of this worktree, all `python3 -I` under `quiet light` at `nice -n 19` (the design's model is imported unchanged, never edited):
  - `crash3.py`, `crash3.out`: crash matrix of the roll-up, adapted from `.scratch/blocks-recheck/crash.py` (same disk model: unsynced bytes lost, directory operations pending until a directory sync, any subset surviving a crash). 4 namespaces, 24 seals, roll-ups at period seals and on a random L trigger, outputs superseding blocks and bases, a checkpoint persisted after every seal (the journal compacted at once, the worst case), a table every 4th seal, a background collector with the design's rule (`:164`). 400 seeds per variant, plus a determinism check of the seal ordinal across a restart.
  - `shapes.py`, `shapes.out`, `shapes2.py`, `shapes2.out`: the design's `files` workload (4,096 namespaces of 1,220 keys, Zipf 0.8 / 1.0 / 1.3, 2,048 transactions a seal, 800 seals) with the design's `Group` and `settle`, adding seven layouts (below). Unlike the model, they sample files and dead bytes at every seal, before the collector, and record the largest single sync group.
  - `wire.py`, `wire.out`: the design's `layout` simulation (α = 1; moderate, hot-heavy, 0.30 C key set) with stores also 2, 8, 16 and 32 seals behind, for the roll-up as designed, a roll-up at every seal, earlier roll-ups on repeats (`dup-2`, `dup-1.5`), and level-0 merged at answer time (`serve-merge`). `wire2.py`: the same at a roll-up period of 64. It was still queued behind the host's load when this pass was committed; `wire2.out` lands beside it when it runs, and the re-check reads it.
  - The design's numbers reproduce: `shapes.out`'s `design` rows give 2.95 / 2.51 / 1.84 times the raw changes against `files.out`'s 2.95 / 2.51 / 1.84.
  - Not run: any build, test or benchmark; `completion_sync_probe` or any fsync timing (the sync figures below are estimates from first principles and completion's published measurements); Linux; real crashes.

### Verdict

**APPROVE WITH CONDITIONS: the roll-up is the right replacement for packing (a third of the writes, no copy-forward, no second collector) and its commit order holds under the crash matrix, but its table and collector are tied to "every 16th seal" while the period is policy, its seal ordinal is undefined, and its file count costs one flush per file on Linux, which the design does not state.**

What holds, and reproduces:

- The commit order (`:162-164`): `crash3.out`, 0 of 400 scenarios violate either invariant (a durable table reaches only present, synced files; at a persisted checkpoint every level-0 section of the layout is in a present seal file, the chain above W is complete, and every block and base is present or rebuildable). Mutants die: a roll-up output never fsynced (400 of 400), the table published before the outputs it reaches are durable (400 of 400), a collector that unlinks superseded files above W (242 of 400).
- Writes and dead bytes: 1.84-2.95 times the raw changes against 3.21-5.38 for any shape that copies live sections forward (`shapes.out`, `packcf`). Dead bytes sampled at every seal: at most 1.3-2.0%, mean 0.2% (`design` rows).
- Determinism of the outputs: every roll-up input is a replicated quantity (sections, |st(s)| at the seal, the policy), and an output's name fixes its bytes (block [a, b] of N is each key's last write in [a, b]; base p is st(p)), so a rebuilt or re-fetched output can be checked by digest.

What the conditions fix: RU-1 (the seal ordinal), RU-2 (the table at a period seal), RU-4 and RU-5 (files and flushes), RU-6 (the wire claim), RU-7 (wording).

### Findings

**RU-1. The roll-up seal ordinal is not defined (D6 "Roll-up", "Files", "Install").**
A namespace rolls up "at every 16th seal" (`:130`), but a seal is named by its log index (`seal-<index>`, `:154`) and nothing counts seals: not apply's state, the checkpoint, the seal file or the table. A voter that counts from its own restart rolls up at other seals than its peers: `crash3.out`, ordinal counted from the restart, 297 of 400 restarts differ; restored from the checkpoint, 0 of 400. An installed voter has the same problem. Fix: apply's state carries the seal ordinal beside the seal trigger's counters, so the checkpoint and the snapshot carry it; each seal file and the table carry the ordinal; a period change takes effect at the next period seal.

**RU-2. The table and the collector assume the period is 16; the period is policy (`:106,130` against `:156,163-164`).**
"Files" and "Commit" put the table "at every 16th seal" and let the collector unlink "every seal file at or below W", on the premise that the roll-up at W consumed them all. The roll-up period is a policy integer that "set limits" can change. With the period at 32 and the table still at every 16th seal, a table lands where some namespaces have not rolled up, and the collector unlinks seal files holding their level-0 sections under a persisted checkpoint whose journal is compacted: `crash3.out` `period-change`, 400 of 400 lose a section. Either fix alone closes it: the table only at period seals (`table-at-rollup`, 0 of 400), or a collector that unlinks a seal file only when the table's layout reaches none of its sections (`period-change+consumed`, 0 of 400). Do both: the first is the rule, the second makes the collector check what it deletes instead of assuming it. The earlier draft (`.scratch/blocks-fold/files.md`) said "a roll-up seal W"; the committed text lost it.

**RU-3. The outputs need not gate the checkpoint (note, a relaxation).**
`crash3.out` `outputs-after-checkpoint` survives (0 of 400): roll-up outputs written and synced after the checkpoint is persisted break nothing, because their inputs (seal files above W, blocks the table reaches) stay until a durable table no longer reaches them, and recovery rebuilds a missing output (`:166`). What outputs need is to be durable before the table that reaches them (`table-before-outputs` dies 400 of 400). So the gate can be the seal file alone. The ~2,000-3,900 output syncs then leave the checkpoint's path, can be paced across the period before the next table, and an output superseded before any table (an L-triggered roll-up merged at the next one) never needs a sync. Optional. If taken, T4(5) adds a crash between the checkpoint and the table, with outputs missing.

**RU-4. The file count costs one device flush per file on Linux; the design implies one per group (D6 "Files", `:162,190`; D11 `:259`; Limit 8).**
The roll-up creates 1,908 / 3,398 / 3,900 files per period seal at Zipf 1.3 / 1.0 / 0.8 (largest single group 1,946 / 3,460 / 3,943; `shapes.out`), averaging 0.9 to 1.2 KB each: 2.3 to 3.5 MB per roll-up spread over that many inodes. Between tables 13,300-16,000 files are on disk; "about 12,000" is the count after the collector. `:162` writes them "as one D11 sync group", which reads as one flush. That holds on macOS only. Completion's `SyncFiles` on Linux is `sync_file_range` on every file, then `fsync` on each, then the directory (`sync-files.md`, "N+1 flushes, nothing amortized (unmeasured)"), and D11 and Limit 8 say so for publication. On ext4 the first fsync commits the jbd2 transaction holding every create (one flush with FUA). Each later fsync finds its transaction already committed and issues its own cache flush (`ext4_sync_file`, `needs_barrier`), so a roll-up sends about 3,900 flush commands.

Estimates per roll-up (first principles, not measured):
- ext4: completion's own 9-file batch is 9.5 ms in a VM whose flush reaches the host's `F_FULLFSYNC`, about 1 ms a file, so about 4 s per roll-up, a quarter of a 16 s period at T8's rate. On bare-metal NVMe with a volatile cache, at 50-500 µs per empty flush, 0.2-2 s. On a device without a volatile cache (power-loss protection, and likely cloud block volumes; unverified), the kernel drops the flushes and it is about 0.1-0.3 s of syscalls.
- APFS: `fsync` each, then one `F_FULLFSYNC`. Completion's 9-file batches (46 ms on x86, 12 ms on M1, against 214 and 45 ms for a full flush each) put a plain fsync at roughly 0.8-2.4 ms for 200 KB files; at 0.2-1 ms for 1 KB files that is 0.8-4 s per roll-up.
- The same 2.3-3.5 MB written as one file with one sync would take about 10 ms. The per-file cost is two to three orders of magnitude above the byte cost.
- A full relay mirrors it (D7, `:196`): about 3,900 new names to fetch per roll-up, beside the per-seal sections.

What to measure (T4(7), before S5.2 fixes the shape): `completion_sync_probe` with N = 256, 1,024 and 4,096 files of 1 KiB, `sync_file_range` then fsync each, concurrent fsync threads and `syncfs`, on ext4 on bare-metal NVMe, on a cloud volume, and on APFS (x86 and arm64). Record the wall per group and the flush count from the block layer (`/sys/block/*/stat` flush column, or `blktrace`). Fix: `:162` and `:190` state the per-roll-up file and flush count per platform, and RU-5 picks a shape.

**RU-5. A shape with fewer files: the shared roll-up file does not die whole; a bounded carry and a staggered, longer period do (probe 3).**
`shapes.out` and `shapes2.out`, 4,096 namespaces, Zipf 0.8 / 1.0 / 1.3:

| Shape | Written / raw changes | Files per seal (largest group) | Dead share, every seal: max |
| --- | --- | --- | --- |
| Design: own file per output, period 16 | 2.95 / 2.51 / 1.84 | 245 / 214 / 122 (3,943 / 3,460 / 1,946) | 2.0 / 1.8 / 1.3% |
| One file per roll-up for all its outputs | 2.95 / 2.51 / 1.84 | 1.2-1.5 (2) | 32 / 27 / 18% |
| The same, copying live members of files under half live | 5.38 / 4.52 / 3.21 | 1.2-1.5 (2) | 7.7 / 5.7 / 4.3% |
| Outputs under 4 KiB in a shared file, copy-forward at half | 4.89 / 3.89 / 2.43 | 9.0 / 8.0 / 6.3 (162 / 117 / 73) | 5.0 / 3.8 / 2.5% |
| Staggered: N rolls up when (seal + N) mod 16 = 0 | 2.93 / 2.49 / 1.84 | 245 / 214 / 122 (255 / 230 / 139) | 2.3 / 1.9 / 1.5% |
| Carry, at most 2 periods: an output under 4 KiB of a namespace touched this period stays one section of the period's seal file | 2.71 / 2.36 / 1.78 | 86 / 85 / 65 (2,635 / 2,000 / 1,300) | 2.8 / 2.2 / 1.6% |
| Carry without the 2-period bound | 4.19 / 3.18 / 2.07 | 16 / 37 / 48 | 3.0 / 2.3 / 1.6% |
| Staggered, period 64 | 2.03 / 1.85 / 1.52 | 66 / 67 / 55 (71 / 75 / 67) | 2.6 / 2.4 / 2.0% |

The brief's shape, one file for all outputs of one roll-up, fails because those outputs do not die together. The ratio ladder keeps a namespace's older rungs for many periods, and a namespace that is not touched does not roll up, so its block pins the file: 18-32% dead. Copying the live members forward brings back packing's writes. The file count of the design is mostly cold namespaces writing a block of a few records each period. Three shapes keep the writes and the near-zero dead share:

1. **Stagger** (same writes, same files, bursts of 139-255 instead of 1,946-3,943). Seal files still die whole, 15 seals later; the table and collector follow RU-2's consumption check. It removes the burst, not the flushes.
2. **Bounded carry** (1.9-2.9 times fewer files, 2-8% fewer bytes written, dead at most 2.8%). A namespace whose rolled-up level-0 is under m bytes (4 KiB here; 16 KiB gives the same, `carry1-16KiB`) is written back as one section of the period's seal file, at most twice in a row, then rolls out. Seal files still die whole, one period later. Unbounded, the carry re-copies a slowly growing section every period and writes more (`carry-4KiB`, 4.19 times).
3. **Period 64, staggered** (the most efficient: 1.52-2.03 times written, 55-67 files a seal, no burst above 75). Level-0 then holds up to 64 seals, bounded by L through the L trigger, which raises the wire cost of repeats (RU-6; `wire2.py` measures it, not yet run).

Each is placement by replicated policy alone (the namespace id, m, the period), so NR-2.3 holds. My recommendation: stagger plus the bounded carry, with the period left at 16 until RU-6's choice and T4(7)'s Linux numbers decide whether 64 pays. None of them is required for correctness. The condition is that the design picks one or states why the per-file count stands, with T4(7)'s numbers.

**RU-6. The hot-key wire cost of level-0 is bounded, but the stated worst understates it (probe 4).**
Level-0 sections repeat a key once per section that wrote it, at most period sections, and their bytes stay below L plus one seal (the L trigger). So the excess over the deduplicated answer is at most about α · |st(s)| / 4, and answers stay below the copy (`wire.out`, worst sent / copy 0.59 hot-heavy, 0.99 moderate). The ratio to the missing bytes is what grows. With stores 2-32 seals behind, which the design's sample (1 to 182 days) never takes, `wire.out` gives:

| α = 1 | Sent / missing, mean (worst) | Roll-ups per 1,000 seals | Written per day |
| --- | --- | --- | --- |
| Hot-heavy, design | 1.54 (3.21) | 62 | 3.59 C |
| Hot-heavy, roll-up every seal | 1.29 (1.99) | 1,000 | 9.82 C |
| Hot-heavy, also roll up at 2x repeats | 1.39 (3.07) | 125 | 4.33 C |
| Hot-heavy, also at 1.5x repeats | 1.43 (2.51) | 184 | 5.01 C |
| Hot-heavy, level-0 merged at answer time | 1.32 (2.31) | 62 | 3.59 C |
| Moderate, design | 1.31 (1.96) | 62 | 0.68 C |
| Moderate, level-0 merged at answer time | 1.22 (1.56) | 62 | 0.68 C |

The 0.30 C key set does not move (1.36-1.37 mean, 2.87 worst in every variant): its excess is the ladder, not level-0.

So the worst is 3.21 times, not 2.17 (`:188`). Rolling up earlier on repeats buys little (worst 3.07 or 2.51) for 21-40% more writes and two to three times the files of hot namespaces. Merging the level-0 sections at answer time recovers most of what a per-seal roll-up gave, with no extra write or file. It is a k-way merge of at most period sections, below L plus one seal, recent and in the page cache. It departs from "nothing is merged per answer" (`:140`), but the merge is per namespace and starting section (at most period variants, each below L), not per R, so the server can hold the merged suffixes in memory and rebuild them at each seal. The trade is server CPU and memory against wire bytes, under P1, and it is the lead's or the owner's call. Options: (a) keep sections whole and restate the bound (3.21 worst at 2-32 seals behind); (b) merge level-0 at answer time, the suffixes cached per seal; (c) roll up on repeats (not recommended: the writes and files cost more than it saves). Recommendation: (b). It also makes period 64 (RU-5.3) affordable on the wire.

**RU-7. Wording (no mechanism).**
1. `:190` "leaves no dead bytes" and the ledger's "no dead bytes" (`:464`): measured after the collector; at every seal, at most 1.3-2.0%. Say "under 2%".
2. `:190` "about 11,500 to 12,200 files on disk": 13,300-16,000 between tables.
3. `:162` "as one D11 sync group": add "one flush on macOS; on Linux one flush per file" (RU-4).
4. `:188` "puts the hot-heavy worst at 2.17 times": 3.21 for stores 2-32 seals behind (RU-6), and T4(8) samples those ages.
5. `:130` "level-0 holds at most 16 seals of changes": with a carry (RU-5.2) it holds at most the period or m bytes; state whichever shape is chosen.

### Conditions

- RU-C1 (RU-1): the seal ordinal is in apply's state, the checkpoint, the snapshot, each seal file and the table; T4(1) restarts and installs a voter mid-period and compares seal files byte for byte.
- RU-C2 (RU-2): the table is published only at period seals, and the collector unlinks a seal file only when the table reaches none of its sections; T4(5) changes the period mid-run in the crash matrix.
- RU-C3 (RU-4, RU-5): `:162,190` state files and flushes per roll-up per platform; the design adopts stagger plus a bounded carry (or period 64), or keeps one file per output with T4(7)'s Linux and APFS sync measurements as the reason; T4(7) runs `completion_sync_probe` at 256-4,096 files of 1 KiB on ext4 (bare-metal NVMe and a cloud volume) and APFS.
- RU-C4 (RU-6): the owner or lead picks (a), (b) or (c); the design restates the hot-key worst at stores 2-32 seals behind, and T4(8) samples those ages.
- RU-C5 (RU-7): the five wording fixes.

No blocking items. RU-2 loses data only through a policy change, which needs the "set limits" entry that S5.2 has yet to build, so RU-C2 lands in the text before S5.2's code does. RU-3 is optional.

### Not run

Any build, test, sanitizer or benchmark; `completion_sync_probe` or any timing of fsync, `F_FULLFSYNC` or `syncfs` (RU-4's figures are estimates); `wire2.py` (period 64 on the wire; queued, not finished at commit); Linux; real crashes or power cuts (the crash matrix models the document's order, not code); relay fetch counts beyond arithmetic; `quiet exclusive`. The model's ring is the current seal only, so its stores 2 seals behind would be ring hits in a group whose ring covers them; the 8-32-seal stores are not.
