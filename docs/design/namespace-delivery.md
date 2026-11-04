> Status: DRAFT (2026-11-03) Designing (ready for preliminary review of the blocks revision). D1, D2 and P1 are the owner's rulings, and the owner ruled D6's mechanism on 2026-11-03: compacted blocks, a cut by cost, the in-memory ring kept as the hot path. D6 and D7 are rewritten on `design/compacted-blocks`; the held Replace, its build cycles (N7) and cycle pin (N8), the sliding window and strata proposal H1 are gone. Earlier revisions answered [the preliminary review and its re-checks](../reviews/namespace-delivery-preliminary.md). Ledger: [status/namespace-design.md](../status/namespace-design.md).

# Namespace delivery

Each namespace has its own position and history, an agent starts from a store shipped in the application image and catches up by fetching only what it is missing, and a restart costs the same at 10K knobs as at 5M.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03.
- Base: `Detent:9b24fb3` for what this revision writes (D3's `service.cc`, D6, D7, D8's catch-up input, "Strata fit", W3, W4, W13); the sections it leaves alone keep their `Detent:d4abd4a` citations (`state.cc` and `change_history.*` are identical at both). Other repositories at `completion:576618d` (Detent's pin), `consensus:61149a2` (main; Detent pins `666cdfc`), `strata:9d52824`. Line citations are at those commits.
- Input: the startup attribution, `docs/perf/agent-startup-2026-11-03.md` on branch `explore/agent-startup` (`Detent:e8d4e59`). Every measured number here comes from it (Intel i9-9980HK, macOS 15.8.1, warm page cache), from `benchmarks/system-capacity-5m-20261024.json`, or from the review's probes, which say so. Sizes of blocks, cuts and answers are arithmetic from a stated churn model, [namespace-delivery-blocks-model.py](namespace-delivery-blocks-model.py), not measurements.
- Depends on: [reader.md](reader.md) D4, whose `change` file shape D8 adopts (RD-5); the lead's L1 ruling, one store directory (ledger); strata Track C D12 and D13 for the group count (D3, T8). Nothing in consensus or strata (H1 is withdrawn, "Strata fit").

| Review | State |
| --- | --- |
| Architect, preliminary | [APPROVE WITH CONDITIONS](../reviews/namespace-delivery-preliminary.md); L1-L7 and C1-C2 folded in (see "Review findings") |
| Architect, re-check | L1-L3, L5, L6 closed; L4 and L7 partial; N1-N6 folded in (see "Review findings") |
| Architect, re-check 2 | N1, N2, N4a, N5, N6 closed; N3 exact but partial; N7-N11 folded in, then superseded by the owner's ruling of 2026-11-03 (D6) |
| Owner ruling, 2026-11-03 | P1 and D6's mechanism; D6, D7, T3, T4, Q2 and the H1 section rewritten; T9 added. Awaits its preliminary review |

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

The authority stays one consensus group (`src/detent_authority/service.cc:1467-1468`). What changes is the position: each namespace counts its own applied changes. Apply already keeps a revision per namespace (`state.cc:1002`), but it stores the global revision of the namespace's last write (`:1001-1003`). It becomes the namespace's own counter, incremented by one for each transaction that applies and changes a key in that namespace (the branch at `:956-1004`). A refused transaction (`:912-990`: unknown or fenced session, retained retry, sequence gap, conflict, capacity) and a transaction that changes nothing (`:1006-1007`) get no position. A record's revision is the namespace position of its last change.

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
2. **Blocks** (p ≤ R, below the ring): one derived block [R + 1, s], holding each key's last change in (R, s], then the ring's entries (s, h].
3. **Full copy** (R < p, a store from another incarnation, or no store): the state at s, then the ring's entries (s, h].

**Outcomes from apply.** The ring is today's `ChangeHistory` (`change_history.hh:23-67`): apply hands it each applied transaction's changed operations through `AppliedChangeRecorder` (`change_recorder.hh:18-32`, called at `state.cc:1480`), each key with its full definition or a delete. It keeps its bounds, 4,096 transactions and 16 MiB (`change_history.hh:9`), and is keyed by namespace position. A refused or unchanged transaction has no position and never enters it; positions are contiguous, so a gap is a source failure, never a silent skip. Every operation is absolute (`docs/AUTHORITY_ARCHITECTURE.md:93`, `service.cc:798-810`), so applying the changes after any R is exact. A producer learns its outcome from `/v2/result` and the retained receipts (`state.cc:1009-1014`), as today.

**Seal.** When the ring's unsealed entries reach half its bound (2,048 transactions or 8 MiB), apply seals: every namespace with unsealed entries gets a level-0 block covering them. The trigger is counted at apply, so every voter seals at the same log index; no command is proposed. A builder off the owner executor deduplicates the namespace's entries by key, last write wins, sorts by key, and writes the block durably. The ring evicts an entry only once a durable block covers it. Time adds nothing a seal needs: the ring's memory, recovery's replay and an answer's bytes are all counted in transactions or bytes, and a time trigger would need a proposed command because apply has no clock.

**Block.** A block [a, b] of namespace N holds, for every key written in positions a to b, that key's last write in the range: its full definition, or a tombstone for a delete, with the revision of that write. Records are sorted by key in a canonical encoding (today's delta encoding per record), so the bytes depend only on (N, incarnation, a, b): every voter and relay that builds it writes the same file, named by that tuple, with a SHA-256 digest. Blocks are immutable and cacheable by name.

**Squash.** After each seal, the two newest blocks above the base merge while the older is less than 4 times the younger and the merge stays within α · |base| / 4. A merge is a k-way pass over key-sorted blocks in which the younger record wins; the result is the block over the union of the ranges. The ratio keeps O(log) blocks, fine near head and coarser with age and size; the size limit keeps the oldest blocks small enough that the cut drops history in steps of a quarter of its budget.

**Cut.** The oldest block joins the base, which drops its tombstones, while either holds:

1. **Cost:** an answer from that block's end would carry at least α · |base| bytes. The authority counts it with a merge over the younger blocks that writes nothing, when their summed bytes first pass α · |base|.
2. **Disk:** the blocks above the base hold more than α · |base|.

An answer only grows as R moves back, so the cost rule never cuts a store whose answer is below α · |base|. The disk rule bounds what a namespace keeps when its blocks repeat keys; the store it cuts gets a full copy that costs at most 1/α times its answer. α is 1 until T9 sets it from time, not bytes: a block answer costs the agent apply CPU per record that a full copy's one-pass build may not, so the time parity can land below byte parity. The trade at 0.5, 1 and 2 is below.

**The base** is the namespace's state at p, its end: live records with their revisions and no tombstones, since no store below p is answered from blocks. It is built by the cut's merges. A namespace's first base comes from a checkpoint image, which carries each record's revision (`ImageEncoder`, `state.cc:1663`) in key order, so one prefix range per namespace: once when S5 lands on an existing authority, and once per new incarnation, from the restored image. No capture of live state is pinned for it.

**Answers.** A derived block [R + 1, s] is the merge of the persisted blocks from the one containing R + 1 to the newest, keeping records whose revision is above R. The full copy at s is the merge of the base and every block above it, without tombstones. Both are named like any block ((N, incarnation, R + 1, s), and (N, incarnation, s) for a copy) and cached by name. An answer may end at an older x a server holds in cache; the session continues from x, so a fleet started from one image shares one merge per relay. The agent applies the pieces in order and publishes one generation (D8), so readers move from R to h with nothing in between.

**The proof.** Write st(x) for the namespace's state at x. Take a ≤ R + 1 ≤ b and apply, to a store at st(R), the records of block [a, b] whose revision is above R. For a key k:

1. Written in (R, b]: since (R, b] is inside [a, b], k's last write in [a, b] is its last write up to b, and it lies above R. The record is applied and sets k to st(b)(k), with st(b)'s revision, or deletes it for a tombstone.
2. Not written in (R, b]: st(b)(k) = st(R)(k), and k's record, if any, has a revision at or below R. It is dropped and the store keeps k.

So the result is st(b) with st(b)'s revisions. A record dropped for revision ≤ R is exactly a key the store already holds at that value: overlap needs no rewrite. A delete then a re-insert (the owner's o12 and o22) leaves the re-insert as the last write; a delete then nothing leaves the tombstone; a tombstone for a key created and deleted after R is a delete of a key the store never held, a no-op. Merging blocks [a, b] and [b + 1, c] with the younger record winning gives each key's last write in [a, c], which is block [a, c], so a derived block equals the persisted range it is cut from. No revision goes backwards (reader.md A4): k's revision in st(b) is its last write up to b, never before its last write up to R. Positions move from R to h in one publication.

**Bounds.** A block answer carries each key changed in (R, s] once, so its bytes are at most the changes since R, and below the T3 bound of twice that. Persisted blocks served whole instead (the trimmed oldest and every younger block, the owner's [o101-o130], [o131-o200]) send keys rewritten across blocks once per block: the model gives 1.30 to 1.52 times the missing bytes, and a full copy built from the base plus blocks up to 2.00 C at α = 1. One derived block also caps an answer at one block and one request, so the per-block terms of T9 vanish. Per namespace, disk is the base, at most α · |base| of blocks, and one block being rewritten, plus the ring in memory. The model, with C the 5M corpus's 261.3 MB of logical wire state (measured, 52.3 B per record) and tombstones at 34 B (estimate), at one year and worst over the merge sawtooth:

| Churn (stated, not measured) | Missing after 1 day / week / month / 6 months | Cut back, α = 0.5 / 1 / 2 | Disk, α = 0.5 / 1 / 2 | Block bytes written per day, α = 1 |
| --- | --- | --- | --- | --- |
| Moderate: 1M key writes a day, 80% to 50K hot keys; 10K creates, 10K deletes | 13.7 / 72.5 / 211 / 323 MB (0.05 / 0.28 / 0.81 / 1.24 C) | 8-11 d / 19-24 d / 46-55 d | 1.50 / 1.99 / 2.98 C | 556 MB |
| Light: 100K a day, same skew; 1K creates, 1K deletes | 3.2 / 10.4 / 34.8 / 154 MB | 84-94 d / 208-218 d / past the year | 1.43 / 1.92 / 2.36 C | 53 MB |
| Hot-heavy: 10M a day, 99% to 50K keys; 1K creates, 1K deletes | 7.9 / 37.4 / 123 / 268 MB | 18-23 d / 39-51 d / 113-123 d | 1.49 / 2.00 / 2.95 C | 2,987 MB |

At α = 1, a moderate store a week old gets 72.5 MB instead of 261 MB, and one a month old gets the full copy, because the disk rule cut at three weeks. Tombstones are why a store can miss more than C: after six months of moderate churn 1.82M keys were deleted, 62 MB of tombstones on top of every live key rewritten. The cut stops that: those stores get the copy. Seals come every 2.9 min (moderate) to 0.3 min (hot-heavy) at one key per transaction, the worst case for the ring; real transactions carry more keys, so seals are rarer. Block bytes written are each byte's rewrites through the merges, about 10 times the deduplicated churn.

**Who builds, where blocks live.** Every voter seals, squashes and cuts from its own apply output, so the group needs no new replication; blocks are derived data, like a checkpoint. They live on each voter's disk beside the journal, and on each relay (D7). An object store may hold them for relays, since they are immutable named files; nothing requires one. A voter whose recorder cannot capture an entry (today it records a gap, `state.cc:1481-1486`) or whose disk refuses a block keeps applying, stops answering below its ring, and fetches the block by name from a peer, checking its digest against its own build when it has one. A voter installed from a snapshot fetches the blocks at or below the install point from its source the same way.

**Checkpoints and restart.** The checkpoint is captured at a seal's index and persisted once that seal's blocks are durable. Consensus compacts only through a checkpoint the host captured (`consensus/ARCHITECTURE.md:105,109`), so every compacted entry is already in a durable block: that is the compaction gate, and it is Detent's choice of checkpoint index. Today a checkpoint falls due at half the live log (`node.cc:511-516`), 32,768 entries at consensus's default, which hold at least 32,768 transactions, sixteen seals, so a due checkpoint waits at most one seal. Recovery restores the checkpoint and replays the journal above it through apply, which refills the ring with every entry above the checkpoint and re-seals at the same indexes; blocks already on disk match by name and digest and are kept. Today's reset after restart (`change_history.cc:364`; history attached only after replay, `service.cc:1693-1705`) goes: no cursor is lost to a restart. The ring holds only entries above the newest durable seal, which are above the checkpoint, so its worst case is the live log's own bound (`log_entries` 65,536, `log_bytes` 64 MiB, `consensus/include/consensus/types.h:58-59`): if blocks stall, consensus's existing backpressure stops writes before memory grows.

**Server cost per answer** (estimates; T4 and T9 measure them): a ring answer reads memory, as today. A block answer reads the blocks from R's to the newest, at most α · |base| plus one block, merges them, and writes its answer once to the cache. A full copy reads the base and every block, at most (1 + α) · |base|, and writes |base| once per seal it is asked at.

A position above head gets no answer until the source catches up, so no namespace goes backwards (D1).

### D7. Relay: a mirror of blocks, base and ring

A relay is a reader, not a member of the group (ND-R7.2; `SCALING.md:52`: agents and relays do not vote). Per namespace it keeps what the authority keeps: the base, the blocks above it, and a ring of the entries after the newest block, mirrored from the session stream (W5), plus a prepared generation in the agent's layout (D8) for agents that bootstrap. It answers with the same three tiers (D6). It fetches each new level-0 block by name from the authority or an upstream relay, and runs the same squash and cut, whose rule depends only on block bytes and α, so it builds the same merged blocks and checks their digests against the upstream's list; a relay that starts late fetches the current base and blocks once. Its exact-base edges (`include/detent_relay/configuration.hh:10`) stay the hot path, per namespace: any R inside its ring is an edge. Blocks and full copies are immutable, so any HTTP cache that authorizes per namespace serves them, and a fleet started from one image shares one derived block per relay. Bundles become upstreams, credentials grant namespace patterns instead of bundle indices (`docs/RELAY.md:47-60`), and each namespace is served once it is ready instead of after every bundle (`RELAY.md:78`). Disk per relay is the authority's per namespace followed: about 2 C at α = 1 in the model, beside the prepared generation (1.51 GB at 5M).

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
| Strata segments as delta blocks | Open, pending strata's measurement. A delete or replace sets a live bit in an older segment (`index.hh:1745-1756`), so a segment alone carries no position range; the Xapiand lead notes that a segment's changing delete state can ship as a per-segment bitmap delta in a manifest-relative install. Strata parks its own "pay only for what you are missing" evaluation for later. Detent's blocks are chosen on their own numbers (D6) | `strata/include/strata/core/index.hh:1745-1756`; `docs/ARCHITECTURE.md:98-106`; `merge_policy.hh:16-60` |
| Relays shipping segments for (R, h] | Open, pending the same measurement of manifest-relative installs. Physical segment files differ between replicas (`REPLICATION.md:86-99`); Detent's relays ship blocks, which are byte-identical by construction (D7) | `REPLICATION.md:86-99` |
| Agent store as one merged strata segment per namespace | Not now. A point read is a fuse filter, a block-tree term dictionary and a stored block (LZ4, zstd or raw), unmeasured against the 268.5 ns target; reads go through an index or view in one process (`get` returns `std::optional<std::string>`); the agent needs read-only attach from other processes without allocation, and page sharing between generations for publication proportional to the change | `format.hh:102-114`; `index.hh:1197-1209`; `docs/design/shared-reader.md` |
| Many groups per node | Designed, not built: coalesced heartbeats and quiescence (Track C2, D12), forced checkpoints and snapshot pacing (D13) | `docs/design/track-c.md:18,627,669` |
| Maturity | Phase 3; the README says not for production storage until the key-value rows pass their gate; `strata::group` is designed, and the tree holds only `strata/core` | `docs/STATUS.md`; `README.md:112-116` |

**H1 is withdrawn.** It asked consensus for three things; blocks need none of them:

1. **Apply output.** Detent's apply already hands its changed operations to `AppliedChangeRecorder` (`change_recorder.hh:18-32`, `state.cc:1480`); the ring and the block builder consume them inside Detent. `Applied { through }` (`consensus/include/consensus/types.h:184`) stays as it is.
2. **A compaction gate.** Consensus compacts only through a checkpoint the host captured (`consensus/ARCHITECTURE.md:105,109`). Detent captures its checkpoint at a seal and persists it after that seal's blocks are durable (D6), so no entry is compacted before a block covers it.
3. **History read by range by non-members.** Relays read blocks and the ring through the session endpoint (D7); nobody reads the journal.

What changes for the Xapiand agreement: Detent asks nothing of strata or consensus, so the agreed H1 shape (retained journal history with a horizon and a byte budget, typed "too old", no reader holding compaction) has no Detent consumer. Its condition "the journal itself, no parallel delta store" is superseded for Detent by the owner's ruling: blocks are delivery history derived from apply, not a second replication path; voters still replicate only through consensus and the journal. A smaller H1 (an apply-output interface and a compaction gate in `strata::group`) is strata's to take up for Xapiand's change capture if it wants one; nothing in Detent waits for it.

**Recommendation:** adopt strata's replication shape (one group, journal, deterministic apply, checkpoint and install), keep Detent's state machine and paged agent store, and do not use the strata core or segments in delivery until strata's measurement of segment deltas says otherwise.

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
| T3 Seeded start | Upstream blocked: first correct read within T1's bound. Upstream open: at head within 2 s; no full copy for a store at or above the cut; wire bytes at most the projected changes (each changed operation counted once per transaction), since a block answer carries each key once; no refused write reaches the agent; 5M at most 1.5 times 10K | Build an image with `detent-image` at R; apply 1,000 changes over 10 namespaces (100 deletes, 50 re-inserts of deleted keys, 200 rewrites of 20 hot keys) and 1,000 refused writes; restart every voter and the relay; start the agent from the image. Repeat with R inside the ring, below the ring inside a block, inside the oldest block above the base, and below the base (a full copy, counted "cut" in health) |
| T4 Blocks | (1) A block at (namespace, incarnation, a, b) is byte-identical on every voter and relay. (2) The ring never evicts an entry no durable block covers; after a restart of every voter and the relay, every answer for an R at or above the cut is unchanged (no reset). (3) The layout follows the rule (ratio 4, merge limit α · base / 4, cut by cost and by disk), and disk per namespace is at most the base plus α · base plus one block being rewritten. (4) A voter installed from a snapshot fetches blocks and answers the same bytes. (5) A block write failure on one voter leaves apply running; that voter answers from its ring only and recovers the block from a peer. (6) Seal, merge, cut and answer-build times and server bytes read per answer, recorded per scale (no target until measured). (7) With the model's moderate churn scaled to 100K keys, cut age and disk within 20% of the model's scaled values | Unit tests over the builder, squash and cut with an oracle that replays every position; the system harness at each scale with the churn generator, voter, leader and relay restarts, a snapshot install, and an injected block write failure |
| T5 Corruption | 100 single-byte flips in random pages: restart is ready; the first touch returns `DETENT_SOURCE_FAILURE` for that namespace only, never a wrong value; the scrub reports untouched flips within one pass; reads recover after repair, at the next build ordinal. A corrupt table or `.read` refuses the store or the namespace | A test over a 100K store written by the reader's test helper |
| T6 Identity | Every generation and segment file (`.image`, `.read`, `.pack`, `.owner`) identical between `detent-image` and an agent's full-copy build, and across two `detent-image` runs; block and full-copy bytes identical from every voter and relay. `.lease` and `owner.lock` are excluded (host and process state) | Per-file SHA-256 at each scale, on macOS and on Linux, and across the two |
| T7 Durability | After an interruption at every primitive of publication, adoption, the startup barrier and the temporary-name removal, then simulated power loss, every file the durable table reaches exists and verifies; no table served before the cut is reverted | The blueprint's crash-cut matrix (`STORAGE_BARRIER_ARCHITECTURE.md:48`), with completion's sync seam (`posix.h:496`) recording what reached a flush and discarding the rest; plus restart-then-republish: a cut in publication, a restart, the same input published again over the reused names, and a power cut at every step of that. A name made durable with unsynced bytes is unlinked and rebuilt, and the namespace publishes (no wedge, N1). Revert check: renaming the table before its fsync fails T7 (N6) |
| T8 Groups (decides S10; gates no step before it) | Per voter, at 4,096 groups and at a bounded count (64, an estimate): idle CPU at most 2% of a core; 2,000 transactions/s spread over them with p99 commit at most the platform's barrier p99 plus 20 ms; memory at most 64 MiB above one group's; file descriptors at most 2 per group plus 256; after a voter restart, every group led and committing within 10 s, with at most one election per group it led. All estimates; `docs/SCALE_TARGETS.md:117` is the throughput baseline | The authority capacity harness with the 5M corpus spread over the namespaces, on Linux ext4 and on the reference host, with strata Track C2 D12 and the shared turn log in place; RSS and open descriptors sampled each second |
| T9 Cut point (sets α) | No target: it measures T_blocks(x) = x / L + one round trip + the agent's apply time for a block answer of x bytes, and T_base = C / L + one round trip + the agent's build time for a full copy, and sets α to the x / C where they cross, at L = 1 and 10 Gb/s (estimates of fleet links). It records relay CPU and bytes read per answer of each kind | On the 5M corpus, reference host and Linux: the agent at a prepared store applies block answers of 0.05, 0.25, 0.5 and 1 C (keys spread uniformly, 5% tombstones) and builds a full copy, five runs each under `quiet exclusive`; fit apply time against x; the relay builds each answer from a block layout produced by the model's moderate churn |

## Format and wire changes

| # | Change | Today |
| --- | --- | --- |
| W1 | `Cursor` is {cluster, incarnation, position}, all the namespace's | `cursor.hh:8-13` |
| W2 | Scope identity removed | `scope.hh`; `AUTHORITY_EXPORT.md:5-9` |
| W3 | Authority: per-namespace position counters and incarnations at apply; record revisions are namespace positions; the directory stream; the ring keyed by namespace position; seals at apply; blocks and the base on disk; checkpoints at a seal | `state.cc:1001-1003`; `state.hh:46-64`; `change_history.hh:9`; `node.cc:467-516` |
| W4 | Producers: sessions, contiguous sequences and `/v2/result` unchanged on one group; preconditions are namespace positions and carry the incarnation. When S10 adds groups, a producer holds a session and sequence per group it writes and reads results per group; a transaction stays in one namespace (`SCALE_TARGETS.md:118`), so in one group | `AUTHORITY_ARCHITECTURE.md:16`; `state.cc:938,943-951`; `service.cc:827` |
| W5 | One session endpoint (Q4): open and next carry `[{name, cursor}]`, or `all` with a directory cursor, and a `wait_ms`. Poll is next with `wait_ms` 0 every `poll_interval_ms`; `POST /v2/distribution` and `delivery_mode` go | `scope`, `namespaces`, `base` (`AUTHORITY_EXPORT.md:63`); `ACTORS.md:97-100` |
| W6 | Per-namespace frames: ring entries after R; a derived block [R + 1, x], then the answer from x; or a full copy at s, then the answer from s. Blocks are named (namespace, incarnation, first, last) and full copies (namespace, incarnation, position); both are immutable and fetchable by name, and an agent applies a tombstone for a key it lacks as a no-op. `X-Detent-Kind` and `X-Detent-Revision` go | `AUTHORITY_EXPORT.md:71` |
| W7 | A new incarnation is delivered as a full copy under the new cursor; agents publish it in the namespace's existing table entry and `change` slot, and readers switch generations (Reader alignment, A4) | `reader.cc:58-61` |
| W8 | Relays are not members: they read W5 and W6 from the authority or an upstream relay and mirror blocks, base and ring. Configuration: upstreams and one store; grants by namespace pattern | `RELAY.md:47-60` |
| W9 | Agent and relay store layout: one directory with `selected`, `change` (reader.md D4), `owner.lock`, generation and segment files, `.tmp-` names | `generation.hh:23-31`; `docs/ACTORS.md:104-113` |
| W10 | A paged root holds one namespace and the new cursor | `paged_layout.hh:30-37` |
| W11 | Derived generation and segment ids with layout version, profile and build ordinal; no storage identity in `.read`, `.owner`, `.lease`; files published by `linkat` from a temporary name | D8's citations |
| W12 | `.read` gains the manifest digest | `reader_metadata.hh:7-13` |
| W13 | Grants accept exact names, whole-component prefixes and `*` (Q1); `namespaces` becomes optional in `managed.json` | `service.cc:446`; `src/detent_delivery/configuration.cc:156` |
| W14 | Completion `SyncFiles` and `OrderFiles` primitives over read-only or read-write descriptors (library API) | `operation.h:39`; `posix.h:731` |

## Alternatives considered

**Persisted blocks served whole** (the trimmed oldest block, then every younger block as stored). Every piece is immutable and shared across R, but a key rewritten in several blocks crosses the wire once per block: 1.30 to 1.52 times the missing bytes in the model, a full copy assembled from the base and blocks up to 2.00 C at α = 1, and one request per block. A derived block costs the server about the same reads, since the oldest block it trims dominates them, and sends each key once. Not selected.

**A sliding window of raw delta bytes, W = k · C, with a held Replace past it** (the previous D6, with build cycles N7 and a cycle pin N8). W counted every change, so a hot key churning used it up: at k = 1 it reached back 0.5 days under the hot-heavy churn, 4.9 under moderate and 49 under light, against cost cuts of 39-51, 19-24 and 208-218 days at α = 1. Compacted cover grows with distinct keys changed and saturates near C, so the cut follows what a store misses, not how often keys change. A store past W also got a full copy built from a live capture of state, which needed the build cycles and pin; the base is persisted, so nothing pins state. Replaced.

**The journal as the only history** (strata proposal H1). Consensus would retain entries and per-command result records below compaction, readable by range by non-members, under a horizon and a budget. A consumer behind the horizon still got a full copy, and uncompacted entries repeat hot keys as W did. Replaced by blocks derived from apply, which need nothing from consensus ("Strata fit").

**Tombstones kept for the incarnation's life.** Every store of any age would get a delta, but a store older than the cut misses more than a full copy holds: in the moderate model, after six months every live key changed and 1.82M keys were deleted, 1.24 C of answer against 1.00 C of copy, and the base would grow by 124 MB of tombstones a year. The cut drops what can never beat the copy. Not selected.

**Changes computed from state plus retained tombstones.** Exact and bounded by the namespace, but each answer scans the whole state by revision and caches poorly. Blocks are that idea with the scan bounded by the age of R. Not selected.

**Keep the global revision.** A namespace's position moves with unrelated writes, an image cursor cannot serve applications reading different namespace sets, and positions would change meaning when groups split. Not selected; D3 keeps one group but gives each namespace its own counter.

**A group per namespace now** (the draft's D3). It does not fit today's stack: 4,096 idle groups cost three times T8's CPU target before any I/O (ND-R1), and Track C2 is not built. Deferred to S10, behind T8.

**Propose effects instead of commands.** The leader would apply before proposing, so the journal would hold outcomes directly; but it changes what consensus replicates and computes outcomes against state the leader has not committed. Apply's recorder output gives the same answer on every voter. Not selected.

**Fresh random ids.** They rule out reuse (`coordinator.cc:211`) but lose byte identity between an image and an agent's build. Derived ids with no-replace publication and verified reuse keep both (D8).

**Strata segments for history, relay caching or the agent store.** Open for the first two, pending strata's measurement; not now for the store. See "Strata fit".

**A `selected` file per namespace.** A reader following all namespaces would open one file per namespace on its first read. Not selected.

**Verify at restart on every core, or check a manifest at every start.** The first is still O(N) per restart; the second reads every owner record and still trusts the pack bytes. The manifest feeds the scrub instead.

## Limits

1. **Silent:** a corrupted page nobody touches stays undetected until the scrub reaches it, up to one `scrub_period` plus a pass. A page a long-lived reader verified before the corruption is served under its verified bit until the scrub, since the reader never re-hashes a verified page (reader.md D3; ND-R8).
2. A store below the cut gets a full copy, by design; health counts full copies by reason (cut, incarnation, no store, repair). Under the disk rule the copy can cost up to 1/α times the answer it replaces: at α = 1, a one-month moderate store gets 261 MB where its changes were 211 MB.
3. A new incarnation gives every consumer of the namespace a full copy once: a store from a restored backup's predecessor holds positions the new history does not have.
4. Until S10, the ring is per group: a cold namespace's entries are evicted by other namespaces' writes, so its consumers reach blocks sooner. Blocks and the cut are per namespace.
5. The cut and disk numbers are from a churn model with uniform cold writes and a fixed hot set (D6). A namespace whose churn differs gets different cut ages; T4 checks the model against a run, and health reports each namespace's cut position and block bytes.
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
| S5 | Authority. S5.1: per-namespace positions and incarnations, the directory stream. S5.2: the ring keyed by namespace position, seals at apply, the block builder, squash and cut, the base (first one from a checkpoint image), checkpoints at a seal, recovery that refills the ring, block fetch from a peer, the three answers, T4 | S5.1: Q5's call. S5.2: S5.1 (nothing from consensus or strata) |
| S6 | W1-W8 and W13: cursor, producers, the session endpoint, frames, incarnations, relays as readers | S5, Q1 and Q4 calls |
| S7 | D8's store: one directory, the table, `change` (reader.md S5), derived ids with `linkat` publication and verified reuse, tombstones for absent keys as no-ops | S6 |
| S8 | D7's relay mirror of blocks, base and ring, and immutable objects | S6, S7 |
| S9 | D9: `detent-image`, read-only adoption, `.lease` created at adoption, ownership and the ordinal-1 fallback with a fresh table, T6 | S1, S7, Q3's call |
| S10 | The group count: bounded groups or one per namespace, with per-group producer sessions (W4) | T8 on Linux; strata Track C2 D12 built; the shared turn log |

What I want from the next check:

1. D6's proof: a block trimmed at R and merged with younger blocks yields the state at its end with that state's revisions, including deletes, re-inserts, tombstones for keys a store never held, and A4.
2. D6's compaction gate as a checkpoint at a durable seal: no compacted entry without a durable block, and the ring bounded by the live log.
3. The cut: by cost and by disk, the 1/α bound on what a cut store pays, and whether the model's assumptions are fair to the 5M corpus (T4.7, T9).

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
