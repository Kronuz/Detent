# Grouped durable image preparation

**Date:** 2026-10-25. **Status:** Validated architectural blueprint; implementation and qualification pending.

Prepare immutable files in durability groups through completion's existing image-preparation machinery. Keep the default single-file API and its barriers unchanged. Detent opts in only after crash-cut tests and downstream qualification.

## Evidence and scope

The [5M capacity trial](CAPACITY_QUALIFICATION.md) leaves 1,441 segment packs and 1,441 segment ownership records in each selected receiver directory. Generation ownership, the generation image and its descriptor add three preparation children: 2,885 in total. `PublicationOperation` in `src/detent_local/disk_store.cc` prepares them sequentially. Each preparation performs file sync and directory sync before returning a prepared-image capability.

This gives 2,885 preparation directory barriers and one selection barrier. The proposed grouping reduces that operation count to two preparation barriers and one selection barrier. File-sync count remains 2,885 in the first implementation. These are source-derived operation counts, not measured speedups. A short relay profile catches full-sync operations but does not establish their share of total bootstrap time. This component does not establish the cause of the separate 500K coordinator CI deadline.

Segment packing is already near its existing 1 MiB capacity. Increasing the segment limit changes another mechanism and is outside this component.

## Durability order

An ownership record permits recovery to identify an unfinished file. Its directory entry must survive before the corresponding payload can be created. The selected-generation name may become durable as soon as replacement runs, so every dependency must already be durable before submitting that replacement.

```text
Write + file-sync segment owners and generation lease
  -> directory barrier A
Write + file-sync packs, generation image and descriptor
  -> directory barrier B
Replace selected
  -> directory barrier C
```

Reused files retain their existing dependency validation and leases. The first group includes the ownership records for every newly created payload; the second includes only payloads whose owners passed barrier A. A failed barrier prevents advancement. Closing a file after sync does not substitute for either directory barrier.

## Completion API

Extend completion's existing image preparation with an opt-in preparation group. Reuse its per-file state machine and primitive drivers. Detent must not implement another create/write/sync sequence. Freeze membership before execution and reject groups spanning backend or directory ownership identities.

A file that has passed file sync is not yet a `PreparedImage`. The group holds that intermediate state privately. Only successful directory sync seals the exact admitted members and exposes their prepared-image capabilities. Tokens must remain tied to their backend and owned directory. Existing constructors and their default behavior remain unchanged.

Initially execute file operations sequentially and close each file after its sync, keeping descriptor use independent of member count. Charge group membership, names and retained buffers through the existing allocation context. Keep admission and leases until originals settle, including when the caller cancels. A failed close is never retried. Failed or cancelled groups cannot manufacture successful preparation capabilities.

Completion already supplies `IO::sync_files(span<File* const>)`, including macOS device-level amortization. A later extension can use bounded windows of open files through the same group API. It must reuse this mechanism rather than introduce another batch-sync policy. That extension requires separate measurements; this component claims no reduction in file-sync calls.

## Detent integration

`PublicationOperation::sealed_` and cancellation's `prefix_usage()` assume the current interleaved artifact order. Grouped preparation requires explicit accounting for created, file-synced and directory-sealed members. A count of completed files cannot stand for a durable prefix after reordering.

Preserve durable orphan ownership when a later payload fails. Keep uncertain files charged until recovery or existing cleanup proves their disposition. Do not delete them merely because a facade was cancelled. Selection still uses the existing publication operation and final directory barrier, with no disk-format change.

## Qualification

1. Inject interruption before and after every create, write, file sync, group barrier, selection replacement and final barrier. Recover through the real reader. A recoverable payload must have a durable owner, and a selected generation must have every dependency.
2. Exercise partial writes, barrier errors, cancellation with originals in flight, duplicate membership, empty groups, exhausted tokens and cleanup. Verify that the unchanged singleton path retains its original sequence.
3. Run completion's driver and platform suites, sanitizers and the journal compatibility gate. Record immutable revisions and Detent's coherent scratch dependency closure before changing production pins.
4. Compare the same saved corpus before and after under an exclusive host hold. Record actual primitive counts, bootstrap wall and CPU time, peak resident memory and file descriptors. Include publisher latency under concurrent writes, failure recovery and both relay paths.

## Limits and status

The blueprint is source-reviewed; no group API or performance result exists yet. A filesystem or driver that reports success without providing its promised durability can still lose acknowledged state. Grouping cannot correct that silent backend failure. An unsupported backend must refuse the opt-in rather than silently weaken its barriers.

This design adds group metadata proportional to admitted file count and preserves sequential file sync in its first scope. It does not change journal acknowledgements, consensus wire bytes, application formats or default completion behavior. Cross-consumer API review and platform qualification remain required before publication and adoption.
