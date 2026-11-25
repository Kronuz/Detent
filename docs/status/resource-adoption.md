# Resource adoption (EX-1)

> Status: ACTIVE (2026-11-25) Detent's byte accounting, allocation adapters, deferred retirement and
> disk-ledger arithmetic are `allocators`' (D6). Quest `resource-adoption`; the journal is
> `.campaign/detent/journal/resource-adoption.md`.

## What Detent keeps

| Header | Now |
|---|---|
| `include/detent_distribution/storage.hh` | `StorageLedger` = `allocators::ByteAccount` (its `Ticket` is `ByteCharge`), `StoragePressure` = `ResourcePressure`, `PagedStorage` |
| `include/detent_distribution/allocator.hh` | `StorageAllocator`, `StorageDelete`, `StorageOwner`, `make_storage_owner` over the charged allocator |
| `include/detent_distribution/buffer.hh` | `OwnedBuffer`, `make_owned_buffer` |
| `include/detent_distribution/memory_resource.hh` | `LedgerMemoryResource` = `ChargedMemoryResource`, `make_storage_resource` |
| `include/detent_distribution/retirement.hh` | the retirement runtime, domains, factories, handles and test seams |
| `include/detent_local/disk_ledger.hh` | `DiskLedger`: census, two-slot registry and permits; `ReservationBook<3>` holds used, reserved and peak |

The names stay because 255 files use `StorageLedger` alone; each is one alias, not a second
implementation. `src/detent_distribution/retirement.cc` (403 lines) is deleted and
`detent_resources` is an interface target over `allocators::resources` and `allocators::retirement`.

## What allocators gained

Detent's copies had moved past `allocators` since the 2026-11-02 attempt. Before deleting them,
`allocators` took what they had (allocators:6850f04):

1. The retirement lost-wakeup fix (Detent:d5d9bc9b): the worker decides drainage and pending from
   one `admission` load. `RetirementTesting::interleave` and `test/retirement.cc` (`drains_across`)
   came with it; with the old two-load step, close and rollback at `worker_admitted` both lose the
   wakeup.
2. `ByteCharge::absorb` (Detent's `Ticket::absorb`, used by frame input, session frames and
   namespace sessions).
3. `ReservationBook::recount(census)`: main's `DiskLedger` settles to the census a publication left
   instead of adding a delta.

## Rulings

1. `DiskLedger::reserve` checks the registry and the reservation identity before admission, so a
   refusal for a full registry leaves the book's peak unchanged. When both the registry is full and
   the bytes do not fit, the refusal now names the registry; both are `DiskPressure`.
2. A9: main deleted the paged segment collector and its test (Detent:44e8d0c9), so variant 13 and its
   fix (Detent:febdef6) have nothing to apply to. The remaining tests that admit `limit - used` hold
   the ticket across a refusal or an operation that charges nothing, never across a retry that needs
   retired headroom.

## Evidence

Pending: see the journal's Evidence table and `docs/perf/resource-adoption.md`.
