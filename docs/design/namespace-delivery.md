> Status: DRAFT (2026-11-03) Designing, preliminary review folded in (ready for re-check of L3-L7). D1 and D2 are the owner's rulings; everything after them is the designer's draft, revised on `design/namespace-rev` against [the preliminary review](../reviews/namespace-delivery-preliminary.md). Q1-Q5 carry lead calls the owner may reverse; strata proposal H1 waits on strata's owner and the Xapiand lead. Ledger: [status/namespace-design.md](../status/namespace-design.md).

# Namespace delivery

Each namespace has its own position and history, an agent starts from a store shipped in the application image, and a restart costs the same at 10K knobs as at 5M.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03.
- Base: `Detent:d4abd4a`; other repositories at `completion:576618d` (Detent's pin), `consensus:61149a2` (main; Detent pins `666cdfc`), `strata:9d52824`. Line citations are at those commits.
- Input: the startup attribution, `docs/perf/agent-startup-2026-11-03.md` on branch `explore/agent-startup` (`Detent:e8d4e59`). Every measured number here comes from it (Intel i9-9980HK, macOS 15.8.1, warm page cache) or from the review's probes, which say so.
- Depends on: [reader.md](reader.md) D4, whose `change` file shape D8 adopts (RD-5); the lead's L1 ruling, one store directory (ledger); strata Track C D12 and D13 for the group count (D3, T8).

| Review | State |
| --- | --- |
| Architect, preliminary | [APPROVE WITH CONDITIONS](../reviews/namespace-delivery-preliminary.md); L1-L7 and C1-C2 folded in (see "Review findings") |

The design covers namespace-granular delivery, agents seeded from an image shipped with the application, and fast agent restart. Classic mode and flat-v1 are being deleted (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`), so it covers the authority, relay and managed agent only.

## Decisions settled by the owner

**D1. The namespace is the one unit.** A namespace is the unit of atomic writes, consistent reads, subscription, revision, cursor, delta retention, relay caching and consensus-group placement. Detent offers no read consistency across namespaces: a client sees each namespace as a consistent snapshot that never goes backwards, independently of the others. Knobs that must change together, or be read as of one moment, belong in one namespace. Ruled 2026-11-03.

**D2. All namespaces by default.** An agent or client that names no namespaces follows every namespace its credential allows, including namespaces created later. A namespace list narrows the subscription; it is never required.

## Abstract

Today one cursor covers a whole subscription (`include/detent/cursor.hh:8-13`), and the history that could serve a delta lives in memory: 4,096 transactions on the authority (`src/detent_authority/change_history.hh:8-10`, reset at `change_history.cc:364`), 64 exact-base edges on the relay (`src/detent_distribution/update_cache.cc:50`). An agent seeded from a week-old image gets a full snapshot, **722.6 s** at 5M knobs, and a restart takes **118-122 s**.

The design takes strata's replication shape (`strata/docs/REPLICATION.md`) instead of inventing a delta store: the journal is the only history, and a consumer too far behind gets the namespace's state instead (D6). Each namespace's position is its own counter, assigned at apply, so cursors, frames, the store, relays and images do not depend on how many consensus groups there are (D3); the authority stays one group until strata's multi-group work exists and T8 is measured on Linux. The agent keeps one prepared generation per namespace under one table in one directory (D8), trusts what it prepared or was seeded with while checking pages on first touch (D10), and replaces per-file flushes with one barrier (D11). One general strata capability, retained journal history read by range (H1), goes to its owners. The gate rows ask for a restart of at most **0.3 s at every scale** and half today's bootstrap. Nothing here is built or measured yet.

## What the measurements say

| Cost (5M) | Cause | Decision |
| --- | --- | --- |
| Restart, 52 s hashing | Recovery hashes every byte six times (`paged_dependency_discovery.cc:117-131`, `paged_page_validator.cc:74-131`, `paged_graph_validator.cc:201-221,266`) | D10 |
| Restart, 55 s sync | The startup census (`StartupOperation`, `src/detent_local/disk_store.cc:71`) syncs every file (`:133`, `:171`); completion's file sync is `F_FULLFSYNC` on macOS (`completion/posix.h:158-165`) | D11 |
| Bootstrap, 239 s sync (33%) | Per-file `F_FULLFSYNC` and directory syncs in relay and agent publication | D11, D12 |
| Bootstrap, 112 s hashing (15%) | Each tier re-validates its own build; the relay re-hashes on recovery | D10, D12 |
| Bootstrap from an old image | Authority history is in memory, empty after restart (`service.cc:1396,1631`); the relay serves exact-base edges only | D6, D7 |
| Scaling | Restart 0.4 / 2.5 / 26 / 120 s, bootstrap 2.7 / 15.8 / 145 / 722 s at 10K / 100K / 1M / 5M (1M interpolated, not measured) | T1-T4 |

## Design

### D3. A position per namespace, on today's group

The authority stays one consensus group (`src/detent_authority/service.cc:1393-1394`). What changes is the position: each namespace counts its own applied changes. Apply already keeps a revision per namespace (`state.cc:1002`), but it stores the global revision of the namespace's last write (`:1001-1003`). It becomes the namespace's own counter, incremented by one for each transaction that applies and changes a key in that namespace (the branch at `:956-1004`). A refused transaction (`:912-990`: unknown or fenced session, retained retry, sequence gap, conflict, capacity) and a transaction that changes nothing (`:1006-1007`) get no position. A record's revision is the namespace position of its last change.

```c++
// include/detent/cursor.hh
struct Cursor {
	Identity cluster{}, incarnation{}; // the namespace's; new on restore or re-creation
	std::uint64_t position = 0;        // the namespace's applied-change counter; 0 is empty
};
```

Positions are contiguous, so a consumer proves it missed nothing: a frame starts at R + 1 and each entry is the previous plus one. The incarnation is drawn when the namespace is created and stored in its directory entry (D5). A restore of the group from a backup issues a new incarnation to every namespace it holds, so a position never names two states under one incarnation. Producer preconditions (`expected_namespace`, a key's `expected` and `absence_revision`, `state.cc:943-951`) keep their meaning, because they already compare within one namespace; they gain the incarnation, and a mismatched incarnation is a `Conflict`. That closes the ABA case a restore, or a future delete and re-create, would otherwise open (ND-R7.3).

D1 makes the namespace the unit of placement, not a group per namespace; `SCALE_TARGETS.md:118` and `SCALING.md:52` plan groups that each own a set of namespaces. The group count is chosen later (S10): bounded groups or one per namespace, once strata Track C2's coalesced heartbeats and quiescence exist (`strata/docs/design/track-c.md:627`, D12) and T8 is measured on Linux. The review's probe puts 4,096 idle groups at 6.3% of a core per voter at 100 ms ticks, before sockets, TLS or the journal (ND-R1), three times T8's 2%. Nothing in D4-D12 depends on that choice. Group state stays Detent's `State` (`state.hh:160-225`) behind the apply, checkpoint and install contract strata's core exposes (`strata/docs/REPLICATION.md:42-45`).

### D4. Authorization per namespace, no scope identity

`canonical_scope_identity()` (`include/detent/scope.hh`) binds one global revision to one namespace set. With per-namespace cursors nothing depends on the set, so the scope identity goes. The authority and relay authorize each namespace of each request against the credential's grants. An empty list means every granted namespace (D2), reversing "an empty request produces an empty scope" (`docs/AUTHORITY_EXPORT.md:7`). Grants name exact names, whole-component prefixes (`payments.*`) and `*` (Q1).

### D5. The namespace directory

The directory is one more stream with its own position: entries (name, incarnation), one position per creation or new incarnation, delivered like a namespace. A producer's first write to an unknown name creates the namespace there, keeping today's implicit creation (`state.cc:959`). The server filters the directory by the requester's grants. An agent following all namespaces holds the directory's cursor; a new entry makes it subscribe from position 0. The authority never deletes a namespace today (its map is only ever set, `state.cc:1002`); if deletion arrives, a re-created name gets a new incarnation (D3).

### D6. History is the journal, with outcomes

The journal the group already writes is the history: no second log or delta format. Three rules make it serve consumers.

**Outcomes, not commands (ND-R3).** The journal holds commands, and apply can refuse one. So apply emits, for every entry, a result record of at most 24 bytes: the namespace position it assigned (0 when refused or unchanged) and one bit per operation that changed a key (at most 128 operations, `AUTHORITY_ARCHITECTURE.md:18`). The history keeps that record beside the entry. Apply is deterministic, so every voter writes the same records, and recovery's replay above the checkpoint rebuilds any it lost. A consumer receives only entries with a position, projected to their changed operations (key and full definition, or `null` for a delete) in today's delta encoding (`AUTHORITY_EXPORT.md:47-51`), without sessions, sequences or preconditions. A refused write never reaches a consumer: it has no position, and positions are contiguous, so a gap is a source failure, never a silent skip. A producer learns its outcome from `/v2/result` (`service.cc:778`) and the retained receipts (`state.cc:1009-1014`), as today.

Applying entries after any R is exact because no operation is relative: an operation is a key plus a full definition, or `null` (`docs/AUTHORITY_ARCHITECTURE.md:79`, `service.cc:750-760`), and preconditions were checked once at apply.

**Where it lives (ND-R10).** History is kept on disk below the consensus compaction point and read by range by processes that are not members: strata proposal H1 ("Strata fit"). A per-namespace position index (position to log index), derived at apply and rebuilt by replay, sits beside it with the same horizon, so a small namespace in a busy group is read without scanning. Compaction waits until the history is durable through the index it compacts. Readers serve sessions on their own executors and are woken at apply; the `Core`'s executor only appends. The in-memory `ChangeHistory` goes.

**Retention lags, with a floor (ND-R2).** Retention no longer resets at a checkpoint, so it has no sawtooth:

| Rule | Value |
| --- | --- |
| Window | Keep namespace N's newest entries until their bytes reach W = max(k · C, F); C is the bytes of N's canonical state at head, tracked at apply; k = 1 (Q2); F = `history_floor_bytes`, 1 MiB by default (an estimate, fixed by T4) |
| Boundaries | Sealed ranges end at positions that are multiples of S = `range_positions` (256 by default, an estimate). The window also always covers (b, h], b the last boundary |
| Replace | N's canonical state (its live records, canonically encoded) at b, followed by entries (b, h]; at h when (b, h] is not retained. The same object from every voter and relay at one position |
| Selection for (R, h] | Same incarnation and R + 1 retained: send the sealed ranges after R and the live tail when their bytes are at most the Replace's; otherwise the Replace |
| Group horizon | While namespaces share a group, the group keeps history back to the oldest entry any window needs, capped at `history_budget_bytes` (2 · Σ W by default, an estimate). Past the cap, the oldest windows are clamped and their consumers below get a Replace, counted by health as "horizon" |
| Size bound | Per namespace: W plus one range plus its largest entry; per group: the budget |

A consumer that missed X bytes, X at most min(W, C), gets entries at every moment, not only just after a cut; Q2's claim that an image of any age gets entries whenever they are cheaper now holds. The floor keeps a small hot namespace from answering with Replace on every request, and S bounds how many Replace objects a namespace produces: at most one per range. History lives outside the `Core`, so the 65,536-entry default of `log_entries` (`consensus/include/consensus/types.h:59`) bounds only the live log. A position above head gets no answer until the source catches up, so no namespace goes backwards (D1).

### D7. Relay: a per-namespace mirror of ranges and Replace

A relay is a reader of history, not a member of the group (ND-R7.2; `SCALING.md:52`: agents and relays do not vote). It reads sealed ranges and Replace objects from the authority or an upstream relay through the session endpoint (W5) and keeps them, per namespace, under D6's window, plus a prepared generation in the agent's layout (D8) for agents that bootstrap. A sealed range or Replace never changes once named by (namespace, incarnation, first, last), so relays and any HTTP cache that authorizes per namespace serve it as an immutable object. A fleet started from one image asks for the same sealed ranges and shares them; only the live tail differs per caller. Bundles become upstreams, credentials grant namespace patterns instead of bundle indices (`docs/RELAY.md:47-60`), and each namespace is served once it is ready instead of after every bundle (`RELAY.md:78`).

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

**The `change` file** takes reader.md D4's shape (RD-5): word 0 is the store word, incremented after every replacement of `selected`; word 1 + slot is a namespace's sequence, incremented after every replacement that changes that namespace's entry. A slot is assigned when a namespace first appears and is never reused while the store exists; the file grows by extension, never shrinks, and is never truncated, unlinked or replaced. The agent creates it with the store, before the first `selected`, takes `owner.lock` exclusively (today's only `LOCK_EX` is the daemon's pid lock, `src/detent_cli/daemon.cc:39,63`), and bumps every word once at startup after the barrier (D11).

**File ids (L6, ND-R4).** Ids are derived from content so two builds of the same input agree, and files are published so a crash and a republication can never trust a file nobody synced:

1. The generation id is SHA-256 of a domain, the layout version, the profile, the incarnation, the position, the base generation id (zero for a Replace build) and the build ordinal, truncated to 16 bytes; today it is random (`src/detent_delivery/coordinator.cc:211`). The segment id keeps its derivation from the generation id and ordinal (`src/detent_local/paged_segment_builder.cc:38-47`) minus the storage identity. That identity, random per host (`disk_store.cc:32`), leaves `.read`, `.owner` and `.lease` (`src/detent_local/reader_metadata.hh:8`, `include/detent_local/ownership.hh:10-17`) and stays in `storage.meta`.
2. The build ordinal is 0 for an ordinary build. A repair of the selected generation at an unchanged head (D10) builds with ordinal r + 1, r the selected entry's, so it writes new inodes under new names and never touches a file the durable table reaches. Byte identity (T6) holds at ordinal 0.
3. Every file is written under a `.tmp-` name and published with `linkat` to its final name, which fails rather than replaces when the name exists; then the temporary name is unlinked. One POSIX call serves Linux, macOS and FreeBSD.
4. A name that already exists is a reused name: the publication verifies its bytes against the digest it expects (the owner record's checksum for a pack, the table's digests for a root or `.read`) and puts it in its own sync group. A mismatch fails the publication for that namespace and names the file in health.
5. Startup removes every `.tmp-` name after the barrier, which drops the second link a crash can leave (completion's `validate_file` wants one link, `completion/posix.h:116`).

The review's crash model finds 14 violations in 283 cases when reused names are trusted, and none in 439 when they are verified and synced (ND-R4); T7 repeats it on the real store.

### D9. Seeding from an image

`detent-image build --source managed.json [--namespaces a,b] [--uid UID] --out DIR` asks for a Replace per namespace and writes the generation files, `selected` with sequence 1 and `change`, without `storage.meta`. `--frames FILE` builds from saved Replace frames with no network. It runs the agent's own Replace build at ordinal 0, so its files are byte-identical to an agent's from the same Replace (T6). It shares its writer with the reader's test helper (reader.md D9).

**Where it lives.** The seeded store is an image layer, read-only under overlayfs, at the agent's `generation_dir`. Adoption never opens a seeded file for writing (ND-R6): it opens each `O_RDONLY | O_NOFOLLOW | O_CLOEXEC` and syncs them through S1's primitives, which accept read-only descriptors (L1 ruling). Completion opens files `O_RDWR` today (`completion/posix.h:731`), which would copy the 1.51 GB store up per container and lose page-cache sharing between containers. What the agent writes is small or new: `storage.meta`, a new `selected` by rename, new generation files, and `change`, the one seeded file it maps read-write (8 bytes per namespace plus 8, copied up once).

**Adoption.** An agent that finds a valid table, a valid `change` and no `storage.meta` adopts the store: it runs the adoption barrier (one sync group over every seeded file and the directory, D11), bumps `change`, reports ready, writes `storage.meta` last, and asks for (R, h] from each seeded cursor. Readiness follows the barrier, as at every start (ND-R9).

**Ownership.** Seeded files must be owned by the agent's account, with one link and no group or other write (`disk_store.cc:28-31`; completion's `validate_file`, `posix.h:113-117`). `detent-image` writes files owned by `--uid`, the agent's account by default. An agent that finds another owner does not adopt: it reports `seed_owner_mismatch` with the expected and found uid in health and bootstraps, so the 722 s path is visible, never silent.

### D10. Restart: trust prepared generations

A generation is verified once, by the build that writes it. Its `.read` is written after that verification, and the table binds the `.read` digest. Restart reads `storage.meta`, the table, and per namespace the 320-byte root and 192-byte `.read`, each checked against the table's digests: O(namespaces), no pack read, no page hash. Packs open on first use. Corruption is still caught:

1. **First touch.** A page's SHA-256 is checked against its parent reference before use, by readers (reader.md D3) and by the agent when an update reads a base page.
2. **Scrub.** A background pass hashes each pack against its owner record's checksum, paced continuously so a full pass ends every `scrub_period`. `.read` gains a manifest digest over (segment id, length, checksum), so the scrub also proves no owner record is missing. The review estimates an hourly pass over 1.51 GB at about 0.2% of a core on the reference host (ND-R8); S3's criterion sets 24 h, and the period is the lead's to tighten.
3. **Repair.** A mismatch fails that namespace alone: readers get `DETENT_SOURCE_FAILURE` for it, and the agent asks for a Replace and publishes a fresh generation at the next build ordinal (D8).

### D11. One barrier instead of a flush per file

**Invariant:** every file the durable table reaches is durable before the table entry naming it can be. Publication keeps it with the grouped order of `docs/STORAGE_BARRIER_ARCHITECTURE.md:17-26`: owner records and reused names, barrier; payloads, barrier; the table replace and the `change` increments, barrier. Each group syncs through S1's `SyncFiles`, which runs `sync_files_and_directory` (`completion/io.h:231`) over the store's one directory: every file, then the device flush, then the directory.

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
| Strata segments as delta blocks | No. A delete or replace sets a live bit in an older segment in place, so a segment holds no tombstones and its visible content changes after it is written. Tiered merges pick segments by live bytes, not adjacency, so a merged segment covers no revision range. The manifest's applied position dates a commit, not a segment | `strata/include/strata/core/index.hh:1745-1756`; `docs/ARCHITECTURE.md:98-106`; `merge_policy.hh:16-60`; `files.hh:102` |
| Relays shipping segments for (R, h] | No for deltas: physical segment files differ between replicas. Yes for its intended use, installing a far-behind consumer from one pinned manifest; D6's Replace is that path | `REPLICATION.md:86-99` |
| Agent store as one merged strata segment per namespace | Not now. A point read is a fuse filter, a block-tree term dictionary and a stored block (LZ4, zstd or raw), unmeasured against the 268.5 ns target; reads go through an index or view in one process (`get` returns `std::optional<std::string>`); the agent needs read-only attach from other processes without allocation, and page sharing between generations for publication proportional to the change | `format.hh:102-114`; `index.hh:1197-1209`; `docs/design/shared-reader.md` |
| Many groups per node | Designed, not built: coalesced heartbeats and quiescence (Track C2, D12), forced checkpoints and snapshot pacing (D13) | `docs/design/track-c.md:18,627,669` |
| Maturity | Phase 3; the README says not for production storage until the key-value rows pass their gate; `strata::group` is designed, and the tree holds only `strata/core` | `docs/STATUS.md`; `README.md:112-116` |

Reconciled with the Xapiand lead's directions: a namespace is placed whole in one group (DISTRIBUTION Track D4), the journal is the history and a Replace brings a far-behind consumer up to date (REPLICATION), and that shape fits D1 on the libraries Detent already runs. Positions are per namespace and assigned at apply; no per-segment sequence range is proposed.

**Recommendation:** adopt the replication shape now, keep Detent's state machine and paged agent store, and do not use the strata core or segments in delivery. One proposal goes to strata's owner and the Xapiand lead; nothing is built on it before they agree:

| # | Proposal (library) | Smallest general mechanism | Would Xapiand use it |
| --- | --- | --- | --- |
| H1 | Retained journal history (consensus, journal); merges the draft's P1 and P2 (ND-R10) | Each entry and a bounded result record from apply, kept on disk below the compaction point, back to a horizon the state machine supplies and under a byte budget, read by log-index range by processes that are not members, within the node budget of Track C D13 (`track-c.md:669`). Compaction waits for the history to be durable | Yes: change capture needs which commands applied and what they changed; remote replicas catch up by range instead of reinstalling |

Not proposed, because only Detent would use them: the owner's compacted blocks (the draft's P3; out until a workload beside Detent needs them), per-segment revision ranges, tombstone segments, byte-identical segments across replicas, and a cross-process mapped reader in the strata core. The per-namespace position index stays in Detent.

## Reader alignment

| Reader assumption (reader.md D2) | Here |
| --- | --- |
| A1: a selected descriptor per namespace | Changes: one table entry per namespace (D8), carrying what the descriptor did, so attaching "all" reads one file instead of one per namespace |
| A2: one record lists the namespaces | Holds: the table. "All" means its entries, which the agent already filtered by credential |
| A3: one store-wide change word | Holds and extends: `change` is reader.md D4's shape, with the store word and a sequence per namespace slot (D8) |
| A4: revisions never go backwards | Holds per incarnation. A new incarnation arrives as a Replace and a new table entry; the reader re-attaches that namespace as a generation switch and reports the new incarnation, instead of throwing as `reader.cc:58-61` does today on any identity change. A store identity change (`storage.meta`, the cluster) is still refused. reader.md must add this case |

The reader attaches a namespace on its first lookup, not at open (reader.md D2), which keeps its first read under 1 ms with thousands of namespaces.

## Gate rows

Targets come from the attribution and are fixed after the first screen. The lead measures on the reference host and on Linux, under `quiet exclusive`. 10K, 100K and 5M use preserved stores; the first T2 run at 1M preserves one.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Restart | At most 0.3 s at each of 10K, 100K, 1M, 5M; 5M at most 1.5 times 10K | The attribution's restart method: an APFS clone of a prepared store per run, wall from exec to "managed local store ready", six runs per scale, then the 40 probes of `benchmarks/prepared-pack-access-5m-20261025.json` with zero failures |
| T2 Bootstrap | At most half the recorded wall at each scale (1.36, 7.9, 73, 361 s); syncs at most 5% of wall; bytes hashed per tier at most 1.2 times bytes built | The attribution's bootstrap harness (`tests/detent_system`, adapted from `capacity-trials/restore_5m.py`) with `sample` attached; sync and hash counters from S2 and S4; on macOS and on Linux ext4 |
| T3 Seeded start | Upstream blocked: first correct read within T1's bound. Upstream open: at head within 2 s, entries only (no Replace), wire bytes at most twice the projected changes. 5M at most 1.5 times 10K | Build an image with `detent-image` at R; apply 1,000 changes (100 deletes) over 10 namespaces; restart every voter and the relay; start the agent from the image. Repeat with R at 0.1, 0.5, 0.9 and 0.99 of each namespace's window, and with 1,000 refused writes mixed in: none reaches the agent |
| T4 Retention | Entries for every namespace whose changes since R are at most min(W, C); a Replace only for the namespace pushed past it; no R inside the window gets a Replace (no sawtooth); a 1 KiB namespace written 100 times a second answers entries to consumers within F; a namespace clamped by the group budget gets a Replace counted as "horizon"; the same answers after voter, leader and relay restarts; per-namespace history bytes at most W plus one range plus 10%, the group at most its budget | T3's setup at each scale, plus enough writes to namespace A to pass its window and not B's, a small hot namespace, and a cold namespace behind enough other writes to reach the budget |
| T5 Corruption | 100 single-byte flips in random pages: restart is ready; the first touch returns `DETENT_SOURCE_FAILURE` for that namespace only, never a wrong value; the scrub reports untouched flips within one pass; reads recover after repair, at the next build ordinal. A corrupt table or `.read` refuses the store or the namespace | A test over a 100K store written by the reader's test helper |
| T6 Identity | Every generation and segment file (`.image`, `.read`, `.pack`, `.owner`) identical between `detent-image` and an agent's Replace build, and across two `detent-image` runs; Replace bytes identical from every voter and relay at one position. `.lease` and `owner.lock` are excluded (host and process state) | Per-file SHA-256 at each scale, on macOS and on Linux, and across the two |
| T7 Durability | After an interruption at every primitive of publication, adoption, the startup barrier and the temporary-name removal, then simulated power loss, every file the durable table reaches exists and verifies; no table served before the cut is reverted | The blueprint's crash-cut matrix (`STORAGE_BARRIER_ARCHITECTURE.md:48`), with completion's sync seam (`posix.h:496`) recording what reached a flush and discarding the rest; plus restart-then-republish: a cut in publication, a restart, the same input published again over the reused names, and a power cut at every step of that |
| T8 Groups (decides S10; gates no step before it) | Per voter, at 4,096 groups and at a bounded count (64, an estimate): idle CPU at most 2% of a core; 2,000 transactions/s spread over them with p99 commit at most the platform's barrier p99 plus 20 ms; memory at most 64 MiB above one group's; file descriptors at most 2 per group plus 256; after a voter restart, every group led and committing within 10 s, with at most one election per group it led. All estimates; `docs/SCALE_TARGETS.md:117` is the throughput baseline | The authority capacity harness with the 5M corpus spread over the namespaces, on Linux ext4 and on the reference host, with strata Track C2 D12, the shared turn log and H1 in place; RSS and open descriptors sampled each second |

## Format and wire changes

| # | Change | Today |
| --- | --- | --- |
| W1 | `Cursor` is {cluster, incarnation, position}, all the namespace's | `cursor.hh:8-13` |
| W2 | Scope identity removed | `scope.hh`; `AUTHORITY_EXPORT.md:5-9` |
| W3 | Authority: per-namespace position counters and incarnations at apply; record revisions are namespace positions; the directory stream; a result record per entry; canonical Replace at a range boundary | `state.cc:1001-1003`; `state.hh:46-64`; `service.cc:156` |
| W4 | Producers: sessions, contiguous sequences and `/v2/result` unchanged on one group; preconditions are namespace positions and carry the incarnation. When S10 adds groups, a producer holds a session and sequence per group it writes and reads results per group; a transaction stays in one namespace (`SCALE_TARGETS.md:118`), so in one group | `AUTHORITY_ARCHITECTURE.md:16`; `state.cc:938,943-951`; `service.cc:778` |
| W5 | One session endpoint (Q4): open and next carry `[{name, cursor}]`, or `all` with a directory cursor, and a `wait_ms`. Poll is next with `wait_ms` 0 every `poll_interval_ms`; `POST /v2/distribution` and `delivery_mode` go | `scope`, `namespaces`, `base` (`AUTHORITY_EXPORT.md:63`); `ACTORS.md:97-100` |
| W6 | Per-namespace frames: sealed ranges ending at multiples of `range_positions`, a Replace at a boundary, and the live tail. Each sealed range and Replace is named (namespace, incarnation, first, last) and fetchable as an immutable object; the tail is served live and never cached. A consumer skips entries at or below its R. `X-Detent-Kind` and `X-Detent-Revision` go | `AUTHORITY_EXPORT.md:71` |
| W7 | A new incarnation is delivered as a Replace under the new cursor; agents publish it as a new table entry and readers re-attach (Reader alignment, A4) | `reader.cc:58-61` |
| W8 | Relays are not members: they read W5 and W6 from the authority or an upstream relay. Configuration: upstreams and one store; grants by namespace pattern | `RELAY.md:47-60` |
| W9 | Agent and relay store layout: one directory with `selected`, `change` (reader.md D4), `owner.lock`, generation and segment files, `.tmp-` names | `generation.hh:23-31`; `docs/ACTORS.md:104-113` |
| W10 | A paged root holds one namespace and the new cursor | `paged_layout.hh:30-37` |
| W11 | Derived generation and segment ids with layout version, profile and build ordinal; no storage identity in `.read`, `.owner`, `.lease`; files published by `linkat` from a temporary name | D8's citations |
| W12 | `.read` gains the manifest digest | `reader_metadata.hh:7-13` |
| W13 | Grants accept exact names, whole-component prefixes and `*` (Q1); `namespaces` becomes optional in `managed.json` | `service.cc:438`; `src/detent_delivery/configuration.cc:156` |
| W14 | Completion `SyncFiles` and `OrderFiles` primitives over read-only or read-write descriptors (library API) | `operation.h:39`; `posix.h:731` |

## Alternatives considered

**The owner's compacted blocks** (`[o1-o10]`, `[o11-o50]`, ..., last write per key, tiered merges, immutable). They send less than D6 when a hot key churns, and cache as well. Under one replication mechanism they belong in the journal as a general capability or not at all; no workload beside Detent asks for them yet, so they stay out.

**Changes computed from state plus retained tombstones.** Exact and bounded by the namespace, but a second history mechanism beside the journal, and answers keyed by each base cache poorly. Not selected.

**Keep the global revision.** A namespace's position moves with unrelated writes, an image cursor cannot serve applications reading different namespace sets, and positions would change meaning when groups split. Not selected; D3 keeps one group but gives each namespace its own counter.

**A group per namespace now** (the draft's D3). It does not fit today's stack: 4,096 idle groups cost three times T8's CPU target before any I/O (ND-R1), and Track C2 is not built. Deferred to S10, behind T8.

**Propose effects instead of commands.** The leader would apply before proposing, so the journal would hold outcomes directly; but it changes what consensus replicates and computes outcomes against state the leader has not committed. A result record written at apply gives the same answer from every voter. Not selected.

**Checkpoint-relative retention** (the draft's P1). Keeping the suffix after the newest checkpoint resets at every cut: in the review's model a consumer X ≤ C behind got a checkpoint with probability X/C (ND-R2). Replaced by the sliding window.

**Fresh random ids.** They rule out reuse (`coordinator.cc:211`) but lose byte identity between an image and an agent's build. Derived ids with no-replace publication and verified reuse keep both (D8).

**Strata segments for history, relay caching or the agent store.** See "Strata fit".

**A `selected` file per namespace.** A reader following all namespaces would open one file per namespace on its first read. Not selected.

**Verify at restart on every core, or check a manifest at every start.** The first is still O(N) per restart; the second reads every owner record and still trusts the pack bytes. The manifest feeds the scrub instead.

## Limits

1. **Silent:** a corrupted page nobody touches stays undetected until the scrub reaches it, up to one `scrub_period` plus a pass. A page a long-lived reader verified before the corruption is served under its verified bit until the scrub, since the reader never re-hashes a verified page (reader.md D3; ND-R8).
2. **Silent unless watched:** a namespace that changed more than its window since an image's position gets a Replace. Correct and bounded by the namespace, but more than compacted blocks would send; health counts Replace answers by reason (window, horizon, incarnation).
3. Until S10, history is retained per group: a cold namespace behind a busy group can be clamped at the group budget and answer with a Replace earlier than its own window would.
4. A seeded image has the trust of a store the agent prepared (Q3): keys an altered image adds survive entries-only catch-up until a Replace replaces that namespace.
5. Byte identity holds for Replace builds at ordinal 0 only; generations built from entries, and repairs, differ by design.
6. D11's macOS argument rests on `F_FULLFSYNC` flushing the whole drive cache, as Apple documents and completion already assumes (`posix.h:181-185`). On Linux and FreeBSD each file's fsync is a flush; there the barrier saves syncs, not flushes.

Documents this changes once built: `docs/ACTORS.md:54-91,97-122`, `docs/RELAY.md:13-80`, `docs/AUTHORITY_EXPORT.md:5-9,29-45,63-71`, `docs/AUTHORITY_ARCHITECTURE.md:16`, `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md:23,29`, `docs/STORAGE_BARRIER_ARCHITECTURE.md:38`, `docs/SCALE_TARGETS.md:72,118`, and reader.md's A1 and A4.

## Open questions for the owner

Each carries the lead's call of 2026-11-03, made under "move fast"; the owner may reverse any.

**Q1. What can a grant name?** (a) Exact names: "all" means every granted name, and a new namespace needs a grant change. (b) Exact names, whole-component prefixes (`payments.*`) and `*`. *Recommendation: (b)*, which makes D2's "created later" reachable without editing credentials. **Lead call: (b)**, limited to those three forms; the security reviewer reviews the matcher.

**Q2. How much history?** (a) A window of k = 1 times the namespace's state, with a floor (D6). (b) A time window, such as 90 days, whatever it costs. (c) (a) plus the owner's compacted blocks. *Recommendation: (a)*: storage stays about twice each namespace, and with the sliding window an image of any age gets entries whenever they are cheaper. Not (b): its cost has no bound. (c) only if Xapiand wants compacted catch-up. **Lead call: (a)**, with lagged retention and the floor.

**Q3. Does a seeded image get the trust of a store the agent prepared?** (a) Yes: pages checked on first touch, and the scrub (D10). (b) Verify it fully before serving on first start, 52 s at 5M today. *Recommendation: (a)*: the image shares the application binary's supply chain, and (b) defeats the scenario. **Lead call: (a)**, with the residual in Limit 4.

**Q4. One-shot delivery.** That is `POST /v2/distribution` and the agent's default `delivery_mode: poll` (`ACTORS.md:97-100`). (a) Remove the endpoint and the mode; sessions only. (b) Keep both, on the per-namespace body. (c) One session endpoint with a wait time; poll becomes a zero wait every `poll_interval_ms`. *Recommendation: (c)*: polling agents keep their behavior on one path. **Lead call: (c)** (W5). It removes an endpoint, so it is the owner's to reverse.

**Q5. The authority's shape, with the Xapiand lead.** (a) A consensus group per namespace now. (b) Keep one group and the global revision, filtering its journal per namespace. (c) Wait for `strata::group` and the strata core's key-value gate. (d) A position per namespace assigned at apply, on today's one group; the group count chosen once Track C2 exists and T8 is measured on Linux. *Recommendation: (d)*: cursor, frames, store, relay and image stop depending on the group count, and (a) misses T8 on today's stack (ND-R1). The cost is Limit 3 until S10. **Lead call: (d)** (D3).

## First steps

Each step lands alone with CI green. S1 to S4 attack the measured 120 s restart and 722.6 s bootstrap on today's single-generation store, and no open question blocks them; their done criteria are in the ledger. S2 and S3 keep today's random ids until S7 (C2).

| Step | Change | Needs |
| --- | --- | --- |
| S1 | Completion: `SyncFiles` and `OrderFiles` over read-only or read-write descriptors in one directory, with seam tests per platform; lands upstream, then the pin moves | Nothing |
| S2 | D11 on today's store: grouped publication syncs, a census without per-file syncs, `ready()` after the barrier, sync counters | S1 |
| S3 | D10 on today's store: restart without the six hash passes, first-touch checks on the agent's base reads, the scrub | Nothing |
| S4 | D12.2: no re-validation of a tier's own build; hash counters | Nothing |
| S5 | Authority. S5.1: per-namespace positions and incarnations, the directory stream, result records. S5.2: history on disk with the position index, the window, Replace at boundaries | S5.1: Q5's call. S5.2: H1 agreed and built |
| S6 | W1-W8 and W13: cursor, producers, the session endpoint, frames, incarnations, relays as readers | S5, Q1 and Q4 calls |
| S7 | D8's store: one directory, the table, `change` (reader.md S5), derived ids with `linkat` publication and verified reuse | S6 |
| S8 | D7's relay mirror and immutable objects | S6, S7 |
| S9 | D9: `detent-image`, read-only adoption, ownership, T6 | S1, S7, Q3's call |
| S10 | The group count: bounded groups or one per namespace, with per-group producer sessions (W4) | T8 on Linux; strata Track C2 D12 built; the shared turn log |

What I want from the re-check:

1. D6: whether result records, contiguous positions and the sliding window give exact (R, h] in every case, including a restore, a clamped namespace and a consumer exactly at a range boundary.
2. D8: whether `linkat` publication, verified reuse and the build ordinal close ND-R4 on the real store.
3. D9: whether read-only adoption avoids copy-up with `change` the only seeded file written.
4. H1: whether it is general enough for strata's owner and the Xapiand lead.

## Review findings

| Finding | Where it landed |
| --- | --- |
| ND-R1, L3 | D3 (one group, a position per namespace); T8 (platforms, memory, descriptors, voter restart; decides S10); S10; alternatives; Q5 (d) |
| ND-R2, L4 | D6 retention (sliding window, floor, Replace at range boundaries, group horizon); T3 and T4 cases across the window; Q2; alternatives |
| ND-R3, L4 | D6 outcomes (a result record per entry, contiguous positions, `/v2/result` for producers; readers serve the tail off the `Core`'s executor); alternatives |
| ND-R4, L6 | D8 file ids (layout version and profile in the id, build ordinal for repairs, `linkat` from a temporary name, verified and synced reuse, startup removal); W11; T7 |
| ND-R5, L1 | The lead's ruling: one store directory. D8 drops `ns/<incarnation>/`; D11's barrier syncs one directory and states the Linux and FreeBSD costs; Limit 6 |
| ND-R6, L7 | D9 (image layer, read-only opens, `change` the only seeded file written; `--uid` and `seed_owner_mismatch`); W14 |
| ND-R7, L5 | W4 producers; W5 one session endpoint (Q4 (c)); W6 range boundaries and the live tail; W7 incarnations; W8 relays as readers, not learners; D7; Reader alignment A4 |
| ND-R8 | D10 scrub paced continuously; Limit 1 |
| ND-R9, L2 | D11 startup order (barrier, temporary names, `change` bump, then ready); D9 adoption; T7 |
| ND-R10 | H1 merges P1 and P2; P3 out; "Strata fit" |
| ND-R11 | T6 (two platforms, `.lease` and `owner.lock` excluded), T7 (restart-then-republish), T8 rows; done criteria in the ledger (lead) |
| C1 | Ledger: S1-S4 criteria (lead) |
| C2 | First steps: S2 and S3 keep random ids and one directory until S7 |
