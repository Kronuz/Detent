# Namespace S7: the agent store

S7 of [namespace delivery](../design/namespace-delivery.md) (D8, D10, D11, W9, W11, T7). The agent store is one directory per agent: the namespace table `selected`, `change` (reader.md D4), `owner.lock`, and every namespace's generation and segment files under names derived from what they hold. It is the `agent-store` contract S8's relay mirror and S9's image consume. Code: `include/detent_local/agent_store.hh`, `src/detent_local/agent_store.cc`; tests in `tests/detent_agent/`.

## Layout

```text
store/                       one directory; the only one the startup barrier syncs
  selected                   the table, replaced by rename
  change                     store word, then one sequence per namespace slot; never replaced
  owner.lock                 held exclusively (flock) by the one writer
  generation-<id>.image      the 320-byte paged root
  generation-<id>.read       reader metadata
  generation-<id>.live       segment rows, live counts, histogram; binds .read and the root
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
3. Linking. Each `.tmp-` name is linked to its final name with `linkat`, which fails on an existing name. An existing name whose bytes hash to what the cycle holds is reused in place. One whose bytes differ is an orphan when the durable table does not reach it: unlinked, then linked again (N1). One the table reaches is corruption: `ReachedNameMismatch` names it and its namespace, the table stays, and the caller builds a repair at the next ordinal (D10). Then the `.tmp-` name is unlinked.
4. Groups (D11). The new owner records and every reused name sync as one group, with the directory; the new payloads (packs, roots, `.read`, `.live`) as a second. Reused names join a group because a name a killed process linked may hold bytes the page cache has and the disk does not.
5. The table, written under a `.tmp-` name, fsynced, renamed over `selected`, then the directory synced. The fsync comes before the rename (N6).
6. `change`: the changed slots, then the store word.

An I/O error after staging leaves the store needing a restart; `ReachedNameMismatch` does not.

## Startup

`AgentStore(io, options)` takes `owner.lock`, opens or creates `change`, issues one barrier (a sync of the directory and a device flush), removes every `.tmp-` name, reads `selected`, checks each entry's `.image` (length and digest), `.read` (digest) and `.live` (its digest, generation, root and `.read` bindings, and that its packs and owner records exist with the row's length), then bumps every `change` word once and is ready. No pack is read and no file is synced. A table that does not decode refuses the store; an entry that does not verify fails its namespace alone (`failed()`), and its `.live` segments stay protected from the orphan rule.

## Catch-up input

An answer may carry a tombstone for a key the store never held (created and deleted after R). The paged overlay applies a delete whose key is absent as a no-op, so the agent needs no new code for it; `test_agent_catch_up` proves it on the real builders.

## Evidence

| Criterion | Test | What it shows |
| --- | --- | --- |
| A1 (W9) | `test_agent_store` | A new store holds `owner.lock` and `change`; after a cycle the directory holds exactly `selected`, `change`, `owner.lock` and the derived files, each singly linked; the table reads back, slots follow first appearance, `change` moves per changed slot and once per word at restart; `.tmp-` names go at startup; `owner.lock` excludes a second writer; a torn table refuses the store; the codec's refusals |
| A2 (W11) | `test_agent_store` | Ids derive from each input and from no storage identity, pinned; files are linked from `.tmp-` names; a name with the right bytes is reused in place (same inode), an orphan with wrong or short bytes is relinked; a base segment the table does not reach, or a `.live` that does not bind, is refused with nothing written; a corrupt reached `.read` fails its namespace, refuses a republication of the same names and is repaired at ordinal 1 |
| A3 (T7) | `test_agent_store_crash` | See below |
| A4 | `test_agent_catch_up` | From a store at R, the ring entries, the block trimmed above R (with the absent-key tombstone) and the base each give the full copy's state commitment at h; a tombstone-only position moves only the namespace's position; a tombstone for a held key deletes it |

## The crash model (T7)

`test_agent_store_crash` models the disk from completion's sync seam (`PosixIO::replace_sync_calls`): an fsync stages a file's bytes and, for the directory, its entries; a device flush (macOS `F_FULLFSYNC`) makes what was staged durable; on Linux an fsync is durable. A hook after every primitive of startup and publication records the kernel's view (names, inodes, bytes). From each recorded event:

- A power cut keeps durable names only, or every name with durable bytes only (metadata ahead of data), cut short or zero-filled. The store must open, every file its table reaches must exist and verify (packs and owner records included), its table must be one the run published with a sequence at least the one it last served, no `.tmp-` name may remain, and it must publish the same input again.
- Restart then republish: the recovered store publishes again over the reused names, recorded, and is cut at every step of that.
- A process kill keeps every name and byte with only the earlier syncs durable; the restart publishes again, recorded, and is cut at every step. Startup syncs no file, so a table renamed before its fsync, or a reused name never synced, surfaces here.

Runs: the first publication into a new store; the same, rebuilt with other bytes under the same derived names (a stale `.live` from the first build is unlinked and relinked, or, once the table reaches it, repaired at the next ordinal); a catch-up cycle that keeps a base segment and adds a namespace; and a repair of a namespace whose reached `.read` is corrupt. Identical cuts recover once (the outcome is cached by the files they leave).

Mutants (`tests/detent_agent/mutants/`, run by `agent_store_mutant.sh`): `n6-rename-before-fsync` (the table renamed before its fsync, T7's revert check), `ndr4-trust-reused-name` (a reused name kept without verifying its bytes) and `ndr4-unsynced-reuse` (a verified reused name left out of the sync groups).

## Open

- The store lands as a module (`detent::agent_store`, built in `src/detent_local/CMakeLists.txt`); namespace-s7-wire wires the agent's publication and readers through it.
- `.lease` files (created at adoption, S9) and the collector of unreached names are not part of S7.
