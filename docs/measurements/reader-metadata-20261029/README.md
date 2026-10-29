# Reader metadata attribution

**Date:** 2026-10-29. **State:** Attribution complete, with no production optimization or resource adoption. The sub-millisecond acceptance gate is unchanged.

All 80 fresh processes pass: the first 20 unchanged keys from the prepared 5M generation, private/canonical resources crossed with observation off/on, rotated order and matching warmups. Every observed attachment reports 50 accumulated stages, exactly 880 metadata bytes read, four metadata closes, one attempt and no retries. The lease remains retained by the view. Caller allocation counts match exactly between observation off/on for every key, resource arm and phase. Constructor medians are 75 allocations for private resources and 79 for canonical resources.

## Observed caller CPU

Directory trust medians are 222.409 and 211.568 microseconds for private and canonical arms. The fixed metadata path has the following medians of per-process stage sums, in microseconds:

| Stage | Private | Canonical |
| --- | --- | --- |
| open | 69.112 | 69.732 |
| stat | 11.406 | 11.245 |
| checks | 15.619 | 15.555 |
| acl | 20.733 | 20.596 |
| read | 14.385 | 13.649 |
| close | 7.685 | 7.544 |
| decode | 27.487 | 24.720 |
| checksum | 2.599 | 2.605 |
| lock | 4.737 | 3.017 |
| named | 3.181 | 3.064 |
| binding | 1.847 | 1.849 |
| limits | 0.621 | 0.625 |

Stat is nested within checks; totals and children are not disjoint. Decode includes each decoder's checksum work, with the root image checksum recorded separately. These medians must not be added to reconstruct a process or subtracted independently to claim a removable cost. File opens dominate the separately timed metadata operations. The storage identity open is about 19.5–19.9 microseconds; the other opens are about 12–13 microseconds.

Observed complete attachment medians are 439.591/424.889 microseconds. Constructor CPU medians rise from observer-off 474.225/467.467 to observer-on 538.355/522.903 microseconds, so instrumentation overhead is material. This diagnostic uses a malloc-event hook in all four arms, preinitializes fixed observer storage and compiles temporary helper copies. It is not the uninstrumented acceptance run, and it establishes no latency win. Faults are process-wide and may include retirement workers.

## Validation and scope

Twenty observer-on/off case invocations pass; four privileged pathname invocations skip for lack of noninteractive sudo. Cases cover metadata corruption, lazy pages, retained views and legacy readers. Dedicated prepared-republish coverage is not included. Initial validation link failures expose missing fixture builder/export archives; both failure records are preserved. After building the missing closure, validation completes with matching outcomes. Neither link failure establishes a reader bug.

Only temporary source/archive copies contain observation. Original production code and disk bytes remain unchanged. Renamed inline helpers avoid mixing differing definitions across objects. Timers preserve `errno`, fixed counters allocate no observation storage during construction, and metadata close guards reuse the existing file owner. Required operations and byte accounting are checked before accepting a sample.

The [next validated blueprint](next-blueprint.md) permits one prototype replacing per-ancestor heap owners with a fixed 256-slot array of existing RAII owners. It changes no format, syscall checks, lease policy or shared-library API. Allocation time inside directory trust is unmeasured, so the prototype has no claimed win. End-to-end benefit must hold in both resource arms before unchanged 200/200 sub-ms acceptance per arm. Otherwise the prototype is discarded.

Raw records, temporary source copies, validation failures, exact identities and controller scripts are retained here. Scripts preserve their original scratch-layout assumptions and require the matching source checkouts, CMake metadata and prepared generation to reproduce; this directory is not a standalone benchmark package.
