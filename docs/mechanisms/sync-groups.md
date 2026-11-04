> Status: DRAFT (2026-11-04) How a publication reaches stable storage in sync groups, what startup syncs, and how the sync counters count. Workstream: namespace-s1-s2. Design: [namespace-delivery.md](../design/namespace-delivery.md) D11.

# Sync groups: one barrier per group instead of a flush per file

A generation reaches `selected` only after every file it names is durable. The store orders that with two sync groups and the table's own barrier, and a restart syncs no file.

```text
publication (agent and relay: one PublicationOperation in DiskStore)

  group A   segment owners, generation lease   create+write each (unsynced), SyncFiles per window, close
  group B   packs, image, live, read, descriptor   the same
  Replace   descriptor -> selected                  rename; then DirectorySync (barrier C)
  then      change increments                       no sync (startup bumps every word)

restart (StartupOperation)
  census  stat every entry, read storage.meta / selected / owner records read-only (no sync)
  barrier DirectorySync        -> then change bump_all, then ready()
```

## The order, and why each step is there

- **Owners before payloads.** An owner record lets recovery identify an unfinished pack or generation; its directory entry must be durable before the payload it names is created. Group A holds every segment owner and the generation lease, and its last `SyncFiles` also syncs the directory, so group B starts only after A's entries survive.
- **Payloads before the table.** Group B holds the packs, the image, `.live`, `.read` and the descriptor (the 176-byte table). The descriptor is fsynced inside the group, before `Replace` renames it over `selected` (D11, N6): a table renamed with unsynced bytes would otherwise become durable through the startup barrier and a power cut would revert a table already served.
- **Rename, then the directory barrier.** `Replace` is a rename; the following `DirectorySync` makes it durable. Only then does `change` increment.
- **Startup syncs no file.** Every file a durable table reaches was synced by the publication that wrote it, so the census only stats, and reads the records it must verify through a read-only open (`MutationRequest::read_only`). It keeps one `DirectorySync`, which makes a `selected` rename that a killed process left in the page cache durable before `ready()`; serving first would let a power cut revert a table a reader already used (ND-R9).

## Windows

A `SyncFiles` takes open descriptors, so a group is written in windows of at most `DiskStoreOptions::sync_group_files` files (default 128): each file is created and written without a sync (`PreparationFilePolicy::Stage`, completion `image.h`), the window is one `SyncFiles` request over its open files, then each is closed. The descriptor count is the bound, not the file count. Per platform the window costs what completion documents (`completion:docs/mechanisms/sync-files.md`):

| Platform | One window of n files |
| --- | --- |
| macOS | n `fsync`, one directory `fsync`, one `F_FULLFSYNC`: one device flush |
| Linux, FreeBSD | n `fsync` (Linux: a `sync_file_range` writeback hint on each first), one directory `fsync`: n + 1 flushes on ext4 and XFS |

At 5M a publication has 1,441 owners plus the lease and 1,441 packs plus four files, so with 128 per window group A is 12 windows and group B 12, and with barrier C the publication issues 25 `F_FULLFSYNC` on macOS, against 5,778 before.

## Cancellation and failure

A window is atomic with respect to cancel: the files it created are synced and closed before a cancel is acted on, so the ledger's "sealed" count is exact. A failure in a window (a write, the group sync, a close) closes the window's other open files, then fences the store with the first error; a failed close is not retried. The group sync's own semantics on error are completion's: files after the failing one are not synced, the directory is not, and nothing is promised for the prefix.

## Counters

`DiskStore::sync_stats()` returns `SyncStats {total, startup, publication}` of `SyncCounters {files, directory_syncs, device_flushes, groups}`. Operations count each request that completed without error (`StorageSession::sync_performed`): a `Sync` is one file and one flush; a `DirectorySync` one directory sync and one flush; a `SyncFiles` of n files is one group, n files, one directory sync and one flush on macOS or n + 1 elsewhere. `startup` is the session's counters when the store became ready; `publication` is the last published publication's delta. The agent logs the publication counters when they change; the relay reports them in its status JSON (`sync_*`, `publication_*`).

## Design vs code

See the ledger (`docs/status/namespace-s1-s2.md`).
