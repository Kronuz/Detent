# Resource adoption

> Status: ACTIVE (2026-11-02) Detent adopts the shared resource primitives in `allocators` by owner ruling; see [the decision](../SHARED_LIBRARY_DECISIONS.md#resource-accounting-one-shared-implementation-november-2).

## State

Branch `phase/resource-adoption`, from Detent:6a31280. The code is the resource part of `track/resource-adoption` (Detent:39f19a4, benchmark sources from Detent:6427b5e), reconciled onto current main.

## Decisions

1. Authority retained-state funding (Detent:33d36bf `src/detent_authority/*`, `tests/detent_authority/test_{node,service}.cc`) is not taken: it needs `consensus:61149a2` (`WorkerLimits::retained`) and is a separate pin wave and feature; the extraction architecture also excludes it from EX-1.
2. Pins: `allocators:07de45d`, plus `completion:21809c7` and `journal:dbf753f`, the immutable closure the extraction architecture names (their allocation and reservation names forward to allocators). `stash` and `lru_cache` keep main's pins.

## Done

(in progress)

## Next

Build, full suite, sanitizers, first-read measurement.

## Blocked on

Nothing yet.
