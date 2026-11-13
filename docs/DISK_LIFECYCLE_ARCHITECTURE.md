# Disk lifecycle

The agent's store directory is the agent store (namespace delivery D8): one writer, `AgentStore`, holding `owner.lock` for its life; the table `selected`; `change`; and every generation's and segment's files under names derived from what they hold. The layout this page once planned (a `storage.meta` identity, ownership records in each `.lease`, a streaming census and a lease-protected collector) is gone, with no migration (D43).

| Concern | How the store handles it | Where |
| --- | --- | --- |
| Startup | One barrier, `.tmp-` names removed, the table read and each entry's `.image`, `.read` and `.live` checked against it, every `change` word bumped | `src/detent_local/agent_store.cc` |
| Publication | Files staged and linked, synced in two groups; the table staged, synced, renamed over `selected`, the directory synced, then `change` | `AgentStore::publish`, [change-file.md](mechanisms/change-file.md) |
| Accounting | `DiskLedger`: `used` is the store's census; a reservation covers one cycle's peak until it settles | `include/detent_local/disk_ledger.hh` |
| Collection | Every generation the table does not reach, `.lease` first under an exclusive lock, except one a reader holds; a deferred collection runs again after 1 s doubling to 30 s | `AgentStore::collect`, `DiskStore::collection_retry_at` |
| Crash safety | A cut at every primitive of startup, publication and collection recovers to the acknowledged table or the one in flight (T7) | `tests/detent_agent/test_agent_store_crash.cc` |

The wiring of `DiskStore`, the coordinator and readers onto the store is in [status/namespace-s7.md](status/namespace-s7.md), "Wiring"; the integration page is [DISK_STORE_INTEGRATION.md](DISK_STORE_INTEGRATION.md).
