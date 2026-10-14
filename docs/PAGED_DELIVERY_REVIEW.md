# Validated Architectural Blueprint: paged delivery completion

**Date:** 2026-10-14
**Status:** Consolidated source review finds no P1. Six P2 corrections are implemented; final qualification and production activation gates remain open.

The Lead Software Architect reviewed the complete delivery component at high reasoning effort. The review covers scalability, edge cases, dependency/state ownership and security. [Implementation evidence](IMPLEMENTATION.md) records the tested commits; [500k delivery measurements](../benchmarks/paged-delivery-measurements.json) record complete-key readback and admitted capacities.

## Review findings

| Finding | Correction |
| --- | --- |
| Optional compaction retries delay required upstream delivery | Skip pre-IO admission refusal; drain accepted publication normally |
| Maintenance recovery failure loses its original exception | Carry the original exception through terminal recovery, confirmation and sink handoff |
| Incoming reset profile defines its own authorization | Bind authorization to the trusted host profile |
| A retained closed facade retains candidate resources | Release the candidate after worker settlement and sink fencing |
| Cancelled construction continues useful work | Check an embedded atomic stop request between work budgets; publish Ready after cleanup |
| Optional certificates starve required pre-begin construction | Yield cached evidence on owner-side pressure and retry after asynchronous retirement |

Workers retain actual capabilities and copied allocation factories. The owner rechecks physical selection before consuming a publication plan. Selected recovery, confirmation and sink replacement precede upstream feedback. Reclamation credit comes only from the collector's durable settlement. These boundaries require no sibling library API change.

The 500k synthetic cases reach approximately 204.75–204.88 MB combined admitted heap and 116.83–198.84 MB mapped peaks. The measurements do not establish RSS, sustained fleet throughput or power savings.

## Production activation

1. Append `LocalImageFormat { FlatV1, PagedV2 }` and a default `FlatV1` writer preference to `CoordinatorOptions`. Parse strict `image_format` values `flat-v1` and `paged-v2` through the managed configuration loader shared by agents and relay bundle sources. Keep plans and proof factories private.
2. Read either format regardless of writer preference. A configured flat writer uses the borrowed paged source adapter for its next forward update; changing configuration alone does not replace an unchanged selected head.
3. Allow compaction to be disabled or use a bounded finite interval. Retain the initial eight-pack, 32,768-recipe and 16 MiB output caps, and pass configured view limits into construction.
4. Preserve construction → base recheck → dependency durability → root selection → actual recovery → confirmation → sink handoff → feedback. Cancellation stops unpublished construction; accepted originals still settle authentically.
5. Qualify all review corrections with native, ASan/UBSan, TSan, Linux and FreeBSD gates, the complete native suite and minimal SDK builds. Exercise real authority → configured paged relay → configured agents → independent readers, including restart, reset rejection, v1-over-paged operation and two-bundle worker sharing.
6. Document source/dependency SHAs, physical coexistence requirements and measured footprints. Older binaries cannot read a selected v2 head.

Private component acceptance follows the final affected gates. Production activation follows assembled process qualification. The blueprint authorizes component implementation without individual approval checkpoints.

## Stage 6

1. Sustain mixed updates and fan-out alongside compaction and pinned-reader pressure. Attribute the outstanding Linux pressure failures.
2. Drill crash, restore, restart, cancellation and credential revocation across the assembled system. Check recovered selection and exact disk debt.
3. Measure reproducible capacity and produce pinned packaging. Report CPU demand, queues, admission and storage costs before making fleet or efficiency claims.
