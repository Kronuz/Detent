> Status: DRAFT (2026-11-03) Designing, ready for preliminary review. D1 and D2 are the owner's rulings; everything after them is the designer's draft on `design/namespace-delivery`. D3, D6 and D7 and the strata proposals P1-P3 wait on the owner and the Xapiand lead (Q5). Ledger: [status/namespace-design.md](../status/namespace-design.md).

# Namespace delivery

Each namespace replicates and delivers on its own journal, an agent starts from a store shipped in the application image, and a restart costs the same at 10K knobs as at 5M.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03.
- Base: `Detent:d4abd4a`; other repositories at `completion:576618d` (Detent's pin), `consensus:61149a2` (main; Detent pins `666cdfc`), `strata:9d52824`.
- Input: the startup attribution, `docs/perf/agent-startup-2026-11-03.md` on branch `explore/agent-startup` (`Detent:e8d4e59`). Every measured number here comes from it: Intel i9-9980HK, macOS 15.8.1, warm page cache.

| Review | State |
| --- | --- |
| Architect, preliminary | Pending |

The design covers namespace-granular delivery, agents seeded from an image shipped with the application, and fast agent restart. Classic mode and flat-v1 are being deleted (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`), so it covers the authority, relay and managed agent only.

## Decisions settled by the owner

**D1. The namespace is the one unit.** A namespace is the unit of atomic writes, consistent reads, subscription, revision, cursor, delta retention, relay caching and consensus-group placement. Detent offers no read consistency across namespaces: a client sees each namespace as a consistent snapshot that never goes backwards, independently of the others. Knobs that must change together, or be read as of one moment, belong in one namespace. Ruled 2026-11-03.

**D2. All namespaces by default.** An agent or client that names no namespaces follows every namespace its credential allows, including namespaces created later. A namespace list narrows the subscription; it is never required.

## Abstract

Today one cursor covers a whole subscription (`include/detent/cursor.hh:8-13`), and the history that could serve a delta lives in memory: 4,096 transactions on the authority (`src/detent_authority/change_history.hh:8-10`, reset at `change_history.cc:364`), 64 exact-base edges on the relay (`src/detent_distribution/update_cache.cc:50`). An agent seeded from a week-old image gets a full snapshot, **722.6 s** at 5M knobs, and a restart takes **118-122 s**.

The design takes strata's replication shape (`strata/docs/REPLICATION.md`) instead of inventing a delta store: one consensus group per namespace, its journal as the only history, and a checkpoint for a consumer too far behind (D3, D6). The agent keeps one prepared generation per namespace under one table (D8), trusts what it prepared or was seeded with while checking pages on first touch (D10), and replaces per-file flushes with one barrier (D11). The strata core does not fit the delivery path or the agent store today; three general proposals for strata's libraries go to their owners. The gate rows ask for a restart of at most **0.3 s at every scale** and half today's bootstrap. Nothing here is built or measured yet.

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

### D3. A consensus group per namespace, and its cursor

Each namespace is one consensus group (D1's placement unit), as each strata index is one shard group (`strata/docs/DISTRIBUTION.md`, "From Xapiand", Track D4). Consensus already runs many small groups per machine on the journal's shared turn log (`consensus/README.md:98`). Today the authority runs one group for every namespace (`src/detent_authority/service.cc:1394`), and a namespace's revision is the global revision of its last write (`src/detent_authority/state.cc:1002-1003`).

```c++
// include/detent/cursor.hh
struct Cursor {
	Identity cluster{}, incarnation{}; // the namespace group's; new on restore
	std::uint64_t position = 0;        // the group's applied log index; 0 is empty
};
```

A namespace's revision is its group's applied position, and every record carries the position of its last change. Group, restore epoch and scope leave the cursor. A voter replacement keeps the incarnation; a restore issues a new one. The name travels beside the cursor, in frames and in the store table. Group state stays Detent's `State` (`state.hh:160-225`) behind the same apply, checkpoint and install contract strata's core exposes (`strata/docs/REPLICATION.md:42-45`), so moving it onto strata later changes the state machine, not delivery (Q5).

### D4. Authorization per namespace, no scope identity

`canonical_scope_identity()` (`include/detent/scope.hh`) binds one global revision to one namespace set. With per-namespace cursors nothing depends on the set, so the scope identity goes. The authority and relay authorize each namespace of each request against the credential's grants. An empty list means every granted namespace (D2), reversing "an empty request produces an empty scope" (`docs/AUTHORITY_EXPORT.md:7`). Grants that cover namespaces not yet created need patterns: Q1.

### D5. The namespace directory

The directory is the state of one more group, the placement map of `DISTRIBUTION.md`'s layers: entries (name, incarnation, group members), delivered on the same machinery as any namespace. A producer's first write to an unknown name creates the namespace there, keeping today's implicit creation (`state.cc:959`). The server filters the directory by the requester's grants. An agent following all namespaces holds the directory's cursor; a new entry makes it subscribe from position 0. The authority never deletes a namespace today (its map is only ever set, `state.cc:1002`).

### D6. History is the namespace's journal

The journal each group already writes is the history: no second log or delta format. Its retained suffix survives every restart because it is on disk. The consensus `RetainedStatePolicy` bounds that suffix (`consensus/README.md:88`); this design sets it relative to the checkpoint (proposal P1):

| Rule | Value |
| --- | --- |
| Retention | Keep the suffix after the newest checkpoint until its bytes reach k times the checkpoint's bytes (k = 1 by default, Q2) |
| Selection for (R, h] | Same incarnation, R at or after the suffix's start, and the entries in (R, h] at most the checkpoint's bytes: send those entries. Otherwise send the newest checkpoint and the entries after it |
| Size bound | Per namespace on disk: about (1 + k) times its checkpoint, plus a checkpoint being prepared |
| Snapshot fallback | Exactly the selection's "otherwise": replaying more bytes than the namespace holds never wins |

Entries reach consumers projected: the changed keys of each applied transaction in today's delta encoding (`AUTHORITY_EXPORT.md:47-51`), without producer sessions, sequences or no-change operations. The projection is computed, never stored. A checkpoint reaches them as Replace: the namespace's live records at position c, canonically encoded, so every voter and relay produces the same bytes. A position above head gets no answer until the source catches up, so no namespace goes backwards (D1).

Applying entries after any R is safe because no operation is relative. An operation is a key plus a full definition, or `null` for a delete (`docs/AUTHORITY_ARCHITECTURE.md:79`, `service.cc:750-760`); preconditions are checked once at apply (`state.cc:943-948`) and the outcome is what the journal replays. The in-memory `ChangeHistory` goes: the journal's tail serves subscribers near head.

### D7. Relay: a per-namespace mirror of checkpoint and suffix

A relay keeps, per namespace, the newest checkpoint and the retained suffix in its own journal store (the same library and rule as D6), and a prepared generation in the agent's layout (D8) for agents that bootstrap. A sealed range of entries and a checkpoint never change once named by (namespace, incarnation, first, last), so relays and any HTTP cache that authorizes per namespace serve them as immutable objects. A fleet started from one image asks for one range and shares one answer. Regional relays may join a group as learners (`consensus/include/consensus/membership.h:118-130`); edge relays pull ranges from them. Bundles become upstreams, credentials grant namespace patterns instead of bundle indices (`docs/RELAY.md:47-60`), and each namespace is served once it is ready instead of after every bundle (`RELAY.md:78`).

### D8. The agent store

```text
generation_dir/
  storage.meta      host identity, 64 B; absent in a seeded image
  selected          namespace table, replaced atomically
  change            8-byte change word, incremented after each table replace, never replaced
  owner.lock
  ns/<incarnation>/ generation-<id>.{image,read,lease}, segment-<id>.{pack,owner}
```

The table replaces the 176-byte descriptor (`include/detent_local/generation.hh:23-31`): a header (magic, version, sequence, count, directory cursor, profile), then entries sorted by name, each with name, cursor, generation id, root length, and the SHA-256 of the root and of the `.read` record. One read gives a reader every namespace. Namespaces share no segments. A publication cycle collects every namespace generation ready in that cycle, syncs them as one group (D11), replaces the table once and increments the word once. The paged format stays: it reads in 268.5 ns at 5M (reader.md) and shares unchanged pages between generations.

Identifiers become content-derived, so two builds of the same input agree. The generation id is SHA-256 of a domain, incarnation, position and base generation id (zero for a Replace build), truncated to 16 bytes; today it is random (`src/detent_delivery/coordinator.cc:211`). The segment id keeps its derivation (`src/detent_local/paged_segment_builder.cc:38-47`) minus the storage identity. That identity, random per host (`disk_store.cc:32`), leaves `.read`, `.owner` and `.lease` (`src/detent_local/reader_metadata.hh:8`, `include/detent_local/ownership.hh:10-17`) and stays in `storage.meta`.

### D9. Seeding from an image

`detent-image build --source managed.json [--namespaces a,b] --out DIR` asks for Replace per namespace and writes the namespace directories and a table with sequence 1, without `storage.meta`. `--frames FILE` builds from saved Replace frames with no network. It runs the agent's own Replace build, so its namespace directories are byte-identical to an agent's from the same Replace (T6). It shares its writer with the reader's test helper (reader.md D9).

An agent that finds a valid table and no `storage.meta` adopts the store: it serves at once, runs the adoption barrier (D11) in the background, writes `storage.meta` last, and asks for (R, h] from each seeded cursor. Its first publication waits for adoption. Seeded files must be owned by the agent's account, with one link and no group or other write (`disk_store.cc:28-31`), so the image build sets ownership.

### D10. Restart: trust prepared generations

A generation is verified once, by the build that writes it. Its `.read` is written after that verification, and the table binds the `.read` digest. Restart reads `storage.meta`, the table, and per namespace the 320-byte root and 192-byte `.read`, each checked against the table's digests: O(namespaces), no pack read, no page hash. Packs open on first use. Corruption is still caught:

1. **First touch.** A page's SHA-256 is checked against its parent reference before use, by readers (reader.md D3; this design needs reader Q3 answered (a)) and by the agent when an update reads a base page.
2. **Scrub.** A background pass hashes each pack against its owner record's checksum at `scrub_bytes_per_second` (32 MiB/s by default, about 45 s for 1.51 GB), after start and every 24 h. `.read` gains a manifest digest over (segment id, length, checksum), so the scrub also proves no owner record is missing.
3. **Repair.** A mismatch fails that namespace alone: readers get `DETENT_SOURCE_FAILURE` for it, and the agent asks for Replace and publishes a fresh generation.

### D11. One barrier instead of a flush per file

**Invariant:** every file the durable table reaches is durable before the table entry naming it can be. Publication keeps it with the grouped order of `docs/STORAGE_BARRIER_ARCHITECTURE.md:17-26`: owner records, barrier; payloads, barrier; table replace, barrier. Each group syncs the way `IO::sync_files` does (`completion/io.h:225`, `posix.h:186-224`): fsync every file, then one full flush per device. On macOS fsync leaves data in the drive's cache and `F_FULLFSYNC` flushes the whole cache, so N fsyncs and one flush give the durability of N full flushes.

With the invariant, restart syncs no file. It issues one barrier, a sync and flush of the store directory, before garbage collection or a new publication, so a table replaced just before a crash is durable before anything it superseded is deleted. That barrier is the census's lasting job (`disk_store.cc:392`); the per-file syncs (`:133`, `:171`) go. Adopting a seeded store syncs every file once, as one group, off the serving path.

**Library change.** Completion's asynchronous driver syncs one file per primitive (`completion/operation.h:39`); the batched path exists only on the synchronous `IO`. Add `SyncFiles` and `OrderFiles` primitives that run `sync_files_and_directory` and `order_files_and_directory` (`io.h:231,240`). This is the device amortization the storage-barrier blueprint defers (`STORAGE_BARRIER_ARCHITECTURE.md:38`), and strata's commits, which go through completion's `IO` (`strata/docs/ARCHITECTURE.md`, "Libraries"), can use it too. Linux and macOS costs are unmeasured; T1 and T2 measure them.

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
| Relays shipping segments for (R, h] | No for deltas: physical segment files differ between replicas. Yes for its intended use, installing a far-behind consumer from one pinned manifest; D6's checkpoint is that path | `REPLICATION.md:86-99` |
| Agent store as one merged strata segment per namespace | Not now. A point read is a fuse filter, a block-tree term dictionary and a stored block (LZ4, zstd or raw), unmeasured against the 268.5 ns target; reads go through an index or view in one process (`get` returns `std::optional<std::string>`); the agent needs read-only attach from other processes without allocation, and page sharing between generations for publication proportional to the change | `format.hh:102-114`; `index.hh:1197-1209`; `docs/design/shared-reader.md` |
| Maturity | Phase 3; the README says not for production storage until the key-value rows pass their gate; `strata::group` is designed, and the tree holds only `strata/core` | `docs/STATUS.md`; `README.md:112-116` |

Reconciled with the Xapiand lead's directions: a namespace is a shard group (DISTRIBUTION Track D4), the journal is the history and a checkpoint brings a far-behind consumer up to date (REPLICATION), and that shape fits D1 exactly on the libraries Detent already runs. Positions live in the journal; no per-segment sequence range is proposed. The cold tier's fetch-on-open hook (`docs/design/cold-tier.md` D8) suits relays only if checkpoints become strata segment packs, which waits on Q5.

**Recommendation:** adopt the replication shape now, keep Detent's state machine and paged agent store, and do not use the strata core or segments in delivery. These proposals go to strata's owner and the Xapiand lead; nothing is built before they agree:

| # | Proposal (library) | Smallest general mechanism | Would Xapiand use it |
| --- | --- | --- | --- |
| P1 | Suffix retention relative to the checkpoint (consensus, journal) | A `RetainedStatePolicy` bound of k times the newest checkpoint's bytes | Yes: a replica back from an outage replays instead of reinstalling, whenever replay is cheaper |
| P2 | Range reads and checkpoint export for non-members (consensus, journal) | Read the retained suffix by position range, and stream a checkpoint to a consumer that is not a voter or learner | Yes: read replicas in other regions, change capture, cold-tier consumers |
| P3 | A compacted range (journal) | Last write per key over a sealed range of the suffix, merged in tiers: the owner's compacted blocks | Unclear. Only if Xapiand's keyed documents want compacted catch-up; otherwise it is Detent-only and stays out (Q2) |

Not proposed, because only Detent would use them: per-segment revision ranges, tombstone segments, byte-identical segments across replicas, and a cross-process mapped reader in the strata core.

## Reader alignment

| Reader assumption (reader.md D2) | Here |
| --- | --- |
| A1: a selected descriptor per namespace | Changes: one table entry per namespace (D8), carrying what the descriptor did, so attaching "all" reads one file instead of one per namespace |
| A2: one record lists the namespaces | Holds: the table. "All" means its entries, which the agent already filtered by credential |
| A3: one store-wide change word | Holds: `change` (D8); this design owns the file (reader Q4) |
| A4: revisions never go backwards | Holds per incarnation; a new incarnation is a new namespace to the reader |

The reader should attach a namespace on its first lookup, not at open, to keep its first read under 1 ms with thousands of namespaces.

## Gate rows

Targets come from the attribution and are fixed after the first screen. The lead measures on the reference host and on Linux, under `quiet exclusive`. 10K, 100K and 5M use preserved stores; the first T2 run at 1M preserves one.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Restart | At most 0.3 s at each of 10K, 100K, 1M, 5M; 5M at most 1.5 times 10K | The attribution's restart method: an APFS clone of a prepared store per run, wall from exec to "managed local store ready", six runs per scale, then the 40 probes of `benchmarks/prepared-pack-access-5m-20261025.json` with zero failures |
| T2 Bootstrap | At most half the recorded wall at each scale (1.36, 7.9, 73, 361 s); syncs at most 5% of wall; bytes hashed per tier at most 1.2 times bytes built | The attribution's bootstrap harness (`tests/detent_system`, adapted from `capacity-trials/restore_5m.py`) with `sample` attached; sync and hash counters from S2 and S4 |
| T3 Seeded start | Upstream blocked: first correct read within T1's bound. Upstream open: at head within 2 s, entries only (no checkpoint), wire bytes at most twice the projected changes. 5M at most 1.5 times 10K | Build an image with `detent-image` at R; apply 1,000 changes (100 deletes) over 10 namespaces; restart every voter and the relay; start the agent from the image |
| T4 Retention | Entries for every namespace whose changes since R are under its checkpoint's bytes; a checkpoint only for the namespace pushed past it; the same answers after voter, leader and relay restarts; per-namespace journal bytes at most (1 + k) times the checkpoint plus 10% | T3's setup at each scale, plus enough writes to namespace A to pass its checkpoint's bytes and not B's |
| T5 Corruption | 100 single-byte flips in random pages: restart is ready; the first touch returns `DETENT_SOURCE_FAILURE` for that namespace only, never a wrong value; the scrub reports untouched flips within one pass; reads recover after repair. A corrupt table or `.read` refuses the store or the namespace | A test over a 100K store written by the reader's test helper |
| T6 Identity | Every file under each namespace directory identical between `detent-image` and an agent's Replace build, and across two `detent-image` runs; Replace bytes identical from every voter and relay at one position | Per-file SHA-256 at each scale |
| T7 Durability | After an interruption at every primitive of publication, adoption and the startup barrier, then simulated power loss, every file the durable table reaches exists and verifies | The blueprint's crash-cut matrix (`STORAGE_BARRIER_ARCHITECTURE.md:48`), with completion's sync seam (`posix.h:496`) recording what reached a flush and discarding the rest |
| T8 Groups | 4,096 namespace groups on three voters: idle CPU at most 2% of a core per voter; 2,000 transactions/s spread over them with p99 commit at most 50 ms (estimates; `docs/SCALE_TARGETS.md:117`'s proposed baseline) | The authority capacity harness with 4,096 namespaces of the 5M corpus |

## Format and wire changes

| # | Change | Today |
| --- | --- | --- |
| W1 | `Cursor` is {cluster, incarnation, position} | `cursor.hh:8-13` |
| W2 | Scope identity removed | `scope.hh`; `AUTHORITY_EXPORT.md:5-9` |
| W3 | Authority: a group per namespace and a directory group; a canonical per-namespace checkpoint; producers see `namespace_revision` as the group position | `service.cc:1394`; `state.hh:46-64`; `service.cc:156` |
| W4 | Session open and next carry `[{name, cursor}]`, or `all` with a directory cursor | `scope`, `namespaces`, `base` (`AUTHORITY_EXPORT.md:63`) |
| W5 | Per-namespace frames: an entry range or a checkpoint, each named by (namespace, incarnation, first, last) and fetchable as an immutable object. `X-Detent-Kind` and `X-Detent-Revision` go | `AUTHORITY_EXPORT.md:71` |
| W6 | Relay configuration: upstreams and one store; grants by namespace pattern | `RELAY.md:47-60` |
| W7 | Agent and relay store layout: table, `change`, `ns/<incarnation>/` | `generation.hh:23-31`; `docs/ACTORS.md:104-113` |
| W8 | A paged root holds one namespace and the new cursor | `paged_layout.hh:30-37` |
| W9 | Content-derived generation and segment ids; no storage identity in `.read`, `.owner`, `.lease` | D8's citations |
| W10 | `.read` gains the manifest digest | `reader_metadata.hh:7-13` |
| W11 | Grants accept patterns (Q1); `namespaces` becomes optional in `managed.json` | `service.cc:438`; `src/detent_delivery/configuration.cc:156` |
| W12 | Completion `SyncFiles` and `OrderFiles` primitives (library API) | `operation.h:39` |

## Alternatives considered

**The owner's compacted blocks** (`[o1-o10]`, `[o11-o50]`, ..., last write per key, tiered merges, immutable). They send less than D6 when a hot key churns, and cache as well. Kept as P3: under one replication mechanism they belong in the journal as a general capability, or not at all.

**Changes computed from state plus retained tombstones.** Exact and bounded by the namespace, but a second history mechanism beside the journal, and answers keyed by each base cache poorly. Not selected.

**Keep one group and the global revision.** A namespace's position moves with unrelated writes, an image cursor cannot serve applications reading different namespace sets, and a far-behind consumer of a small namespace scans a busy shared journal. D1 rules it out.

**Strata segments for history, relay caching or the agent store.** See "Strata fit".

**A `selected` file per namespace.** A reader following all namespaces would open one file per namespace on its first read. Not selected.

**Verify at restart on every core, or check a manifest at every start.** The first is still O(N) per restart; the second reads every owner record and still trusts the pack bytes. The manifest feeds the scrub instead.

## Limits

1. **Silent:** a corrupted page nobody touches stays undetected until the scrub reaches it, up to 24 h plus one pass.
2. **Silent unless watched:** a namespace that changed more than its checkpoint's bytes since an image's position gets a checkpoint. Correct and bounded by the namespace, but more than compaction (P3) would send; health counts checkpoints by reason.
3. A namespace per group multiplies groups: 4,096 per voter at today's namespace limit (`state.hh:26`). T8 gates it; strata's own target is at least 1,000 groups per node (`DISTRIBUTION.md:16`).
4. Byte identity holds for Replace builds only; generations built from entries differ by design.
5. D11's macOS argument rests on `F_FULLFSYNC` flushing the whole drive cache, as Apple documents and completion already assumes (`posix.h:181-185`).

Documents this changes once built: `docs/ACTORS.md:54-91,102-122`, `docs/RELAY.md:13-80`, `docs/AUTHORITY_EXPORT.md:5-9,29-45,63-71`, `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md:23,29`, `docs/STORAGE_BARRIER_ARCHITECTURE.md:38`, `docs/SCALE_TARGETS.md:72,118`, and reader.md's A1.

## Open questions for the owner

**Q1. What can a grant name?** (a) Exact names: "all" means every granted name, and a new namespace needs a grant change. (b) Exact names, prefixes (`payments.*`) and `*`. *Recommendation: (b)*, which makes D2's "created later" reachable without editing credentials.

**Q2. How much history?** (a) k = 1: keep the suffix until replaying it would cost more than the checkpoint (D6). (b) A time window, such as 90 days, whatever it costs. (c) (a) plus the owner's compacted blocks, as P3. *Recommendation: (a)*: storage stays about twice each namespace, and an image of any age gets entries whenever they are cheaper. Choose (c) if Xapiand takes P3.

**Q3. Does a seeded image get the trust of a store the agent prepared?** (a) Yes: pages checked on first touch, and the scrub (D10). (b) Verify it fully before serving on first start, 52 s at 5M today. *Recommendation: (a)*: the image sits in the same layer as the application binary that reads it, and (b) defeats the scenario.

**Q4. Remove one-shot delivery?** That is `POST /v2/distribution` and the agent's default `delivery_mode: poll` (`ACTORS.md:99`). (a) Remove it; sessions only. (b) Keep both, on the per-namespace body. *Recommendation: (a)*, one delivery path. It removes a user-visible mode, so it is the owner's call.

**Q5. The authority's shape, with the Xapiand lead.** (a) A consensus group per namespace now, on Detent's state machine, with strata's core later behind the same contract. (b) Keep one group and filter its journal per namespace. (c) Wait for `strata::group` and the strata core's key-value gate. *Recommendation: (a)*: it is strata's replication shape on libraries Detent already runs, and it leaves the state-machine choice open.

## First steps

Each step lands alone with CI green. S1 to S4 attack the measured 120 s restart and 722.6 s bootstrap on today's single-generation store, and no open question blocks them.

| Step | Change | Needs |
| --- | --- | --- |
| S1 | Completion: `SyncFiles` and `OrderFiles` primitives with seam tests; lands upstream, then the pin moves | Nothing |
| S2 | D11 on today's store: grouped publication syncs, a census without per-file syncs, sync counters | S1 |
| S3 | D10 on today's store: restart without the six hash passes, first-touch checks on the agent's base reads, the scrub | Nothing |
| S4 | D12.2: no re-validation of a tier's own build; hash counters | Nothing |
| S5 | Authority: a group per namespace, the directory group, canonical checkpoints, P1 retention | Q5, Q2, P1 agreed |
| S6 | W1-W5 and W11: cursor, sessions, frames, directory | S5, Q1, Q4, P2 agreed |
| S7 | D8's store, per-namespace publication, the `change` word (reader S5) | S6 |
| S8 | D7's relay mirror and immutable objects | S6, S7 |
| S9 | D9: `detent-image`, adoption, T6 | S7, Q3 |

What I want from preliminary review:

1. D6: whether journal entries plus checkpoints give exact (R, h] in every case, including a restore and a voter replacement.
2. D3 and T8: whether 4,096 groups per voter holds.
3. D11: the invariant and the single startup barrier.
4. D10: whether first-touch checks and the scrub replace startup verification with no silent gap beyond limit 1.
5. The strata fit: whether P1-P3 are general enough for strata's owner and the Xapiand lead.
