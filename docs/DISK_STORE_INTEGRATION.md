# DiskStore integration

`DiskStore` (`include/detent_local/disk_store.hh`) is the coordinator's view of the agent store ([DISK_LIFECYCLE_ARCHITECTURE.md](DISK_LIFECYCLE_ARCHITECTURE.md)). It keeps one shape for its callers: `begin` a flat image or a paged plan, `selection()`, `confirm_recovered`, `close`.

```text
coordinator (owner thread)          worker thread              disk
  DiskStore::begin  -> prepare cycle
  DiskStore::work() -> DiskWork ----> run(): AgentStore::publish / collect ---> store/
  DiskStore::advance() <- settle (census, selection, errors)
```

- Startup. `make_disk_store` opens the agent store synchronously and recovers the selected generation. A fresh store needs an empty directory; an existing one needs its `owner.lock`. A generation that does not recover fences the store and keeps every file.
- Publication. `begin` checks the build (derived name, cursor, base, admission), reserves the cycle's peak, and queues one cycle: the generation as the store's one namespace, every other entry dropped. A refusal (`DiskPressure`, `StoragePressure`) queues nothing and leaves a paged plan whole. `ReachedNameMismatch` and `LeaseHeld` reject the publication; any other failure fences the store.
- Work. `work()` hands one publication or collection to the caller's worker; `advance()` on the owner settles it. The coordinator wakes at `collection_retry_at()` for a collection that left a reader's generation.
- Close. `close()` lets running work finish, drops pending work and releases the agent store; `drained()` reports it.

Tests: `tests/detent_local/test_disk_store.cc`, `test_paged_disk_store.cc`, `test_paged_store_startup.cc`, `test_paged_publication_intents.cc`, and the coordinator's `tests/detent_delivery/test_coordinator.cc` and `test_repair.cc`.
