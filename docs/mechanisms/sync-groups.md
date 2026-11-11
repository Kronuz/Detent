> Status: CURRENT (2026-11-09) How a publication reaches stable storage in sync groups, what startup syncs, and how the sync counters count. Workstream: namespace-s1-s2. Design: [namespace-delivery.md](../design/namespace-delivery.md) D11.

# Sync groups: one barrier per window instead of a flush per file

A generation reaches `selected` only after every file it names is durable. A publication orders that with two sync groups and the table's own barrier; a restart syncs one file, `storage.meta`, in its one barrier.

```text
publication (agent and relay: one PublicationOperation in DiskStore)

  group A   segment owners, generation lease       create+write each (unsynced), SyncFiles per window, close
  group B   packs, image, live, read, descriptor   the same
  Replace   descriptor -> selected                 rename; then DirectorySync (barrier C)
  then      change increments                      no sync (startup bumps every word)

restart (StartupOperation)
  census   stat every entry, read storage.meta / selected / owner records read-only (no sync)
  remove   selected, only when its ownership names a layout that is not current
  barrier  SyncFiles {storage.meta} + directory    -> then change bump_all, then ready()
```

## The order, and why each step is there

- **Owners before payloads.** An owner record lets recovery identify an unfinished pack or generation, so its entry must be durable before the payload it names exists. Group A holds every segment owner and the generation lease. Its last `SyncFiles` also syncs the directory, so group B starts only after A's entries survive.
- **Payloads before the table.** Group B holds the packs, the image, `.live`, `.read` and the descriptor (the 176-byte table). The descriptor is fsynced inside the group, before `Replace` renames it over `selected` (D11, N6). A table renamed with unsynced bytes would become durable through the startup barrier, and a power cut would then revert a table already served.
- **Rename, then the directory barrier.** `Replace` is a rename; the following `DirectorySync` makes it durable. Only then does `change` increment.
- **Startup syncs only what it trusts unsynced.** Every file a durable table reaches was synced by the publication that wrote it, so the census only stats, and it reads the records it must verify through a read-only open (`MutationRequest::read_only`). `storage.meta` is the exception: store creation names it before its own `Sync`, so a process killed in between leaves a record that a restart decodes and embeds in later leases and owner records. The barrier is therefore one `SyncFiles` over `storage.meta` (reopened read-only) plus the directory. The directory half also makes durable a `selected` rename that a killed process left in the page cache, before `ready()`; serving first would let a power cut revert a table a reader already used (ND-R9).

## Windows

`SyncFiles` takes open descriptors, so a group is written in windows of at most `DiskStoreOptions::sync_group_files` files (default 128). Each file is created and written without a sync (`PreparationFilePolicy::Stage`, completion `image.h`). The window is one `SyncFiles` request over its open files, then each file is closed. The descriptor count is the bound, not the file count. Per platform a window costs what completion documents (`completion:docs/mechanisms/sync-files.md`):

| Platform | One window of n files |
| --- | --- |
| macOS | n `fsync`, one directory `fsync`, one `F_FULLFSYNC`: one device flush |
| Linux, FreeBSD | n `fsync` (Linux: a `sync_file_range` writeback hint on each first), one directory `fsync`: n + 1 flushes on ext4 and XFS |

## Flush counts

On macOS a publication issues one device flush per window plus one for barrier C, and a restart issues one. On Linux and FreeBSD the counters charge one flush per `fsync`, as completion's contract states.

| Shape | Files | Windows (groups) | Directory syncs | Device flushes, macOS | Source |
| --- | --- | --- | --- | --- | --- |
| T7 publication, window 1 | 7 | 7 | 8 | 8 | `test_local_publication_power_cut`, counters checked against the sync seam |
| T7 publication, window 2 | 7 | 4 | 5 | 5 | the same |
| T7 publication, window 128 | 7 | 2 | 3 | 3 | the same |
| Restart barrier | 1 | 1 | 1 | 1 (2 on Linux) | `test_local_paged_store_startup` |
| 5M publication, 128 per window | 2,887 | 24 | 25 | 25 | `docs/measurements/sync-groups-20261107/README.md` |

The T7 rows are deterministic: T7 fails if a counter differs from the calls the seam recorded. At 5M the publication holds 1,441 owners plus the lease (group A, 12 windows) and 1,441 packs plus four files (group B, 12 windows); with barrier C that is 25 `F_FULLFSYNC`, against 5,775 for the per-file shape. Summed over a 5M cold-bootstrap run's "publication synced" lines, the 2026-11-07 queue run counts 27 device flushes after and 5,777 before (`docs/measurements/sync-groups-20261107/overnight-summary.txt`). Those runs predate the `storage.meta` sync in the restart barrier, which adds one `fsync` per restart on Linux and no flush on macOS.

## Cancellation and failure

A window is atomic with respect to cancel: the files it created are synced and closed before a cancel is acted on, so the ledger's "sealed" count is exact. A failure in a window (a write, the group sync, a close) closes the window's other open files, then fences the store with the first error; a failed close is not retried. The group sync's own semantics on error are completion's: files after the failing one are not synced, the directory is not, and nothing is promised for the prefix.

## Counters

`DiskStore::sync_stats()` returns `SyncStats {total, startup, publication}` of `SyncCounters {files, directory_syncs, device_flushes, groups}`. Operations count each request that completed without error (`StorageSession::sync_performed`):

- A `Sync` is one file and one flush.
- A `DirectorySync` is one directory sync and one flush.
- A `SyncFiles` of n files is one group, n files and one directory sync, with one flush on macOS or n + 1 elsewhere.

`total` keeps its final value after the store closes; `startup` is the session's counters when the store became ready; `publication` is the last published publication's delta. The agent logs the publication counters when they change ("publication synced N files in N groups, ..."); the relay reports them in its status JSON (`sync_*`, `publication_*`).

## Power-cut model

`test_local_publication_power_cut` (T7) replaces completion's sync calls with a seam that applies each `fsync`, directory `fsync` and device flush to a model of the disk. It materializes every event in four cut views (durable or all entries; unsynced data truncated or zeroed) and a kill view, and requires the restarted store to select either the generation before the publication or, once publication began, the one it publishes. Restart-then-republish cycles start from every fifth boundary and always from the one completing `Replace`; before the rerun reports ready, a cut may also leave whatever the cut it restarted from allowed. Store creation is cut at every event after `storage.meta` exists, and the restarted store must have made `storage.meta` durable when it reports ready.

## Design vs code

See the ledger (`docs/status/namespace-s1-s2.md`).
