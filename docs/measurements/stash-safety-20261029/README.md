# Stash safety dependency qualification

**Status:** Native SDK and upstream tests pass; full-product hosted CI pending.

The candidate changes only the stash pin from `stash:2908b488c10dbd6830c384f53358783639a36965` to `stash:fa1936ee2ff07afe55276722de12d712b15f17c7`. The target includes the reviewed insert-descent task-loss and cleanup-lifetime repairs. Logger and scheduler pins remain unchanged; this does not take later scheduler API or Debouncer changes.

A clean public-pin Release SDK build passes seven tests: logging, agent basics, canonical snapshots, shared-memory concurrency, reader capacity, agent CLI and system-agent behavior. The two standalone stash tests pass with `-O2 -UNDEBUG`, including concurrent producers with tasks due at the current clock. Default Release disables the smoke test's assertions; that initial run is superseded by this explicitly checked profile. The logs preserve both final suites.

| `StashContext` | Before | After |
| --- | ---: | ---: |
| Size | 56 bytes | 4,352 bytes |
| Alignment | 8 bytes | 64 bytes |

The logger's single `Scheduler<Logging>` embeds two contexts (`scheduler.h:149-150`, `logger.cc:504` at the unchanged pins). Their size sum grows by 8,592 bytes, about 8.39 KiB, before surrounding-object padding. This is fixed object size, not process RSS. No Detent throughput improvement or neutrality is claimed; upstream reports a small tight-insertion cost in exchange for the correctness repair.

All C++ consumers require a rebuild because the embedded context layout changes. The consumer's logger/scheduler source compatibility is checked by the clean selected SDK/legacy-agent target rebuild and operational tests. The full authority/relay/managed-agent closure is left to hosted qualification before this step is considered complete.

`context-footprint.json` records the exact old/new source revisions. `build_candidate.py` records the SDK profile and selected tests; `measure_context.py` compiles the same size probe against each header. The scripts use the qualification scratch layout under `.scratch/stash-adoption-20261029`, with the original header from the immutable LRU qualification closure. Run compilation/tests under `quiet light`. Library qualification additionally sets `CMAKE_CXX_FLAGS_RELEASE='-O2 -UNDEBUG'` in a separate build of the pinned stash source.
