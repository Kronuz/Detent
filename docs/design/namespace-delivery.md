> Status: DRAFT (2026-11-04) Designing (ready for preliminary review of the roll-up). D1, D2 and P1 are the owner's rulings, and the owner ruled D6's mechanism on 2026-11-03: compacted blocks, a cut by cost, the in-memory ring kept as the hot path. The blocks review and its re-check are folded in: seals on log progress derived from consensus's limits, every layout input replicated, a cut by cost over whole-block answers with a separate disk cap and honest bounds, a commit protocol on D8's rules, persisted blocks served whole (the lead confirmed), and a roll-up that consumes seal files whole in place of the seal-file packing (NR-4), which awaits the architect. Earlier revisions answered [the preliminary review and its re-checks](../reviews/namespace-delivery-preliminary.md). Ledger: [status/namespace-design.md](../status/namespace-design.md).

# Namespace delivery

Each namespace has its own position and history, an agent starts from a store shipped in the application image and catches up by fetching only what it is missing, and a restart costs the same at 10K knobs as at 5M.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03.
- Base: `Detent:9b24fb3` for what this revision writes (D3's `service.cc` and starting positions, D6, D7, D8's catch-up input, "Strata fit", W3, W4, W13; the source is unchanged at `Detent:bd52cc3`); the sections it leaves alone keep their `Detent:d4abd4a` citations (`state.cc` and `change_history.*` are identical at both). Other repositories at `completion:576618d` (Detent's pin), `consensus:61149a2` (main; Detent pins `666cdfc`), `strata:9d52824`. Line citations are at those commits.
- Input: the startup attribution, `docs/perf/agent-startup-2026-11-03.md` on branch `explore/agent-startup` (`Detent:e8d4e59`). Every measured number here comes from it (Intel i9-9980HK, macOS 15.8.1, warm page cache), from `benchmarks/system-capacity-5m-20261024.json`, or from the review's probes, which say so. Seal liveness, and sizes of blocks, cuts and answers, come from a key-level simulation of stated shapes, [namespace-delivery-blocks-model.py](namespace-delivery-blocks-model.py), not measurements.
- Depends on: [reader.md](reader.md) D4, whose `change` file shape D8 adopts (RD-5); the lead's L1 ruling, one store directory (ledger); strata Track C D12 and D13 for the group count (D3, T8). Nothing in consensus or strata (H1 is withdrawn, "Strata fit").

| Review | State |
| --- | --- |
| Architect, preliminary | [APPROVE WITH CONDITIONS](../reviews/namespace-delivery-preliminary.md); L1-L7 and C1-C2 folded in (see "Review findings") |
| Architect, re-check | L1-L3, L5, L6 closed; L4 and L7 partial; N1-N6 folded in (see "Review findings") |
| Architect, re-check 2 | N1, N2, N4a, N5, N6 closed; N3 exact but partial; N7-N11 folded in, then superseded by the owner's ruling of 2026-11-03 (D6) |
| Owner ruling, 2026-11-03 | P1 and D6's mechanism; D6, D7, T3, T4, Q2 and the H1 section rewritten; T9 added |
| Architect, blocks preliminary | [REJECT](../reviews/namespace-blocks-preliminary-2026-11-04.md); NB-L1 to NB-L4 and NB-C1 to NB-C6 folded in (see "Review findings") |
| Architect, blocks re-check | NB-L1, L2, L4 and NB-C1 to C6 closed, NB-L3 partial; NR-1 to NR-3 and NB-L3's wording folded in; NR-4 answered with the roll-up, which awaits the architect |

The design covers namespace-granular delivery, agents seeded from an image shipped with the application, and fast agent restart. Classic mode and flat-v1 are being deleted (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`), so it covers the authority, relay and managed agent only.

## Decisions settled by the owner

**D1. The namespace is the one unit.** A namespace is the unit of atomic writes, consistent reads, subscription, revision, cursor, delta retention, relay caching and consensus-group placement. Detent offers no read consistency across namespaces: a client sees each namespace as a consistent snapshot that never goes backwards, independently of the others. Knobs that must change together, or be read as of one moment, belong in one namespace. Ruled 2026-11-03.

**D2. All namespaces by default.** An agent or client that names no namespaces follows every namespace its credential allows, including namespaces created later. A namespace list narrows the subscription; it is never required.

**P1. A client pays only for what it is missing.** An agent with a store of any age gets the changes its store lacks, never the whole dataset, as long as those changes cost less than a fresh copy; a full copy is for a client with no store, or one so far behind that the copy is cheaper ("why buy a full house instead of repairing the one I have?"). That is what makes catch-up fast and scalable. The mechanism is the owner's (ruled 2026-11-03): persist compacted blocks per namespace, sealed by size, squashed coarser as history ages and grows, each holding the last write per key and a tombstone per deleted key; keep the in-memory ring as the hot path, since most stores are recent; cut history where serving it costs more than the full copy. D6 sizes it.

How each decision serves P1:

| Decision | What a client pays |
| --- | --- |
| D3 Positions per namespace | What it misses in one namespace is a contiguous range of that namespace's positions, independent of other namespaces' writes |
| D6 Ring, blocks, base | Each changed key once, at its last value, since its store's position; a full copy only below the cut, where it is cheaper |
| D7 Relays | The same answers from a nearby relay; a fleet shares each upstream fetch |
| D8, D11 Store and barrier | A catch-up publishes the changed pages and syncs once |
| D9 Seeding | A seeded store is a store: it catches up with what it lacks |
| D10 Restart | No rebuild and no full read of a store the agent already trusts |

## Abstract

Today one cursor covers a whole subscription (`include/detent/cursor.hh:8-13`), and the history that could serve a delta lives in memory: 4,096 transactions on the authority (`src/detent_authority/change_history.hh:8-10`, reset at `change_history.cc:364`), 64 exact-base edges on the relay (`include/detent_relay/configuration.hh:10`). An agent seeded from a week-old image gets a full snapshot, **722.6 s** at 5M knobs, and a restart takes **118-122 s**.

Each namespace's position is its own counter, assigned at apply, so cursors, frames, the store, relays and images do not depend on how many consensus groups there are (D3); the authority stays one group until strata's multi-group work exists and T8 is measured on Linux. History has three tiers per namespace (D6): today's in-memory ring of exact entries, compacted blocks below it, and a base that is the namespace's state at the cut. An agent inside the ring gets entries; one below it gets one block holding each key's last change since its position; one below the cut, or with no store, gets the full copy. Every voter builds the same blocks from apply's output, relays mirror them (D7), and the checkpoint is taken at a sealed position, so a restart loses no history and consensus needs no change. The agent keeps one prepared generation per namespace under one table in one directory (D8), trusts what it prepared or was seeded with while checking pages on first touch (D10), and replaces per-file flushes with one barrier (D11). The gate rows ask for a restart of at most **0.3 s at every scale**, half today's bootstrap, and catch-up bytes no larger than the changes missed. Nothing here is built or measured yet.

## What the measurements say

| Cost (5M) | Cause | Decision |
| --- | --- | --- |
| Restart, 52 s hashing | Recovery hashes every byte six times (`paged_dependency_discovery.cc:117-131`, `paged_page_validator.cc:74-131`, `paged_graph_validator.cc:201-221,266`) | D10 |
| Restart, 55 s sync | The startup census (`StartupOperation`, `src/detent_local/disk_store.cc:71`) syncs every file (`:133`, `:171`); completion's file sync is `F_FULLFSYNC` on macOS (`completion/posix.h:158-165`) | D11 |
| Bootstrap, 239 s sync (33%) | Per-file `F_FULLFSYNC` and directory syncs in relay and agent publication | D11, D12 |
| Bootstrap, 112 s hashing (15%) | Each tier re-validates its own build; the relay re-hashes on recovery | D10, D12 |
| Bootstrap from an old image | Authority history is in memory, empty after restart (built at `service.cc:1470`, attached only after replay at `:1693-1705`); the relay serves exact-base edges only | D6, D7 |
| Scaling | Restart 0.4 / 2.5 / 26 / 120 s, bootstrap 2.7 / 15.8 / 145 / 722 s at 10K / 100K / 1M / 5M (1M interpolated, not measured) | T1-T4 |

## Design

### D3. A position per namespace, on today's group

The authority stays one consensus group (`src/detent_authority/service.cc:1467-1468`). What changes is the position: each namespace counts its own applied changes. Apply already keeps a revision per namespace (`state.cc:1002`), but it stores the global revision of the namespace's last write (`:1001-1003`). It becomes the namespace's own counter, incremented by one for each transaction that applies and changes a key in that namespace (the branch at `:956-1004`). A refused transaction (`:912-990`: unknown or fenced session, retained retry, sequence gap, conflict, capacity) and a transaction that changes nothing (`:1006-1007`) get no position. A record's revision is the namespace position of its last change. At S5.1's enabling entry each namespace's counter starts at its stored revision (`namespaces_`, `:1002`), which is at least every record revision in it, so the reader's checks still hold: no namespace watermark above the cursor (`src/detent_local/paged_read_state.cc:26-28`) and no regression within an incarnation (`reader.cc:58-61`). A store built before S5 has no incarnation in its cursor (W1), so it gets one full copy per namespace, a generation switch (A4) whose records all carry revisions at or below the new position.

```c++
// include/detent/cursor.hh
struct Cursor {
	Identity cluster{}, incarnation{}; // the namespace's; new on restore or re-creation
	std::uint64_t position = 0;        // the namespace's applied-change counter; 0 is empty
};
```

Positions are contiguous, so a consumer proves it missed nothing: a frame starts at R + 1 and each entry is the previous plus one. The incarnation is drawn when the namespace is created and stored in its directory entry (D5). A restore of the group from a backup issues a new incarnation to every namespace it holds, through one administrative entry in the restored group's log so every voter applies the same ones, so a position never names two states under one incarnation. Producer preconditions (`expected_namespace`, a key's `expected` and `absence_revision`, `state.cc:943-951`) keep their meaning, because they already compare within one namespace; they gain the incarnation, and a mismatched incarnation is a `Conflict`. That closes the ABA case a restore, or a future delete and re-create, would otherwise open (ND-R7.3).

D1 makes the namespace the unit of placement, not a group per namespace; `SCALE_TARGETS.md:118` and `SCALING.md:52` plan groups that each own a set of namespaces. The group count is chosen later (S10): bounded groups or one per namespace, once strata Track C2's coalesced heartbeats and quiescence exist (`strata/docs/design/track-c.md:627`, D12) and T8 is measured on Linux. The review's probe puts 4,096 idle groups at 6.3% of a core per voter at 100 ms ticks, before sockets, TLS or the journal (ND-R1), three times T8's 2%. Nothing in D4-D12 depends on that choice. Group state stays Detent's `State` (`state.hh:160-225`) behind the apply, checkpoint and install contract strata's core exposes (`strata/docs/REPLICATION.md:42-45`).

### D4. Authorization per namespace, no scope identity

`canonical_scope_identity()` (`include/detent/scope.hh`) binds one global revision to one namespace set. With per-namespace cursors nothing depends on the set, so the scope identity goes. The authority and relay authorize each namespace of each request against the credential's grants. An empty list means every granted namespace (D2), reversing "an empty request produces an empty scope" (`docs/AUTHORITY_EXPORT.md:7`). Grants name exact names, whole-component prefixes (`payments.*`) and `*` (Q1).

### D5. The namespace directory

The directory is one more stream with its own position: entries (name, incarnation), one position per creation or new incarnation, delivered like a namespace. A producer's first write to an unknown name creates the namespace there, keeping today's implicit creation (`state.cc:959`). The server filters the directory by the requester's grants. An agent following all namespaces holds the directory's cursor; a new entry makes it subscribe from position 0. The authority never deletes a namespace today (its map is only ever set, `state.cc:1002`); if deletion arrives, a re-created name gets a new incarnation (D3).

### D6. History: the ring, compacted blocks, the base

Each namespace's history has three tiers, newest first:

```text
 1 ............... p | blocks above the base, tombstones kept | s | ring entries | h
 base: state at p,     coarser with age, sizes set by the rule     exact, in memory
 no tombstones
```

An answer for a consumer at R comes from the first tier that reaches R:

1. **Ring** (R + 1 still in the ring): the entries (R, h], exact, from memory. Most agents restart near head and never touch disk.
2. **Blocks** (p ≤ R, below the ring): the persisted block holding R + 1, trimmed to the records whose revision is above R, then every younger block whole, then the ring's entries (s, h]. Every piece is a stored file read in order; nothing is merged per answer.
3. **Full copy** (R < p, a store from another incarnation, or no store): the state at s, then the ring's entries (s, h].

**Outcomes from apply.** The ring is today's `ChangeHistory` (`change_history.hh:23-67`): apply hands it each applied transaction's changed operations through `AppliedChangeRecorder` (`change_recorder.hh:18-32`, called at `state.cc:1480`), each key with its full definition or a delete. It keeps its bounds, 4,096 transactions and 16 MiB (`change_history.hh:9`), and is keyed by namespace position. A refused or unchanged transaction has no position and never enters it; positions are contiguous, so a gap is a source failure, never a silent skip. Every operation is absolute (`docs/AUTHORITY_ARCHITECTURE.md:93`, `service.cc:798-810`), so applying the changes after any R is exact. A producer learns its outcome from `/v2/result` and the retained receipts (`state.cc:1009-1014`), as today. Apply also keeps each namespace's live size |st(x)|, the canonical encoded bytes of its records, beside the state's byte total (`bytes_`, `state.cc:963-1000`), and every seal records it.

**Every input is replicated.** Every voter and relay must build the same blocks, so nothing that decides a seal, a squash or a cut is local:

1. **Sizes** are canonical encoded bytes, the block encoding's size of each record, never arena bytes (`storage_capacity()`, `change_history.cc:333-348`, counts the ring's allocator).
2. **The policy** (the change-seal thresholds, the roll-up period, the ratio 4, α, the disk cap) joins the authority's `Limits` (`state.hh:25-31`), which are already part of the replicated semantic profile (`semantic_profile`, `state.cc:183`; `docs/SCALE_TARGETS.md:48`). Every value is an integer, as `Limits` holds them: α is a ratio of two integers, so L = ⌊|st(s)| · α_num / (4 · α_den)⌋ and the cost test, Σ · α_den ≥ α_num · |st(s)|, are exact on every platform. The policy changes only through the online "set limits" entry, applied by every voter at one log index (`SCALE_TARGETS.md:59`), and takes effect at the next seal; that entry does not exist yet, so S5.2 builds it, and until then changing the policy means exporting into a new authority (`SCALE_TARGETS.md:48`). The log-progress thresholds are not policy: they derive from consensus's limits (see "Seal"). Each seal file carries the policy in force, so a reader of the files needs no configuration.
3. **The changes** a seal writes come from apply itself, into a per-seal accumulator owned by apply. The ring's capture can fail under delivery-ledger pressure (`change_history.cc:25-28`; the gap at `state.cc:1481-1486`; that ledger is shared with subscriptions, `service.cc:541-553,1470-1471`), so the ring only serves; a gap there costs ring hits on that voter, never a block. The accumulator holds at most the positioned bytes of the live log, since an unwritten seal holds back the checkpoint and so the log (see "Checkpoints").
4. **The first base** is taken at a log-defined index: S5.1's administrative entry that enables positions, for every namespace then present, and a restore's entry for every new incarnation (D3). Every voter encodes it from its state at that index, whose persistent root (`records_` is a `PersistentMap`, `state.hh:220`) it retains until the base is durable. A namespace created later starts from an empty base at position 0.

**Seal.** Apply seals at an index when, since the previous seal, either:

1. **Changes:** positioned changes reach 2,048 transactions or 8 MiB of canonical bytes, or
2. **Log progress:** the log has advanced one eighth of `log_entries` entries or one eighth of `log_bytes` of encoded entry bytes: 8,192 entries or 8 MiB at consensus's defaults (`consensus/include/consensus/types.h:58-59`).

Both are counted from the entries themselves (index, encoded size, outcome), so every voter seals at the same index and no command is proposed. Rule 2 derives from the group's limits in force (`group_limits()`, `consensus/include/consensus/core.h:163,316-318`), never from a constant, so an operator who lowers a consensus limit lowers the trigger with it (the re-check's `seal2.out`: fixed thresholds reach `LogFull` once a limit falls below eight times them; derived ones never did). Those limits are replicated only when the group's configuration carries them; otherwise each node applies its own (`:316`). So S5.2 sets them in the configuration at the enabling entry, and a node refuses to start, or to keep voting after a configuration change, when its retained bounds (`retained_state.log_entries` and `log_bytes`, `src/detent_authority/node.cc:22-25`) are below the group's: the `LogFull` test takes the smaller of the two (`core.h:726-727`). Rule 2 counts what gets no position: refusals, NoChange, session Open and Close (`state.cc:890-1015`) and leader no-ops, which the applied-through index covers. Without it a group whose entries are mostly unpositioned never seals, never checkpoints, and stops at `LogFull` (`consensus/include/consensus/core.h:725-730`); the model replays the review's cases (`seal` in [the model](namespace-delivery-blocks-model.py)):

| Log mix (the review's NB-1 cases) | Seals on changes only | With log progress |
| --- | --- | --- |
| Every entry a positioned 256 B write | live, 1,152 entries above the checkpoint | live, 1,152 |
| 10% positioned, 90% NoChange | live, 12,440 | live, 1,152 |
| 3% positioned, 97% NoChange or refused | `LogFull` at entry 65,537 | live, 1,152 |
| 20% positioned 4 KiB among refused 64 KiB batches | `LogFull` at entry 1,240 | live, 451 |
| 1% positioned, a producer retrying a Conflict | `LogFull` at entry 32,769 | live, 1,152 |
| No positioned entry, session churn only | `LogFull` at entry 65,537 | live, 1,152 |

A seal with no positioned change writes a seal file with no namespaces; it is a checkpoint point all the same. Time adds nothing a seal needs, and a time trigger would need a proposed command because apply has no clock.

**Block.** A block [a, b] of namespace N holds, for every key written in positions a to b, that key's last write in the range: its full definition, or a tombstone for a delete, with the revision of that write. Records are sorted by key in a canonical encoding (today's delta encoding per record), so the bytes depend only on (N, incarnation, a, b) and the policy: every voter and relay holds the same bytes under that name, with a SHA-256 digest. Blocks are immutable and cacheable by name. A namespace's level-0 block is its section of a seal file; every other block, and every base, is a file of its own (see "Files").

**Roll-up.** A seal writes each touched namespace's changes as a level-0 section of the seal file and squashes nothing. A namespace rolls up at every 16th seal (the policy's roll-up period, an integer) when it has level-0 sections, and at any seal where its level-0 sections reach L: the sections merge into one block, younger record winning, and the squash and cut below run on its blocks in memory, so only the blocks and base that result are written. At every 16th seal every namespace with sections rolls up, so every seal file at or below it is consumed whole: no section outlives its seal file, nothing is copied forward, and level-0 holds at most 16 seals of changes. Between roll-ups the level-0 sections are blocks like any other, served whole.

**Squash.** At each roll-up of a namespace, the two newest blocks above the base merge while the older is less than 4 times the younger and the merged block is either at most L = α · |st(s)| / 4 or no larger than the larger input. A merge is a k-way pass over key-sorted blocks in which the younger record wins; the result is the block over the union of the ranges. The ratio keeps O(log) blocks, fine near head and coarser with age; L keeps the oldest blocks small enough that the cut drops history in steps; the second condition lets a merge that only deduplicates pass L, so key-set churn rewrites one block instead of repeating keys across many.

**Cut.** Then, at the same roll-up, while the namespace has two or more blocks above the base, the oldest joins the base, which drops its tombstones, when either holds:

1. **Cost:** the younger blocks sum to at least α · |st(s)|. A store inside the oldest block would be sent at least those blocks whole, and the full copy is |st(s)|, so the cut sends a copy to a store only when the copy costs at most 1/α of the answer it replaces, when the cut happens.
2. **Disk cap:** all blocks above the base sum to more than 2α · |st(s)|. It bounds what a namespace keeps apart from its newest block, separately from cost; it fires only when the oldest block alone exceeds α · |st(s)|, which in the model happens once, in a small namespace after a mass delete ("Bounds").

The newest block is never cut, so a store just below the ring always gets a block answer; for a namespace whose state is nearly empty, or just shrank, that answer can exceed the copy by up to one block. Because the answer is whole blocks, the cost rule compares what a store would actually be sent with what the copy costs, and comparing with |st(s)|, not the base, keeps a mass delete from sending block answers larger than the shrunken copy. α is 1 until T9 sets it from time, not bytes: a block answer costs the agent apply CPU per record that a full copy's one-pass build may not, so the time parity can land below byte parity.

**The base** is the namespace's state at p, its end: live records with their revisions and no tombstones, since no store below p is answered from blocks. Each cut rewrites it as the merge of the base and the oldest block.

**Answers.** A block answer streams the block holding R + 1 through a revision filter, then sends the younger blocks as stored; nothing is built or cached per R, the younger blocks are the same files for every store, and any HTTP cache can serve them. The full copy at s is the merge of the base and every block, without tombstones; it is built once per s a client asks for, cached by name (N, incarnation, s), one per namespace per server, replaced by a newer one, and counted in the disk bound. The agent applies the pieces in order and publishes one generation (D8), so readers move from R to h with nothing in between.

**The proof.** Write st(x) for the namespace's state at x. Take a ≤ R + 1 ≤ b and apply, to a store at st(R), the records of block [a, b] whose revision is above R. For a key k:

1. Written in (R, b]: since (R, b] is inside [a, b], k's last write in [a, b] is its last write up to b, and it lies above R. The record is applied and sets k to st(b)(k), with st(b)'s revision, or deletes it for a tombstone.
2. Not written in (R, b]: st(b)(k) = st(R)(k), and k's record, if any, has a revision at or below R. It is dropped and the store keeps k.

So the result is st(b) with st(b)'s revisions. Each younger block [b + 1, c] is then the case R = b, in which every record is above R, so it applies whole and yields st(c); the chain ends at st(s). A record dropped for revision ≤ R is exactly a key the store already holds at that value: overlap needs no rewrite, and applying the trimmed block unfiltered is also correct (the review's oracle, 849,481 answers). A delete then a re-insert (the owner's o12 and o22) leaves the re-insert as the last write; a delete then nothing leaves the tombstone; a tombstone for a key created and deleted after R is a delete of a key the store never held, a no-op. Merging blocks [a, b] and [b + 1, c] with the younger record winning gives each key's last write in [a, c], which is block [a, c], so a squash changes no answer. No revision goes backwards (reader.md A4): k's revision in st(b) is its last write up to b, never before its last write up to R. Positions move from R to h in one publication.

**Files.** A voter keeps history in one directory beside the journal, under D8's publication rules:

1. `seal-<index>`, one per group seal: the index, the previous seal's index, the policy in force, then per namespace the seal touched, its level-0 section and |st| at the seal; at a roll-up seal, the name and SHA-256 of every block and base the roll-up wrote. Its bytes are canonical, so every voter writes the same file.
2. `block-<N>-<incarnation>-<a>-<b>` and `base-<N>-<incarnation>-<p>`: one file per block above level 0 and per base.
3. `layout`: the table at a 16th seal W: for every namespace, its base and blocks by name with SHA-256. The level-0 sections above W are in the seal files.

Placement is a function of the replicated policy alone, so "the layout" below covers which file holds every byte, not only the blocks' contents (NR-2.3). One seal writes one seal file, not one file per namespace (about one a second at T8's rate, where per-namespace level-0 files would be about 1,611, NB-8); a roll-up writes one file per namespace that rolls up, plus a base for each cut.

**Commit.** The layout at any seal is a function of the layout at W, the seal files above W and the policy they carry, so files are durable copies of a computed state, and publishing them needs no new rules:

1. **Seal:** the seal file, and at a roll-up seal every block and base it lists, are written under `.tmp-` names, each fsynced, linked to its final name with `linkat` (D8.3; an existing name is verified against its digest, D8.4), and the directory synced, as one D11 sync group, in seal order: seal i + 1 commits after seal i. Nothing is unlinked.
2. **Table:** at every 16th seal, once its layout is durable, `layout` is published as D8 publishes `selected`: `.tmp-`, fsync, rename, directory sync, with W that seal. The first table is published at S5.1's enabling entry, holding the first bases with W at that entry, and no checkpoint above the entry is persisted before it is durable; a namespace created later enters the next table.
3. **Collector:** once a table is durable, it unlinks every seal file at or below W, all of whose sections the roll-up at W consumed, and every block or base file written at or below W that the table does not reach. An input of a squash or cut is never unlinked before a durable table no longer reaches it.

**Recovery.** A voter reads `layout`, which always exists after the enabling entry, then the seal files above W in index order while each names its predecessor and verifies; the chain stops at the first gap. It recomputes the layout seal by seal; an output of a roll-up is found by name and checked against the digest its seal file lists (D8.4), or rebuilt from its inputs, which the collector has not removed. Orphans and `.tmp-` names go (D8.4-5). Entries above the last chained seal are in the journal, because the checkpoint is at or below it, and replay through apply re-seals them at the same indexes. Replay from a checkpoint below W re-seals seals at or below W only to refill the ring and the accumulator; it folds none of them, since the table holds them. A file that fails its digest, at recovery or when an answer reads it, is fetched by name from a peer, which holds the same bytes; one whose journal span is compacted and that no peer holds is recovered by an install.

**Checkpoints and the gate.** The checkpoint is captured in apply at the seal's index, and persisted once the layout at that seal is durable: its seal file and every output the seals up to it produced. Today a due checkpoint captures whatever index is applied when `service_checkpoint_due` runs (`node.cc:519-530`); S5.2 moves the capture to the seal's apply, which the persistent root makes cheap. Consensus compacts only through a checkpoint the host captured (`consensus/ARCHITECTURE.md:105,109`), so the compaction gate is a durable tiling of (p, S] for every namespace at the checkpoint's seal S. Today a checkpoint falls due at half the live log (`node.cc:503-516`); log-progress seals come every eighth of it, so a due checkpoint is captured within an eighth of the log, below the `LogFull` bound. Recovery restores the checkpoint and replays the journal above it, which refills the ring; today's reset after restart (`change_history.cc:364`; history attached only after replay, `service.cc:1693-1705`) goes, so no cursor is lost to a restart. If seal files stall (a full disk), apply keeps running, checkpoints stop, and consensus's backpressure stops writes at the live log's bound before the accumulator or the ring grows past it.

**Ring eviction.** The ring evicts lazily: only at its bound, oldest first, and only entries the durable layout covers. So it keeps up to 4,096 transactions above the blocks, and an agent one seal behind still gets ring entries.

**Install.** A voter installed from a snapshot at seal S fetches the source's `layout` and the files it reaches, then the source's seal files above W and the outputs they list, verifies every digest, and publishes them under the commit above; only then does it acknowledge the install to consensus and persist the installed checkpoint. If the source's W is past S it adopts that layout, folds none of its own seals at or below W, which are the same seals, and answers below its ring only once its apply reaches W. Then it continues as any voter.

**Bounds.** A block is at most L plus one seal's changes, against L when it was built: a namespace rolls up once its level-0 reaches L, and a merge past L grows nothing. L falls with the live size, so after a mass delete an older block can exceed the current L. An answer for a store above the cut is the trimmed oldest block, the younger blocks (below α · |st(s)| at the last roll-up) and the level-0 sections since (below L plus one seal), so typically about 1.25 α · |st(s)|, when the oldest block is at most L. Apart from the newest block, which is never cut, the blocks stay within the cap of 2α · |st(s)|, so an answer reaches at most about 2α · |st(s)| plus the newest block; after a mass delete in a small namespace the newest block alone can exceed the cap (below). Every block carries a key once, so a block answer is never more than the changes counted per transaction. Per namespace, disk is the base, the blocks and level-0 sections, one cached full copy, and what was superseded since the last table: one base rewrite at a cut, and squashed inputs. The key-level model ([namespace-delivery-blocks-model.py](namespace-delivery-blocks-model.py), `layout`) runs these rules over one namespace of 40,000 keys (the 5M corpus at 1/125: 52 B per record, 34 B per tombstone, an estimate), C its starting state, sealing every 64 positions and rolling up every 16 seals, with the churn models scaled to it, the review's shapes, and namespaces of 1,200 keys (the 5M corpus over 4,096 namespaces) whose seals touch about 5% or 40% of their keys:

| Shape (stated, not measured) | Cut reaches back, α = 0.5 / 1 / 2 | Answer / missing bytes above the cut, α = 1: mean (worst) | Worst answer / copy above the cut, α = 1 | Copy / answer just below the cut, α = 1 | Disk peak, α = 1 (during a cut) |
| --- | --- | --- | --- | --- | --- |
| Moderate: 1M key writes a day at 5M, 80% to 1% of keys; 1% creates, 1% deletes | 15-20 d / 32-44 d / 75-102 d | 1.29 (1.58) | 0.99 | 0.99 | 2.84 C (4.09 C) |
| Light: 100K a day, same skew | 125-151 d / about 340 d, one cut in 400 / none in 400 d | 1.32 (1.72) | 0.69 | 0.98 | 2.27 C (3.28 C) |
| Hot-heavy: 10M a day, 99% to 1% of keys; 0.01% creates, 0.01% deletes | 24-30 d / 55-66 d / none in 150 d | 1.39 (2.17) | 0.59 | 0.98 | 2.25 C (3.26 C) |
| Uniform key set of 0.15 C | none in 600,000 positions | 1.16 (1.33) | 0.20 | none cut | 1.17 C |
| Uniform key sets of 0.30 / 0.50 / 0.75 / 0.95 C | at α = 1: 80-99K / 52-63K / 48-58K / 46-54K positions | 1.83 (2.87) / 1.44 (2.50) / 1.21 (1.40) / 1.17 (1.32) | 0.86 / 1.19 / 0.67 / 0.68 | 0.97 to 0.98 | 2.19 to 2.24 C (3.21 to 3.26 C) |
| Bulk load into an empty namespace | at α = 1: 43-52K positions | 1.09 (1.30) | 0.72 | 0.99 | 2.19 C (3.22 C) |
| 90% of the keys deleted, then writes | at α = 1: 5.1-6.1K positions | 1.18 (1.45) | 0.91 | 0.88 | 1.44 C (2.25 C) |
| 1,200 keys, uniform rewrites, seals of 60 / 600 positions | at α = 1: 1.6-1.9K / 1.8-2.1K positions | 1.13 (1.35) / 1.07 (1.25) | 0.76 / 0.70 | 0.88 / 0.86 | 2.24 C (3.42 C) / 2.20 C (3.60 C) |
| 1,200 keys, 90% deleted, then writes, seals of 60 / 600 positions | at α = 1: 180 / 29-59K positions | 1.21 (1.29) / 1.22 (1.96) | 0.71 / 1.96 | 0.91 / 1.00 | 1.43 C (2.19 C) / 1.33 C (2.15 C) |

The cost rule made every cut but one: in the 1,200-key namespace whose seals rewrite 40% of its keys, after the mass delete, the cap fired once, the newest block held 1.64 times the cap, and an answer reached 1.96 times the copy (the re-check's independent port: up to 3.44 times the cap and 2.00 times the copy). A store just below the cut gets a copy at most 1/α of the whole-block answer it would have had at the cut (α = 0.5: worst 1.98; α = 1: 0.86 to 1.00; α = 2: 0.50). In the 40,000-key model no answer above the cut exceeded 1.19 times the copy (the 0.50 C key set) or 0.91 after the mass delete, where the design first reviewed sent 3.62 times; that design gave copies of 1.54 times the answer (moderate), 3.34 to 1.57 (key sets) and 1.59 (bulk load). Key sets of 0.30 to 0.50 C send up to 1.83 times the missing bytes on average, because each block holds most of the set and the ratio stops the young blocks merging into the old, and level-0 sections served whole repeat hot keys until their roll-up, which puts the hot-heavy worst at 2.17 times; the cut still bounds both against the copy. Seal, block and base bytes written are 0.68 C a day at α = 1 for the moderate churn (about 180 MB a day at 5M, about 3.4 times its raw changes) and 3.6 C for the hot-heavy (about 1.8 times); the key sets write 1.1 to 2.1 C per 10,000 positions, 4 to 8 times their raw changes; T4.7 records the write rate. The disk figures are the base, the blocks, the level-0 sections and the cut's transient; they exclude the cached full copy (up to |st(s)| per namespace per server) and the squashed inputs awaiting the next table (NB-C2).

Across a group, the model's `files` mode runs the seal files, roll-ups and tables over 4,096 namespaces of 1,220 keys and over 64 of 78,125, 2,048 transactions per seal on a Zipf weight (0.8 to 1.3), for 800 seals with a table every 16. Rolling up every 16 seals writes 1.8 to 3.4 times the raw changes, leaves no dead bytes, and creates 121 to 245 files per seal at 4,096 namespaces (written as one sync group per roll-up, about 2,000 to 3,900 files) and 5 at 64, about 11,500 to 12,200 files on disk at 4,096. Every 64 seals: 1.5 to 2.2 times, and 53 to 64 files per seal. The packing it replaces (below) wrote 5.0 to 11.6 times.

**Server cost per answer** (estimates; T4 and T9 measure them): a ring answer reads memory, as today. A block answer reads what it sends plus the records of R's block it filters out, at most one block more: in the model, 1.3 to 2.9 times the missing bytes on average at α = 1, and up to 13 times for a short answer whose R falls late in a large block. It merges nothing and caches nothing per R. A full copy reads the base and every block, at most (1 + 2α) · |st(s)| at the cap, and writes |st(s)| once per s it is asked at.

A position above head gets no answer until the source catches up, so no namespace goes backwards (D1).

### D7. Relay: a mirror of blocks, base and ring

A relay is a reader, not a member of the group (ND-R7.2; `SCALING.md:52`: agents and relays do not vote). Per namespace it keeps what the authority keeps: the base, the blocks above it, and a ring of the entries after the newest block, mirrored from the session stream (W5), plus a prepared generation in the agent's layout (D8) for agents that bootstrap. It answers with the same three tiers (D6). It computes no layout: at each seal it fetches the upstream's layout for the namespaces it serves (the names, digests and |st(s)| of base and blocks) and every block or base it lacks, by name, from the authority or an upstream relay, and verifies each digest. Sections of a seal file are served by name, so a relay never receives another namespace's records. A relay that starts late, or falls behind the upstream's newest seal, fetches the current layout and its files once; level-0 names vanish when the upstream squashes them, so it never asks for a seal it missed by its old name. Its exact-base edges (`include/detent_relay/configuration.hh:10`) stay the hot path, per namespace: any R inside its ring is an edge once the relay holds the upstream's newest seal; an R in a span it missed is answered from blocks until then. Blocks and full copies are immutable, so any HTTP cache that authorizes per namespace serves them, and every store inside one block shares the same files. Bundles become upstreams, credentials grant namespace patterns instead of bundle indices (`docs/RELAY.md:47-60`), and each namespace is served once it is ready instead of after every bundle (`RELAY.md:78`). Disk per relay is the authority's per namespace followed: 2.3 to 2.9 C at α = 1 in the model, beside the prepared generation (1.51 GB at 5M). Its own files follow D6's commit and recovery, with the upstream's layout in place of the rules.

### D8. The agent store

```text
generation_dir/          one directory (L1); the only directory the barrier syncs
  storage.meta           host identity, 64 B; absent in a seeded image
  selected               namespace table, replaced atomically
  change                 reader.md D4's words, never replaced
  owner.lock             held exclusively by the agent while it writes the store
  generation-<id>.{image,read,lease}
  segment-<id>.{pack,owner}
  .tmp-<random>          a file being written; removed at startup
```

The table replaces the 176-byte descriptor (`include/detent_local/generation.hh:23-31`): a header (magic, version, sequence, count, directory cursor, profile), then entries sorted by name, each with name, cursor, `change` slot, generation id, build ordinal, root length, and the SHA-256 of the root and of the `.read` record. One read gives a reader every namespace. Namespaces share no segments. A publication cycle collects every namespace generation ready in that cycle, syncs them as one group (D11), replaces the table once, then increments `change`. The paged format stays: it reads in 268.5 ns at 5M (reader.md) and shares unchanged pages between generations.

**The `change` file** takes reader.md D4's shape (RD-5): word 0 is the store word, incremented after every replacement of `selected`; word 1 + slot is a namespace's sequence, incremented after every replacement that changes that namespace's entry. A slot is assigned when a namespace first appears and is never reused while the store exists; the file grows by extension, never shrinks, and is never truncated, unlinked or replaced, with one exception: a seeded `change` owned by another account, which the agent never reads and replaces by rename after its owner check (D9). The agent creates it with the store, before the first `selected`, takes `owner.lock` exclusively (today's only `LOCK_EX` is the daemon's pid lock, `src/detent_cli/daemon.cc:39,63`), and bumps every word once at startup after the barrier (D11).

**Catch-up input.** An answer may carry a tombstone for a key the store does not hold (created and deleted after R, D6's proof); the agent applies it as a no-op. Today's deltas are contiguous and never carry one (`state.cc:952-953` counts a delete as a change only when the key exists), so S7 adds the case to the agent's tests. This is the one change blocks force on D8.

**File ids (L6, ND-R4).** Ids are derived from content so two builds of the same input agree, and files are published so a crash and a republication can never trust a file nobody synced:

1. The generation id is SHA-256 of a domain, the layout version, the profile, the incarnation, the position, the base generation id (zero for a build from a full copy) and the build ordinal, truncated to 16 bytes; today it is random (`src/detent_delivery/coordinator.cc:211`). The segment id keeps its derivation from the generation id and ordinal (`src/detent_local/paged_segment_builder.cc:38-47`) minus the storage identity. That identity, random per host (`disk_store.cc:32`), leaves `.read`, `.owner` and `.lease` (`src/detent_local/reader_metadata.hh:8`, `include/detent_local/ownership.hh:10-17`) and stays in `storage.meta`.
2. The build ordinal is 0 for an ordinary build. A repair of the selected generation at an unchanged head (D10) builds with ordinal r + 1, r the selected entry's, so it writes new inodes under new names and never touches a file the durable table reaches. Byte identity (T6) holds at ordinal 0.
3. Every file is written under a `.tmp-` name and published with `linkat` to its final name, which fails rather than replaces when the name exists; then the temporary name is unlinked. One POSIX call serves Linux, macOS and FreeBSD.
4. A name that already exists is a reused name: the publication verifies its bytes against the digest it expects (the owner record's checksum for a pack, the table's digests for a root or `.read`) and, when it matches, puts it in its own sync group. A mismatching name that the table on disk does not reach is an orphan a crash left behind: the publication unlinks it and links its own file under that name (N1). A mismatch on a name the table reaches is corruption, and D10's repair applies.
5. Startup removes every `.tmp-` name after the barrier, which drops the second link a crash can leave (completion's `validate_file` wants one link, `completion/posix.h:116`).

The review's crash model finds 14 violations in 283 cases when reused names are trusted, and none in 439 when they are verified and synced (ND-R4). The re-check's model of this discipline finds 0 violations in 643 cases, for a first publication and for a repair at the next ordinal; failing on an orphan instead of unlinking it wedged the namespace in 162 of 643 power-cut cases, and unlinking gives 0 wedges and 0 violations (N1). T7 repeats both on the real store.

### D9. Seeding from an image

`detent-image build --source managed.json [--namespaces a,b] [--uid UID] --out DIR` asks for each namespace's full copy (D6) and writes the generation files, `selected` with sequence 1 and `change`, without `storage.meta` or any `.lease`. `--frames FILE` builds from saved full-copy frames with no network. It runs the agent's own full-copy build at ordinal 0, so its files are byte-identical to an agent's from the same copy (T6). It shares its writer with the reader's test helper (reader.md D9). (Blocks change only the word: a Replace was the full copy.)

**Where it lives.** The seeded store is an image layer, read-only under overlayfs, at the agent's `generation_dir`. Adoption never opens a seeded file for writing (ND-R6): it opens each `O_RDONLY | O_NOFOLLOW | O_CLOEXEC` and syncs them through S1's primitives, which accept read-only descriptors (L1 ruling). Completion opens existing files `O_RDWR` today (`completion/posix.h:731`), which would copy the 1.51 GB store up per container and lose page-cache sharing between containers. So completion's existing-file open gains a read-only mode (S1), and the census and collector open every seeded file through it. The one file they open for writing is a generation's `.lease`, through the `ExclusiveLease` primitive (`src/detent_local/disk_store.cc:1267-1269`, `completion/operation.h:39`); an image ships none, and adoption creates one per seeded generation as a new file before its barrier, so the census finds the lease it requires (`disk_store.cc:298,378`). What the agent writes is small or new: `storage.meta`, the `.lease` files, a new `selected` by rename, new generation files, and `change`, the one seeded file it writes, mapped read-write (8 bytes per namespace plus 8, copied up once) (N4a).

**Adoption.** An agent that finds a valid table, a valid `change` and no `storage.meta` adopts the store: it runs the adoption barrier (one sync group over every seeded file and the directory, D11), bumps `change`, reports ready, writes `storage.meta` last, and asks for (R, h] from each seeded cursor. Readiness follows the barrier, as at every start (ND-R9).

**Ownership.** Seeded files must be owned by the agent's account, with one link and no group or other write (`disk_store.cc:28-31`; completion's `validate_file`, `posix.h:113-117`). `detent-image` writes files owned by `--uid`, the agent's account by default. Adoption checks owners with `fstatat` before it opens any seeded file. An agent that finds another owner does not adopt, and reports `seed_owner_mismatch` with the expected and found uid in health, so the 722 s path is visible, never silent (N4b):

1. A directory owned by another account is refused by completion's directory trust unless shared directories are allowed (`completion/posix.h:450-455`). The agent issues no primitive in it and stays not ready. This is the common mismatch, an image layer owned by root with the agent not, and the health reason's documentation says that it leaves the agent not ready rather than bootstrapping.
2. Files owned by another account in the agent's own directory are never opened or read, the table included (N10). The agent bootstraps in place at build ordinal 1 with a fresh table. An image is built at ordinal 0 (T6), so no derived name equals a seeded file, and for D8.4 a foreign table reaches nothing: a reused name at ordinal 1 is the agent's own, from an earlier attempt, and is verified or treated as an orphan as usual.
3. The seeded `change` is foreign too. Readers never mapped it, because they refuse a `change` not owned by the publisher (reader.md D4 rule 4). The agent replaces it before its barrier with a new agent-owned file, by rename: the one exception to D8's "never replaced", for a file nothing has read.
4. The collector runs only after the first publication, and only on names the new table does not reach. It unlinks foreign names without opening them.
5. A restart before the first publication finds `selected` still foreign and takes this path again; once the agent's own table is published, restarts read it as usual.

The re-check's model of this fallback finds 0 violations and 0 foreign opens in 1,141 cases at ordinal 1, with a crash at every primitive of startup, bootstrap and collector; reading the ordinal from the seeded entry opens a foreign file in every case, and running the collector at start breaks the invariant (N10).

### D10. Restart: trust prepared generations

A generation is verified once, by the build that writes it. Its `.read` is written after that verification, and the table binds the `.read` digest. Restart reads `storage.meta`, the table, and per namespace the 320-byte root and 192-byte `.read`, each checked against the table's digests: O(namespaces), no pack read, no page hash. Packs open on first use. Corruption is still caught:

1. **First touch.** A page's SHA-256 is checked against its parent reference before use, by readers (reader.md D3) and by the agent when an update reads a base page.
2. **Scrub.** A background pass hashes each pack against its owner record's checksum, paced continuously so a full pass ends every `scrub_period`. `.read` gains a manifest digest over (segment id, length, checksum), so the scrub also proves no owner record is missing. The review estimates an hourly pass over 1.51 GB at about 0.2% of a core on the reference host (ND-R8); S3's criterion sets 24 h, and the period is the lead's to tighten.
3. **Repair.** A mismatch fails that namespace alone: readers get `DETENT_SOURCE_FAILURE` for it, and the agent asks for a full copy (a store it cannot trust is no store) and publishes a fresh generation at the next build ordinal (D8).

### D11. One barrier instead of a flush per file

**Invariant:** every file the durable table reaches is durable before the table entry naming it can be. Publication keeps it with the grouped order of `docs/STORAGE_BARRIER_ARCHITECTURE.md:17-26`: owner records and reused names, barrier; payloads, barrier; the new table written under a temporary name and fsynced, then renamed over `selected`, barrier (the directory sync); then the `change` increments. The table's fsync comes before the rename: startup syncs no file, so a renamed table with unsynced bytes would become durable through the startup barrier and a power cut would revert a table already served (N6). The increments need no sync, because startup bumps every word (reader.md D4 rule 2). Each group syncs through S1's `SyncFiles`, which runs `sync_files_and_directory` (`completion/io.h:231`) over the store's one directory: every file, then the device flush, then the directory.

The amortization differs by platform. On macOS fsync leaves data in the drive's cache and one `F_FULLFSYNC` per device flushes it all (`posix.h:181-185`), so N files cost one flush. Linux `files_sync` starts writeback on every file and then fsyncs each (`posix.h:229-240`); on ext4 and XFS each fsync is its own device flush. FreeBSD syncs file by file as well. T2's Linux run decides whether S1 needs concurrent fsyncs there.

**Startup.** Restart syncs no file. It issues one barrier, a sync and flush of the store directory; then removes `.tmp-` names (D8); bumps `change` (reader.md D4 rule 2); and only then reports ready, garbage-collects or publishes. The census already calls `ready()` after its barrier (`disk_store.cc:178-179`) and keeps that order (ND-R9): serving before the barrier would let a power cut revert a table a reader already used. The per-file syncs (`:133`, `:171`) go. Adopting a seeded store syncs every file once, as one group, before ready (D9).

**Library change.** Completion's asynchronous driver syncs one file per primitive (`completion/operation.h:39`); the batched path exists only on the synchronous `IO`. S1 adds `SyncFiles` and `OrderFiles` (`io.h:231,240`) over read-only or read-write descriptors in one directory. This is the device amortization the storage-barrier blueprint defers (`STORAGE_BARRIER_ARCHITECTURE.md:38`), and strata's commits, which go through completion's `IO` (`strata/docs/ARCHITECTURE.md`, "Libraries"), can use it too.

### D12. Cold bootstrap

1. **Sync** (239 s at 5M): D11's group sync in every publication, relay and agent.
2. **Hashing** (112 s): each byte hashed once per tier. The builder's page digests are the verification, so re-validating a tier's own build and the relay's re-hash on recovery go (D10). Frame SHA-256 stays.
3. **Building** (313 s of overlapped export, decode and build): namespaces build independently, up to `build_parallelism` at once, and an agent starts a namespace as soon as its relay has it. The gain depends on the namespace mix and is not estimated.
4. **Seeding** takes bootstrap off the agent fleet in the owner's scenario; relays, and agents without an image, still bootstrap.

## Strata fit

The bar: Detent uses strata only where its needs map onto strata's existing concepts with nothing Detent-only bolted on.

| Question | Finding | Evidence |
| --- | --- | --- |
| Strata segments as delta blocks | Deferred: the owner holds strata's evaluation, and Detent needs no number from it to land D6. A delete or replace sets a live bit in an older segment (`index.hh:1745-1756`), so a segment alone carries no position range; the Xapiand lead notes that a segment's changing delete state can ship as a per-segment bitmap delta in a manifest-relative install. Strata parks its own "pay only for what you are missing" evaluation for later. Detent's blocks are chosen on their own numbers (D6) | `strata/include/strata/core/index.hh:1745-1756`; `docs/ARCHITECTURE.md:98-106`; `merge_policy.hh:16-60` |
| Relays shipping segments for (R, h] | Deferred with the row above. Physical segment files differ between replicas (`REPLICATION.md:86-99`); Detent's relays ship blocks, which are byte-identical by construction (D7) | `REPLICATION.md:86-99` |
| Agent store as one merged strata segment per namespace | Not now. A point read is a fuse filter, a block-tree term dictionary and a stored block (LZ4, zstd or raw), unmeasured against the 268.5 ns target; reads go through an index or view in one process (`get` returns `std::optional<std::string>`); the agent needs read-only attach from other processes without allocation, and page sharing between generations for publication proportional to the change | `format.hh:102-114`; `index.hh:1197-1209`; `docs/design/shared-reader.md` |
| Many groups per node | Designed, not built: coalesced heartbeats and quiescence (Track C2, D12), forced checkpoints and snapshot pacing (D13) | `docs/design/track-c.md:18,627,669` |
| Maturity | Phase 3; the README says not for production storage until the key-value rows pass their gate; `strata::group` is designed, and the tree holds only `strata/core` | `docs/STATUS.md`; `README.md:112-116` |

**H1 is withdrawn.** It asked consensus for three things; blocks need none of them:

1. **Apply output.** Detent's apply already hands its changed operations to `AppliedChangeRecorder` (`change_recorder.hh:18-32`, `state.cc:1480`); the ring and the block builder consume them inside Detent. `Applied { through }` (`consensus/include/consensus/types.h:184`) stays as it is.
2. **A compaction gate.** Consensus compacts only through a checkpoint the host captured (`consensus/ARCHITECTURE.md:105,109`). Detent captures its checkpoint at a seal and persists it once the layout at that seal is durable (D6), so no entry is compacted before a durable tiling covers it.
3. **History read by range by non-members.** Relays read blocks and the ring through the session endpoint (D7); nobody reads the journal.

What changes for the Xapiand agreement: Detent asks nothing of strata or consensus, so the agreed H1 shape (retained journal history with a horizon and a byte budget, typed "too old", no reader holding compaction) has no Detent consumer. Its condition "the journal itself, no parallel delta store" is superseded for Detent by the owner's ruling: blocks are delivery history derived from apply, not a second replication path; voters still replicate only through consensus and the journal. A smaller H1 (an apply-output interface and a compaction gate in `strata::group`) is strata's to take up for Xapiand's change capture if it wants one; nothing in Detent waits for it.

**Recommendation:** adopt strata's replication shape (one group, journal, deterministic apply, checkpoint and install), keep Detent's state machine and paged agent store, and do not use the strata core or segments in delivery unless a resumed strata evaluation shows segment deltas beat D6's blocks.

## Reader alignment

| Reader assumption (reader.md D2) | Here |
| --- | --- |
| A1: a selected descriptor per namespace | Changes: one table entry per namespace (D8), carrying what the descriptor did, so attaching "all" reads one file instead of one per namespace |
| A2: one record lists the namespaces | Holds: the table. "All" means its entries, which the agent already filtered by credential |
| A3: one store-wide change word | Holds and extends: `change` is reader.md D4's shape, with the store word and a sequence per namespace slot (D8) |
| A4: revisions never go backwards | Holds per incarnation, including across a block answer (D6's proof). A new incarnation is a generation switch on the same namespace: it arrives as a full copy under the new cursor, the agent publishes it in the namespace's existing table entry and `change` slot, and the reader re-attaches it as any switch and reports the new incarnation. A revision regression is refused only within one incarnation; today `reader.cc:58-61` throws on any identity change. A store identity change (`storage.meta`, the cluster) is still refused. reader.md A4 says the same |

The reader attaches a namespace on its first lookup, not at open (reader.md D2), which keeps its first read under 1 ms with thousands of namespaces.

## Gate rows

Targets come from the attribution and are fixed after the first screen. The lead measures on the reference host and on Linux, under `quiet exclusive`. 10K, 100K and 5M use preserved stores; the first T2 run at 1M preserves one.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Restart | At most 0.3 s at each of 10K, 100K, 1M, 5M; 5M at most 1.5 times 10K | The attribution's restart method: an APFS clone of a prepared store per run, wall from exec to "managed local store ready", six runs per scale, then the 40 probes of `benchmarks/prepared-pack-access-5m-20261025.json` with zero failures |
| T2 Bootstrap | At most half the recorded wall at each scale (1.36, 7.9, 73, 361 s); syncs at most 5% of wall; bytes hashed per tier at most 1.2 times bytes built | The attribution's bootstrap harness (`tests/detent_system`, adapted from `capacity-trials/restore_5m.py`) with `sample` attached; sync and hash counters from S2 and S4; on macOS and on Linux ext4 |
| T3 Seeded start | Upstream blocked: first correct read within T1's bound. Upstream open: at head within 2 s; no full copy for a store at or above the cut; wire bytes at most the projected changes (each changed operation counted once per transaction), since each block carries a key once; no refused write reaches the agent; a store built before S5 passes the watermark check (`paged_read_state.cc:26-28`) after its full copy; 5M at most 1.5 times 10K | Build an image with `detent-image` at R; apply 1,000 changes over 10 namespaces (100 deletes, 50 re-inserts of deleted keys, 200 rewrites of 20 hot keys) and 1,000 refused writes, with the policy's change seal at 64 transactions; restart every voter and the relay; start the agent from the image. Repeat with R inside the ring, below the ring inside a block, inside the oldest block above the base, and below the base (a full copy, counted "cut" in health), and with a store built before S5 |
| T4 Blocks | (1) Every seal file, block and base is byte-identical on every voter, and every block and base on every relay, also with a ring capture failure injected on every voter at the same position and, once the "set limits" entry exists, with a policy change mid-run. (2) The ring evicts only at its bound and only entries the durable layout covers; after a restart of every voter and the relay, every answer for an R at or above the cut is unchanged (no reset). (3) The layout follows the rules (ratio 4, L, the deduplicating merge, the cost cut against \|st(s)\|, the disk cap), and disk per namespace stays within the base, 2α · \|st(s)\| of blocks apart from the newest, the level-0 sections since the last roll-up, one cached full copy, and what was superseded since the last table (at most one base rewrite and the squashed inputs). (4) A voter installed from a snapshot fetches the table, bases, blocks and seal files and answers the same bytes. (5) Crash matrix: a power cut at every primitive of a seal commit, a roll-up, a table publication (the first one included), the collector and an install; recovery reaches the layout of an uninterrupted run, every file the table reaches verifies, replay from a checkpoint below W folds no seal twice, no checkpoint is persisted above a seal whose layout was not durable, and an install is acknowledged only after its files and table are durable; a rotted seal file whose journal span is compacted is fetched from a peer. (6) Liveness: 200,000 entries of which 97% are NoChange or refused, and a run of session churn with no positioned write, never reach `LogFull` and keep compacting, at consensus's default limits and at `log_entries` 4,096 and `log_bytes` 4 MiB; a node whose retained bounds are below the group's refuses to start. (7) Seal, merge, cut and copy times, server bytes read per answer, bytes written per position, and files and flushes per seal and per roll-up at 4,096 namespaces, recorded per scale (no target until measured). (8) With the model's churns scaled to 100K keys, the key sets of 0.30 to 0.95 C, a bulk load, a 90% delete, and a 1,200-key namespace whose seals touch 40% of its keys with a 90% delete, cut position, the rule that cut, and copy / answer just below the cut within 20% of the model's | Unit tests over the accumulator, seal, squash, cut, commit and recovery with an oracle that replays every position; the crash matrix over completion's sync seam (`posix.h:496`), as T7; the system harness at each scale with the churn generator, voter, leader and relay restarts, a snapshot install, an injected capture failure and an injected seal-file write failure |
| T5 Corruption | 100 single-byte flips in random pages: restart is ready; the first touch returns `DETENT_SOURCE_FAILURE` for that namespace only, never a wrong value; the scrub reports untouched flips within one pass; reads recover after repair, at the next build ordinal. A corrupt table or `.read` refuses the store or the namespace | A test over a 100K store written by the reader's test helper |
| T6 Identity | Every generation and segment file (`.image`, `.read`, `.pack`, `.owner`) identical between `detent-image` and an agent's full-copy build, and across two `detent-image` runs; block and full-copy bytes identical from every voter and relay. `.lease` and `owner.lock` are excluded (host and process state) | Per-file SHA-256 at each scale, on macOS and on Linux, and across the two |
| T7 Durability | After an interruption at every primitive of publication, adoption, the startup barrier and the temporary-name removal, then simulated power loss, every file the durable table reaches exists and verifies; no table served before the cut is reverted | The blueprint's crash-cut matrix (`STORAGE_BARRIER_ARCHITECTURE.md:48`), with completion's sync seam (`posix.h:496`) recording what reached a flush and discarding the rest; plus restart-then-republish: a cut in publication, a restart, the same input published again over the reused names, and a power cut at every step of that. A name made durable with unsynced bytes is unlinked and rebuilt, and the namespace publishes (no wedge, N1). Revert check: renaming the table before its fsync fails T7 (N6) |
| T8 Groups (decides S10; gates no step before it) | Per voter, at 4,096 groups and at a bounded count (64, an estimate): idle CPU at most 2% of a core; 2,000 transactions/s spread over them with p99 commit at most the platform's barrier p99 plus 20 ms; memory at most 64 MiB above one group's; file descriptors at most 2 per group plus 256; after a voter restart, every group led and committing within 10 s, with at most one election per group it led. All estimates; `docs/SCALE_TARGETS.md:117` is the throughput baseline | The authority capacity harness with the 5M corpus spread over the namespaces, on Linux ext4 and on the reference host, with strata Track C2 D12 and the shared turn log in place; RSS and open descriptors sampled each second |
| T9 Cut point (sets α) | No target: it measures T_blocks(x) = x / L + one round trip + the agent's apply time for a block answer of x bytes, and T_base = C / L + one round trip + the agent's build time for a full copy, and sets α to the x / C where they cross, at L = 1 and 10 Gb/s (estimates of fleet links); α then enters the layout policy in `Limits` through the "set limits" entry (D6). It records relay CPU and bytes read per answer of each kind | On the 5M corpus, reference host and Linux: the agent at a prepared store applies block answers of 0.05, 0.25, 0.5 and 1 C (keys spread uniformly, 5% tombstones) and builds a full copy, five runs each under `quiet exclusive`; fit apply time against x; the relay serves each answer from a block layout produced by the model's moderate churn |

## Format and wire changes

| # | Change | Today |
| --- | --- | --- |
| W1 | `Cursor` is {cluster, incarnation, position}, all the namespace's | `cursor.hh:8-13` |
| W2 | Scope identity removed | `scope.hh`; `AUTHORITY_EXPORT.md:5-9` |
| W3 | Authority: per-namespace position counters, starting at each namespace's stored revision, and incarnations at apply; record revisions are namespace positions; per-namespace live sizes; the directory stream; the ring keyed by namespace position, evicting lazily; seals at apply on changes or log progress; the layout policy in `Limits`; seal files, blocks, bases and the `layout` table on disk; checkpoints at a seal with a durable layout | `state.cc:1001-1003`; `state.hh:25-31,46-64`; `change_history.hh:9`; `node.cc:467-516` |
| W4 | Producers: sessions, contiguous sequences and `/v2/result` unchanged on one group; preconditions are namespace positions and carry the incarnation. When S10 adds groups, a producer holds a session and sequence per group it writes and reads results per group; a transaction stays in one namespace (`SCALE_TARGETS.md:118`), so in one group | `AUTHORITY_ARCHITECTURE.md:16`; `state.cc:938,943-951`; `service.cc:827` |
| W5 | One session endpoint (Q4): open and next carry `[{name, cursor}]`, or `all` with a directory cursor, and a `wait_ms`. Poll is next with `wait_ms` 0 every `poll_interval_ms`; `POST /v2/distribution` and `delivery_mode` go | `scope`, `namespaces`, `base` (`AUTHORITY_EXPORT.md:63`); `ACTORS.md:97-100` |
| W6 | Per-namespace frames: ring entries after R; or the block holding R + 1 trimmed to revisions above R, each younger block whole, then the ring's entries; or a full copy at s, then the ring's entries. Blocks are named (namespace, incarnation, first, last) and full copies (namespace, incarnation, position); both are immutable and fetchable by name, one session response carries every frame, and an agent applies a tombstone for a key it lacks as a no-op. `X-Detent-Kind` and `X-Detent-Revision` go | `AUTHORITY_EXPORT.md:71` |
| W7 | A new incarnation is delivered as a full copy under the new cursor; agents publish it in the namespace's existing table entry and `change` slot, and readers switch generations (Reader alignment, A4) | `reader.cc:58-61` |
| W8 | Relays are not members: they read W5 and W6 from the authority or an upstream relay, adopt the upstream's layout per namespace, and mirror blocks, base and ring. Configuration: upstreams and one store; grants by namespace pattern | `RELAY.md:47-60` |
| W9 | Agent and relay store layout: one directory with `selected`, `change` (reader.md D4), `owner.lock`, generation and segment files, `.tmp-` names | `generation.hh:23-31`; `docs/ACTORS.md:104-113` |
| W10 | A paged root holds one namespace and the new cursor | `paged_layout.hh:30-37` |
| W11 | Derived generation and segment ids with layout version, profile and build ordinal; no storage identity in `.read`, `.owner`, `.lease`; files published by `linkat` from a temporary name | D8's citations |
| W12 | `.read` gains the manifest digest | `reader_metadata.hh:7-13` |
| W13 | Grants accept exact names, whole-component prefixes and `*` (Q1); `namespaces` becomes optional in `managed.json` | `service.cc:446`; `src/detent_delivery/configuration.cc:156` |
| W14 | Completion `SyncFiles` and `OrderFiles` primitives over read-only or read-write descriptors (library API) | `operation.h:39`; `posix.h:731` |

## Alternatives considered

**A derived block per (R, s)** (the lead's B1 of 2026-11-04): the blocks from R's to the newest merged into one answer [R + 1, s], each key once. It sends exactly the missing bytes, but the review measured the server reading 1.51 times the answer on average and up to 11.6 times, with a merge and a cache entry per distinct R (1,260 entries for 1,260 stores), against 1.25 times on the wire on average and 1.45 at worst for persisted blocks served whole, with no merge and every store in a block sharing the same files (NB-6). Both read the same blocks. Whole blocks were the owner's original picture; they win on server cost and stay within a quarter of the wire bytes in the churn shapes (this model: 1.09 to 1.29 on average), so D6 takes them. Not selected.

**Seals counted on positioned changes only** (the lead's B3). A group whose entries are mostly refusals, NoChange or session churn never seals, so never checkpoints, and stops at `LogFull` (NB-1; D6's table). D6 keeps the change trigger and adds log progress.

**A level-0 file per namespace per seal.** At T8's rate over 4,096 namespaces, about 1,611 files and syncs a second (NB-8). D6 writes one seal file per seal.

**Seal files that also hold small outputs, with live sections copied forward** (the fold of 2026-11-04). Squash ran at every seal, outputs under 1 MiB stayed in the seal file, and a seal file under half live had its live sections copied into the newest one, so a cold namespace's section never pinned an old file. It kept to about one file per seal, but the model's `files` mode measures the copy-forward at 2.3 to 4.9 times the raw changes, total writes of 5.0 to 11.6 times against 1.8 to 3.4 for the roll-up, and it needed two thresholds outside the replicated policy and a second collector (the re-check's NR-4). The roll-up consumes seal files whole instead. Replaced.

**A sliding window of raw delta bytes, W = k · C, with a held Replace past it** (the previous D6, with build cycles N7 and a cycle pin N8). W counted every change, so a hot key churning used it up: at k = 1 it reached back 0.5 days under the hot-heavy churn, 4.9 under moderate and 49 under light (the previous revision's expected-value model), against cost cuts of 54-65, 33-42 and about 340 days at α = 1 in this one. Compacted cover grows with distinct keys changed and saturates near C, so the cut follows what a store misses, not how often keys change. A store past W also got a full copy built from a live capture of state, which needed the build cycles and pin; the base is persisted, so nothing pins state. Replaced.

**The journal as the only history** (strata proposal H1). Consensus would retain entries and per-command result records below compaction, readable by range by non-members, under a horizon and a budget. A consumer behind the horizon still got a full copy, and uncompacted entries repeat hot keys as W did. Replaced by blocks derived from apply, which need nothing from consensus ("Strata fit").

**Tombstones kept for the incarnation's life.** Every store of any age would get a delta, but a store older than the cut misses more than a full copy holds: in the moderate model, after six months every live key changed and 1.82M keys were deleted, 1.24 C of answer against 1.00 C of copy, and the base would grow by 124 MB of tombstones a year. The cut drops what can never beat the copy. Not selected.

**Changes computed from state plus retained tombstones.** Exact and bounded by the namespace, but each answer scans the whole state by revision and caches poorly. Blocks are that idea with the scan bounded by the age of R. Not selected.

**Keep the global revision.** A namespace's position moves with unrelated writes, an image cursor cannot serve applications reading different namespace sets, and positions would change meaning when groups split. Not selected; D3 keeps one group but gives each namespace its own counter.

**A group per namespace now** (the draft's D3). It does not fit today's stack: 4,096 idle groups cost three times T8's CPU target before any I/O (ND-R1), and Track C2 is not built. Deferred to S10, behind T8.

**Propose effects instead of commands.** The leader would apply before proposing, so the journal would hold outcomes directly; but it changes what consensus replicates and computes outcomes against state the leader has not committed. Apply's recorder output gives the same answer on every voter. Not selected.

**Fresh random ids.** They rule out reuse (`coordinator.cc:211`) but lose byte identity between an image and an agent's build. Derived ids with no-replace publication and verified reuse keep both (D8).

**Strata segments for history, relay caching or the agent store.** Deferred for the first two (strata's evaluation is on hold); not now for the store. See "Strata fit".

**A `selected` file per namespace.** A reader following all namespaces would open one file per namespace on its first read. Not selected.

**Verify at restart on every core, or check a manifest at every start.** The first is still O(N) per restart; the second reads every owner record and still trusts the pack bytes. The manifest feeds the scrub instead.

## Limits

1. **Silent:** a corrupted page nobody touches stays undetected until the scrub reaches it, up to one `scrub_period` plus a pass. A page a long-lived reader verified before the corruption is served under its verified bit until the scrub, since the reader never re-hashes a verified page (reader.md D3; ND-R8).
2. A store below the cut gets a full copy, by design; health counts full copies by reason (cut, incarnation, no store, repair). When the cost rule cuts, the copy costs at most 1/α times the whole-block answer it replaces, at the moment of the cut (model: 0.86 to 1.00 at α = 1, 1.98 at 0.5, 0.50 at 2); a later deduplicating merge can shrink that answer by a few percent. The disk cap can cut a store whose answer is smaller; it fired once in the model, in a 1,200-key namespace after a mass delete, and health counts its cuts apart. A store above the cut is typically sent up to about 1.25 α · |st(s)| (the 40,000-key model's worst: 1.19 times the copy); the ceiling is about 2α · |st(s)| plus the newest block, which is never cut. A namespace whose state is nearly empty, or just shrank, keeps its newest block, which can exceed its copy: 1.96 times in the model, 2.00 in the re-check's port.
3. A new incarnation gives every consumer of the namespace a full copy once: a store from a restored backup's predecessor holds positions the new history does not have.
4. Until S10, the ring is per group: a cold namespace's entries are evicted by other namespaces' writes, so its consumers reach blocks sooner. Blocks and the cut are per namespace.
5. The cut and disk numbers are from a key-level model of one namespace at 1/125 of the 5M corpus, with a fixed hot set, uniform key sets, a bulk load and a mass delete (D6). A namespace whose churn differs gets different cut ages; T4 checks the model against a run, and health reports each namespace's cut position, block bytes and which rule cut.
6. A seeded image has the trust of a store the agent prepared (Q3): keys an altered image adds survive catch-up until a full copy replaces that namespace.
7. Byte identity holds for full-copy builds at ordinal 0 only; generations built from block answers, and repairs, differ by design.
8. D11's macOS argument rests on `F_FULLFSYNC` flushing the whole drive cache, as Apple documents and completion already assumes (`posix.h:181-185`). On Linux and FreeBSD each file's fsync is a flush; there the barrier saves syncs, not flushes.

Documents this changes once built: `docs/ACTORS.md:54-91,97-122`, `docs/RELAY.md:13-80`, `docs/AUTHORITY_EXPORT.md:5-9,29-45,63-71`, `docs/AUTHORITY_ARCHITECTURE.md:16`, `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md:23,29`, `docs/STORAGE_BARRIER_ARCHITECTURE.md:38`, `docs/SCALE_TARGETS.md:72,118`, and reader.md's A1 and A4.

## Open questions for the owner

Q1, Q3, Q4 and Q5 carry the lead's call of 2026-11-03, made under "move fast"; the owner may reverse any. Q2 is the owner's ruling.

**Q1. What can a grant name?** (a) Exact names: "all" means every granted name, and a new namespace needs a grant change. (b) Exact names, whole-component prefixes (`payments.*`) and `*`. *Recommendation: (b)*, which makes D2's "created later" reachable without editing credentials. **Lead call: (b)**, limited to those three forms; the security reviewer reviews the matcher.

**Q2. How much history?** **Owner ruling, 2026-11-03:** compacted blocks per namespace, the in-memory ring kept as the hot path, and a cut by cost: a store whose missing changes cost less than a full copy gets them, and one farther behind gets the copy (P1, D6). It supersedes the lead's call (a), the sliding window. α comes from T9; until then it is 1.

**Q3. Does a seeded image get the trust of a store the agent prepared?** (a) Yes: pages checked on first touch, and the scrub (D10). (b) Verify it fully before serving on first start, 52 s at 5M today. *Recommendation: (a)*: the image shares the application binary's supply chain, and (b) defeats the scenario. **Lead call: (a)**, with the residual in Limit 6.

**Q4. One-shot delivery.** That is `POST /v2/distribution` and the agent's default `delivery_mode: poll` (`ACTORS.md:97-100`). (a) Remove the endpoint and the mode; sessions only. (b) Keep both, on the per-namespace body. (c) One session endpoint with a wait time; poll becomes a zero wait every `poll_interval_ms`. *Recommendation: (c)*: polling agents keep their behavior on one path. **Lead call: (c)** (W5). It removes an endpoint, so it is the owner's to reverse.

**Q5. The authority's shape, with the Xapiand lead.** (a) A consensus group per namespace now. (b) Keep one group and the global revision, filtering its journal per namespace. (c) Wait for `strata::group` and the strata core's key-value gate. (d) A position per namespace assigned at apply, on today's one group; the group count chosen once Track C2 exists and T8 is measured on Linux. *Recommendation: (d)*: cursor, frames, store, relay and image stop depending on the group count, and (a) misses T8 on today's stack (ND-R1). The cost is Limit 4 until S10. **Lead call: (d)** (D3).

## First steps

Each step lands alone with CI green. S1 to S4 attack the measured 120 s restart and 722.6 s bootstrap on today's single-generation store, and no open question blocks them; their done criteria are in the ledger. S2 and S3 keep today's random ids until S7 (C2).

| Step | Change | Needs |
| --- | --- | --- |
| S1 | Completion: `SyncFiles` and `OrderFiles` over read-only or read-write descriptors in one directory, and a read-only mode for the existing-file open (D9), with seam tests per platform; lands upstream, then the pin moves | Nothing |
| S2 | D11 on today's store: grouped publication syncs, a census without per-file syncs, `ready()` after the barrier, sync counters | S1 |
| S3 | D10 on today's store: restart without the six hash passes, first-touch checks on the agent's base reads, the scrub | Nothing |
| S4 | D12.2: no re-validation of a tier's own build; hash counters | Nothing |
| S5 | Authority. S5.1: per-namespace positions starting at each namespace's stored revision, incarnations, live sizes, the directory stream, the enabling entry. S5.2: the "set limits" entry (`SCALE_TARGETS.md:59`), the layout policy in `Limits` with α as a ratio of integers, the group's consensus limits in its configuration and the retained-bound check, the ring keyed by namespace position with lazy eviction, seals on changes or log progress with apply's accumulator, seal files, roll-ups, squash and cut, bases and the first table at the enabling entry, the commit, collector, recovery and repair from a peer, the checkpoint captured in apply at the seal's index (today `service_checkpoint_due` captures at the applied index, `node.cc:519-530`) and persisted with a durable layout, install, the three answers, T4 | S5.1: Q5's call. S5.2: S5.1 (nothing from consensus or strata) |
| S6 | W1-W8 and W13: cursor, producers, the session endpoint, frames, incarnations, relays as readers | S5, Q1 and Q4 calls |
| S7 | D8's store: one directory, the table, `change` (reader.md S5), derived ids with `linkat` publication and verified reuse, tombstones for absent keys as no-ops | S6 |
| S8 | D7's relay mirror of blocks, base and ring, and immutable objects | S6, S7 |
| S9 | D9: `detent-image`, read-only adoption, `.lease` created at adoption, ownership and the ordinal-1 fallback with a fresh table, T6 | S1, S7, Q3's call |
| S10 | The group count: bounded groups or one per namespace, with per-group producer sessions (W4) | T8 on Linux; strata Track C2 D12 built; the shared turn log |

What I want from the next check:

1. D6's inputs: every quantity that decides a seal, squash or cut is canonical or replicated (sizes, policy, accumulator, first base), and the log-progress seal keeps every log mix compacting.
2. D6's roll-up, which replaces the seal-file packing (NR-4): seal files consumed whole, one file per block and base, the files and writes it costs, and the commit, recovery and install around it.
3. The cut: the cost rule over whole-block answers against |st(s)|, the deduplicating merge, the disk cap as a separate bound, and whether the key-level model is fair to the 5M corpus (T4.8, T9).

## Review findings

| Finding | Where it landed |
| --- | --- |
| ND-R1, L3 | D3 (one group, a position per namespace); T8 (platforms, memory, descriptors, voter restart; decides S10); S10; alternatives; Q5 (d) |
| ND-R2, L4 | Superseded by the owner's ruling: no sawtooth, since blocks and the cut depend on block bytes, not on checkpoints (D6); T3 and T4 cases across the tiers |
| ND-R3, L4 | D6 outcomes: the ring holds apply's changed operations, refused writes get no position, `/v2/result` for producers; the per-command result records (N2) go, since nothing reads the journal |
| ND-R4, L6 | D8 file ids (layout version and profile in the id, build ordinal for repairs, `linkat` from a temporary name, verified and synced reuse, startup removal); W11; T7 |
| ND-R5, L1 | The lead's ruling: one store directory. D8 drops `ns/<incarnation>/`; D11's barrier syncs one directory and states the Linux and FreeBSD costs; Limit 8 |
| ND-R6, L7 | D9 (image layer, read-only opens, `change` the only seeded file written; `--uid` and `seed_owner_mismatch`); W14 |
| ND-R7, L5 | W4 producers; W5 one session endpoint (Q4 (c)); W6 frames; W7 incarnations; W8 relays as readers, not learners; D7; Reader alignment A4 |
| ND-R8 | D10 scrub paced continuously; Limit 1 |
| ND-R9, L2 | D11 startup order (barrier, temporary names, `change` bump, then ready); D9 adoption; T7 |
| ND-R10 | Superseded: H1 withdrawn ("Strata fit") |
| ND-R11 | T6 (two platforms, `.lease` and `owner.lock` excluded), T7 (restart-then-republish), T8 rows; done criteria in the ledger (lead) |
| C1 | Ledger: S1-S4 criteria (lead) |
| C2 | First steps: S2 and S3 keep random ids and one directory until S7 |
| N1 | D8.4: a mismatching name the table does not reach is unlinked and rebuilt; on a name it reaches, D10's repair; T7 |
| N2 | Superseded: no result records are stored; the ring is rebuilt by replay above a checkpoint at a seal (D6) |
| N3, N7, N8, N11 | Superseded: no held Replace, build cycles or cycle pin; the full copy is merged from the persisted base (D6) |
| N4 | D9: (a) no `.lease` in an image, created at adoption; a read-only existing-file open in completion (S1) for every seeded file; (b) owners checked before opening, a foreign directory refused, foreign files never opened and the bootstrap built at ordinal 1 (N10) |
| N5 | Superseded: a block answer starts at R + 1 by construction (D6) |
| N6 | D11: the temporary table fsynced before the rename, `change` increments after; T7 revert check. Incarnation: a generation switch on the same namespace (Reader alignment A4, W7, reader.md A4) |
| N9 | Superseded: D6's disk bound (base plus α · base plus one block); T4.3 |
| N10 | D9 "Ownership": a foreign directory leaves the agent not ready; foreign files and table never read; bootstrap at ordinal 1 with a fresh table; a foreign table reaches nothing; the foreign `change` replaced by rename (D8's one exception); the collector after the first publication only |
| H1 (re-check) | Withdrawn: apply output is Detent's recorder, the compaction gate is the checkpoint at a durable seal, nobody reads the journal ("Strata fit") |
| NB-L1 (NB-1) | D6 "Seal": a seal also fires on log progress (8,192 entries or 8 MiB since the last), counted from the entries; an empty seal is a checkpoint point; the model's `seal` table; T4.6 |
| NB-L2 (NB-2) | D6 "Every input is replicated": canonical sizes; the policy in `Limits`, changed by the "set limits" entry and carried in each seal file; apply's accumulator, not the ring, feeds seals; the first base at the enabling or restore entry; D7 relays adopt the upstream's layout; T4.1 with a capture failure on every voter and a policy change |
| NB-L3 (NB-3) | D6 "Squash" and "Cut": the cost rule compares the whole-block answer with \|st(s)\|; merges that only deduplicate pass L; the disk cap at 2α · \|st(s)\| is separate; the model shows the cost rule making every cut; Limit 2 restates the 1/α bound with the model's ratios; ledger B2 |
| NB-L4 (NB-4) | D6 "Files", "Commit", "Recovery", "Checkpoints and the gate", "Install": seal files and the `layout` table under D8's `.tmp-`, `linkat`, verified reuse and rename; the collector after a durable table; recovery recomputes the layout from the table and the seal chain; the gate is a durable tiling of (p, S]; T4.5's crash matrix |
| NB-C1 (NB-7) | D3: positions start at the stored revision; a pre-S5 store takes one full copy; T3 |
| NB-C2 (NB-5) | D6 "Bounds" and T4.3: the disk bound counts one base rewrite, the squashed inputs until the next table, and one cached full copy; no per-R cache exists |
| NB-C3 (NB-6) | Superseded: whole persisted blocks replace B1 (D6 "Answers", alternatives), so there is no merge or cache per R; T4.7 records bytes read per answer |
| NB-C4 (NB-8) | D6 "Files": one seal file per group seal; every block above level 0 and every base its own file, written at roll-ups (NR-4); T4.7 records files and flushes per seal at 4,096 namespaces |
| NB-C5 (NB-9, NB-10) | D6 "Ring eviction": lazy, at the bound, only covered entries; D7: a relay behind fetches the upstream's layout, and the edge claim is qualified |
| NB-C6 (NB-11) | The model seals by size alone with no hour cap (B3), states the newest-block floor, and compares with \|st(s)\|; D6 "Cut" and Limit 2 state the floor and the one-block overshoot |
| NB-L3 (re-check) | D6 "Cut", "Bounds" and Limit 2: the cap bounds the blocks apart from the newest; about 1.25 α · \|st(s)\| is the typical case and about 2α · \|st(s)\| plus the newest block the ceiling; a block is bounded against L when it was built; the 40,000-key figures are labelled as such; 1,200-key rows in the table and in T4(8) |
| NR-1 | D6 "Seal": the log-progress thresholds derive from the group's consensus limits in force, which S5.2 puts in the configuration; a node with lower retained bounds refuses to start; T4(6) at lowered limits |
| NR-2 | D6 "Every input is replicated": α a ratio of integers with exact L and cost test; the "set limits" entry in S5.2; placement follows the replicated policy alone, since the packing thresholds are gone (D6 "Files") |
| NR-3 | D6 "Commit", "Recovery", "Install": the first table at the enabling entry; roll-up outputs' digests in their seal file; a rotted file fetched by name from a peer, else an install; replay below W folds no seal at or below W; an install is acknowledged after its files and table are durable; T4(5) |
| NB-C2 (re-check) | D6 "Bounds": the disk figures exclude the cached full copy and the squashed inputs awaiting a table |
| S5 note | S5 and D6 "Checkpoints and the gate": the checkpoint is captured in apply at the seal's index, not at whatever index `service_checkpoint_due` finds (`node.cc:519-530`) |
| NR-4 | D6 "Roll-up": level-0 sections are consumed by a per-namespace roll-up every 16 seals or at L, so seal files die whole and nothing is copied forward; the model's `files` mode: 1.8 to 3.4 times the raw changes written against 5.0 to 11.6 for the packing, no dead bytes, 121 to 245 files per seal at 4,096 namespaces (5 at 64); awaits the architect |
