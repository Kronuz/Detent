# Namespace S7: the agent store

S7 of [namespace delivery](../design/namespace-delivery.md) (D8, D10, D11, W9, W11, T7). The agent store is one directory per agent: the namespace table `selected`, `change` (reader.md D4), `owner.lock`, and every namespace's generation and segment files under names derived from what they hold. It is the `agent-store` contract S8's relay mirror and S9's image consume. Code: `include/detent_local/agent_store.hh`, `src/detent_local/agent_store.cc`; tests in `tests/detent_agent/`.

## Layout

```text
store/                       one directory; the only one the startup barrier syncs
  selected                   the table, replaced by rename
  change                     store word, then one sequence per namespace slot; never replaced
  owner.lock                 held exclusively (flock) by the one writer
  generation-<id>.image      the 320-byte paged root, or a flat image
  generation-<id>.lease      empty; a reader holds it shared while it reads the generation
  generation-<id>.read       reader metadata (empty for a flat image)
  generation-<id>.live       segment rows, live counts, histogram; binds the whole .read and the root
  segment-<id>.pack/.owner   pages and the owner record
  .tmp-<random>              a file being written; startup removes it
```

## The table

`selected` is one file: a 64-byte header (magic `DTTABLE1`, layout 1, entry count, sequence, the directory cluster and position, the profile), then the entries sorted by name, then the SHA-256 of everything before it. An entry holds the name (1 to 255 bytes), the cursor (cluster, incarnation, position), the `change` slot, the generation id, the build ordinal, the root length and the SHA-256 of `.image` and of `.read`: 137 bytes plus the name. Decoding refuses anything but a whole current table (wrong magic, layout, digest, padding, order, a duplicate slot, trailing bytes) with one error, `TableRefused` (D43).

A slot is assigned when a name first appears, in first-appearance order within a cycle, and never reused. After each replacement of `selected`, `change` increments each changed entry's sequence, then the store word (`ChangeWriter::publish`).

## Derived ids

The generation id is the first 16 bytes of SHA-256 over `detent.agent-store.generation`, the layout, the profile (semantic id and capabilities), the incarnation, the position, the base generation (zero for a build from a full copy) and the build ordinal. A segment id is the first 16 bytes of SHA-256 over `detent.agent-store.segment`, the generation id and the segment ordinal. No storage identity enters either, so two builds of the same input on any host name the same files; `test_agent_store` pins one id so a derivation change fails a test. A catch-up and a full copy at the same position derive different ids (their bases differ); both name the same state.

## Publication

`AgentStore::publish(directory, generations)` runs one cycle:

1. Validation. The store derives each generation's id, checks that its `.live` binds that id, the root's and `.read`'s digests, that every new pack matches its row, and that every row it does not write is a segment the durable table reaches. Nothing is written for a cycle that fails.
2. Staging. Every file is written under a fresh `.tmp-` name, unsynced.
3. Linking. Each `.tmp-` name is linked to its final name with `linkat`, which fails on an existing name. An existing name the durable table reaches is reused in place when its bytes hash to what the cycle holds; when they differ it is corruption: `ReachedNameMismatch` names it and its namespace, the table stays, and the caller builds a repair at the next ordinal (D10). Any other existing name is an orphan, unlinked and linked again (N1), even with the right bytes: a killed process may have linked it unsynced, or its fsync may have failed (EIO) while the page cache keeps the bytes and a later fsync reports success. Then the `.tmp-` name is unlinked.
4. Groups (D11). The new owner records and `.lease` files sync as one group, with the directory; the new payloads (packs, roots, `.read`, `.live`) as a second. A reused name needs neither: the durable table reaches it, so its bytes were synced before that table was renamed.
5. The table, written under a `.tmp-` name, fsynced, renamed over `selected`, then the directory synced. The fsync comes before the rename (N6).
6. `change`: the changed slots, then the store word.

An I/O error after staging leaves the store needing a restart; `ReachedNameMismatch` does not.

## Startup

`AgentStore(io, options)` takes `owner.lock`, opens or creates `change` (extending one shorter than a word, which a cut between its creation and its sizing leaves), issues one barrier (a sync of the directory and a device flush), removes every `.tmp-` name, reads `selected`, checks each entry's `.image` (length and digest), `.read` (digest) and `.live` (its digest, generation, root and `.read` bindings, and that its packs and owner records exist with the row's length), then bumps every `change` word once and is ready. No pack is read and no file is synced. A table that does not decode refuses the store; an entry that does not verify fails its namespace alone (`failed()`), and its `.live` segments stay protected from the orphan rule.

## Catch-up input

An answer may carry a tombstone for a key the store never held (created and deleted after R). The paged overlay applies a delete whose key is absent as a no-op, so the agent needs no new code for it; `test_agent_catch_up` proves it on the real builders.

## Evidence

| Criterion | Test | What it shows |
| --- | --- | --- |
| A1 (W9) | `test_agent_store` | A new store holds `owner.lock` and `change`; after a cycle the directory holds exactly `selected`, `change`, `owner.lock` and the derived files, each singly linked; the table reads back, slots follow first appearance, `change` moves per changed slot and once per word at restart; `.tmp-` names go at startup; `owner.lock` excludes a second writer; a torn table refuses the store; an empty or 3-byte `change` opens, with or without `selected`, and a ragged one refuses; the codec's refusals |
| A2 (W11) | `test_agent_store` | Ids derive from each input and from no storage identity, pinned; files are linked from `.tmp-` names; a reached name with the right bytes is reused in place (same inode), an unreached name is relinked whatever its bytes, and a pack whose fsync failed (EIO, through the sync seam) is relinked after the restart, never reused; a base segment the table does not reach, or a `.live` that does not bind, is refused with nothing written; a corrupt reached `.read` fails its namespace, refuses a republication of the same names and is repaired at ordinal 1 |
| A3 (T7) | `test_agent_store_crash` | See below |
| A4 | `test_agent_catch_up` | From a store at R, the ring entries, the block trimmed above R (with the absent-key tombstone) and the base each give the full copy's state commitment at h; a tombstone-only position moves only the namespace's position; a tombstone for a held key deletes it |

## The crash model (T7)

`test_agent_store_crash` models the disk from completion's sync seam (`PosixIO::replace_sync_calls`): an fsync stages a file's bytes and, for the directory, its entries; a device flush (macOS `F_FULLFSYNC`) makes what was staged durable; on Linux an fsync is durable. A hook after every primitive of startup and publication records the kernel's view (names, inodes, bytes). From each recorded event:

- A power cut keeps durable names only, or every name with durable bytes only (metadata ahead of data), cut short or zero-filled. The store must open, every file its table reaches must exist and verify (packs and owner records included), its table must be one the run published with a sequence at least the one it last served, no `.tmp-` name may remain, and it must publish the same input again.
- Restart then republish: the recovered store publishes again over the reused names, recorded, and is cut at every step of that.
- A process kill keeps every name and byte with only the earlier syncs durable; the restart publishes again, recorded, and is cut at every step. Startup syncs no file, so a table renamed before its fsync, or an unsynced name trusted as reused, surfaces here.
- `change` is in the model. It is never synced, so a cut keeps it empty or at its size; a kill between its creation and its sizing (before the first event of a run that creates it) keeps every name with `change` empty. Its words never decide a recovery, so the model keeps its length alone.

Runs: the first publication into a new store; the same, rebuilt with other bytes under the same derived names (a stale `.live` from the first build is unlinked and relinked, or, once the table reaches it, repaired at the next ordinal); a catch-up cycle that keeps a base segment and adds a namespace; and a repair of a namespace whose reached `.read` is corrupt. Identical cuts recover once (the outcome is cached by the files they leave).

Mutants (`tests/detent_agent/mutants/`, run by `agent_store_mutant.sh`): `n6-rename-before-fsync` (the table renamed before its fsync, T7's revert check), `ndr4-trust-reused-name` (a reached name reused without verifying its bytes), `f1-short-change-refused` (`ChangeWriter` refusing a `change` shorter than a word, killed by T7) and `f2-reuse-unreached` (an unreached name reused when its bytes verify, killed by `test_agent_store`).

## Derived names in the managed store

The managed store (`DiskStore`) names its generations and segments by the same derivation, from one place: `next_generation` in `include/detent_local/derived_ids.hh` (D8.1, D8.2). Each builder derives its own name (`ImageBuilder`, `PagedSnapshotPlanBuilder`, `PagedMaintenanceBuilder`, `PagedPlanBuilder`, `PagedDeltaPlanBuilder`, `PagedCompactionBuilder`); no caller passes an id. Until namespace-s6-cutover (D88) the old cursor maps to a namespace cursor: the incarnation is the first 16 bytes of SHA-256 over `detent.agent-store.incarnation`, the cluster, the group, the restore epoch and the scope, so a trusted reset that changes only the cluster derives new names; the position is the revision.

- Base. A snapshot, a repair, a trusted reset and a migration are full copies (zero base); a delta, progress and a compaction name the selected generation as their base.
- Ordinal. A build at the selected head's cursor takes the head's ordinal plus one, any other build 0. The table entry carries it.
- Check. `DiskStore::begin` refuses a publication whose generation and ordinal are not the derivation over the selected generation.
- Records. `.owner` (`DTSGOWN2`: segment, length, checksum) and `.read` (`DTREAD02`) hold no storage identity; `.lease` is empty.
- `PagedSemanticLimits` has no default bounds: callers pass the store's recovery limits or `paged_format_semantics`, the format's.

Tests: `test_derived_names` (rebuilds at one head take ordinals 1, 2, 3 under distinct names; a cluster-only reset derives a new name; mutants `tests/detent_local/mutants/s7-a2-ordinal-reused.patch`, `s7-f1-cluster-omitted.patch`), `test_ownership` and `test_reader_metadata` (the record layouts, pinned).

## Wiring

The agent publishes and serves through the agent store; nothing else writes its directory (D43: the old layout, `storage.meta`, `DTSELECT` descriptors, ownership records and their collector are gone, with no migration).

```text
coordinator (owner thread)          worker thread              disk
  DiskStore::begin  -> prepare cycle
  DiskStore::work() -> AgentWork ---> run(): AgentStore::publish / collect ---> store/
  DiskStore::advance() <- settle (census, selection, errors)
readers: table -> image peek -> flock(.lease, SH) -> same inode as the name -> map
```

- Publication. `DiskStore::begin` checks the build as before (derived name, cursor, intent, admission) and prepares one cycle: the generation as the store's one namespace, named after the configured scope (D88, until s6-cutover), every other entry dropped. A flat image publishes an empty `.read` and a `.live` without rows, charged to the store's account. The cycle views the builder's bytes; the store holds them until it settles. The cycle is reserved before a paged plan moves into the store, so a refusal (`DiskPressure`, `StoragePressure`) leaves the caller's plan whole. `work()` hands the cycle to the coordinator's worker; `advance()` on the owner settles it: the selection, the ledger against the store's census, and the paged recovery the caller confirms. `ReachedNameMismatch` and `LeaseHeld` reject the publication and leave the table; any other error fences the store.
- Profile. The table holds the store's profile. A publication adopts its own (`AgentStore::adopt`); the cycle that writes it drops every entry of the other.
- Startup. `make_disk_store` opens the agent store (`owner.lock`, the one barrier, the table checked), reads the profile from the table, and recovers the selected generation. A fresh store needs an empty directory; an existing one needs its `owner.lock`. A generation that does not recover fences the store and keeps every file.
- Readers. `read_selected` reads the table (exactly one entry), peeks the image for its cursor (the paged root's prefix or the flat header), and requires the table's profile and the namespace cursor the cursor maps to. A reader opens the empty `.lease`, locks it shared, and maps the image only when the locked inode is still the name's.
- Collection. After each publication settles (and its paged recovery is confirmed), the next work is `AgentStore::collect()`: every generation the table does not reach goes, `.lease` first under an exclusive non-blocking lock, except one a reader holds and the segments its `.live` names. Collection syncs nothing; a cut keeps any subset of its removals, all of names no table reaches.
- Revisited cursor. A cycle that derives a name the table no longer reaches but a reader still holds throws `LeaseHeld` before writing anything; the publication is retried.
- Accounting. The ledger's `used` is the store's census (every name, one generation per `.lease`); a reservation covers the cycle's peak (its files, the `.lease` and the staged table) until it settles against the census the cycle left.
- Change file. `change` holds the store word from the store's creation; a namespace's slot appears with its first publication, so a fresh store's file is one word.
- Catch-up. The ring entries, a trimmed block and a base all build on the recovered selected generation; from a store recovered after a process kill at every agent-store primitive of a publication they reach the full copy's commitment (`test_agent_catch_up`).

Tests: `test_disk_store` (startup modes, census settlement, cancellation, admission, collection around a reader's lease, a close while work runs), `test_paged_publication_intents` (delta, progress, maintenance and compaction published and collected), `test_paged_disk_store`, `test_paged_store_startup` (one barrier; damage fences), `test_foreign_generation` (another format's generation fences and keeps its files), `test_paged_root_partial_reclaim` (cut leftovers collected around a held lease), `test_managed_reader` (the reader's table, lease and image binding), `test_agent_store` (`collection`, `held_lease`), `test_agent_store_crash` (T7, each cycle followed by a collection, cut at every removal).

## Open

- The non-local filesystem warning for `owner.lock` (s7 F5) is not done.
