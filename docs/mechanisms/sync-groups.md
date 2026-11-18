> Status: CURRENT (2026-11-09) How a publication reaches stable storage in sync groups, what startup syncs, and how the sync counters count. Workstream: namespace-s1-s2, namespace-s7. Design: [namespace-delivery.md](../design/namespace-delivery.md) D11.

# Sync groups: one barrier per group instead of a flush per file

A generation reaches `selected` only after every file it names is durable. A publication orders that with two sync groups, the table's own sync and a directory barrier; a restart syncs no file, only the directory, in its one barrier. Code: `src/detent_local/agent_store.cc`.

```text
publication (AgentStore::publish, for the agent and the relay through DiskStore)

  stage     every file under a .tmp- name, written unsynced, linked to its derived name
  group A   owner records, generation .lease          SyncFiles + directory
  group B   packs, .image, .read, .live               SyncFiles + directory
  table     .tmp- table written, fsynced alone, renamed over selected; then a directory sync
  then      change increments                         no sync (startup bumps every word)

restart (the AgentStore constructor)
  lock      owner.lock; change opened or created
  barrier   directory sync + device flush             -> .tmp- names removed, table and entries checked
  then      change bump_all, then ready
```

## The order, and why each step is there

- **Owners before payloads.** An owner record lets recovery identify an unfinished pack or generation, so its entry is durable before the payload it names. Group A holds every new segment owner record and the generation's `.lease`; its sync also syncs the directory, so group B starts only after A's entries survive.
- **Payloads before the table.** Group B holds the packs, the image, `.read` and `.live`. A name the durable table already reaches and whose bytes verify is reused and synced by neither group: the publication that wrote it synced it.
- **The table synced before its rename.** The new table is written under a `.tmp-` name and fsynced alone before it is renamed over `selected` (D11, N6). A table renamed with unsynced bytes would become durable through a later barrier, and a power cut would then revert a table already served. The mutant that renames first is killed by T7 (`tests/detent_agent/mutants/n6-rename-before-fsync.patch`).
- **Rename, then the directory barrier.** The directory sync after the rename makes it durable. Only then does `change` increment.
- **Startup syncs only the directory.** Every file a durable table reaches was synced by the publication that wrote it. The one barrier makes durable a `selected` rename a killed process left in the page cache before the store serves it; serving first would let a power cut revert a table a reader already used (ND-R9).

## Counts

A group is one `SyncFiles` request over its open files plus the directory (completion's `sync_files_and_directory`). Per platform it costs what completion documents (`completion:docs/mechanisms/sync-files.md`):

| Platform | One group of n files |
| --- | --- |
| macOS | n `fsync`, one directory `fsync`, one `F_FULLFSYNC`: one device flush |
| Linux, FreeBSD | n `fsync`, one directory `fsync`: n + 1 flushes |

A publication on macOS therefore issues four device flushes whatever its size: group A, group B, the table and the directory barrier. A restart issues one (`test_local_paged_store_startup` checks `{files 0, groups 0, directory_syncs 1, device_flushes 1}`).

## Failure

A failed sync, write or link poisons the agent store: the table is as it was, and the store needs a restart, whose startup removes the `.tmp-` names and checks every entry. `DiskStore` fences on such a failure. A `ReachedNameMismatch` or `LeaseHeld` leaves the table as it was without poisoning.

## Counters

`DiskStore::sync_stats()` returns `SyncStats {total, startup, publication}` of `SyncCounters {files, directory_syncs, device_flushes, groups}`, from `AgentStore::sync_counters()`:

- A group of n files is one group, n files and one directory sync, with one flush on macOS or n + 1 elsewhere.
- The table's fsync is one file and one flush.
- A directory sync is one directory sync and one flush.

`total` keeps its final value after the store closes; `startup` is the counters when the store opened; `publication` is the last published publication's delta. The agent logs the publication counters when they change ("publication synced N files in N groups, ..."); the relay reports them in its status JSON (`sync_*`, `publication_*`).

## Power-cut model

`test_agent_store_crash` (T7) replaces completion's sync calls with a seam that applies each `fsync`, directory `fsync` and device flush to a model of the disk, and cuts after every primitive of startup, publication and collection. It materializes each cut in four views (durable or all entries; unsynced data truncated or zeroed) and a kill view, and requires the restarted store to serve a table that was published, with a sequence at least the one served, whose every reached file verifies byte for byte.
