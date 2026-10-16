# Validated Architectural Blueprint: bounded fleet qualification

**Date:** 2026-10-15
**Status:** Lead Software Architect design GO; implementation and qualification active.

The next [Stage 6](STAGE6_ARCHITECTURE.md) component qualifies sustained delivery with a native streaming transport driver and a separately sized durable SDK cohort. Transport probes do not represent durable agents or independent failure domains. The existing Python harness retains process ownership and the producer oracle.

The [runner reference](FLEET_QUALIFICATION.md) describes invocation and evidence limits.

## Implementation boundaries

1. Add `benchmarks/detent_fleet_load.cc` and private `benchmarks/fleet_load.{hh,cc}` for fixed probe slots, scheduling and aggregate statistics. `tests/detent_system/run_fleet.py` reuses `Actors`, `Evidence`, `Authority`, `ManagedCohort` and `Writers`. Keep producer sequencing and expected state in one implementation.
2. Add a private `ProbeAccess` seam in `src/detent_delivery/client_probe_internal.hh`. Reuse production TLS transport, bounded HTTP/control decoding, session operation guards and `FragmentDecoder`. Production consumers build semantic records; probes hash and release individual decoded fragments. A separate `ProbeOffer` carries window, target, kind, byte counts and digest, and confers no validated snapshot or publication capability.
3. Validate session/window binding, coding, offsets, fragment checksums, decompression bounds, finality and complete HTTP framing. Hash concatenated decoded payload independently of random framing update IDs. During writes, require integrity and monotonic cursor progress. At quiescent checkpoints, reconcile writers, establish a quorum boundary and compare snapshot digests at that exact cursor against a fully validating reference. Verify every definition independently through durable SDK readers. Different delta compositions cannot establish semantic equality by byte comparison.
4. Preadmit slots and use fixed owner shards, bounded handshake/control concurrency and a separate active-decoder allowance. Configure relay session, poll, connection and transfer limits with control headroom. Cold transfers proceed in bounded batches; holding all successful response headers until every probe arrives would deadlock above transfer capacity. Report cold bootstrap, warm parked sessions and reconnect churn separately.
5. Extend the deterministic writer corpus with typed definitions, 1/10/1,000-operation transactions, no-change, deletion/reinsertion and CAS conflict. Preserve one outstanding command and exact retry identity per producer. Order overlapping-key oracle updates by commit revision, rather than response arrival. Terminal receipts advance sequences according to the authority contract; unresolved outcomes fail qualification.

## Scalability and resource admission

Measure seeded voter and relay baselines, then marginal probe cost at 500 and 2,000 before admitting 10,000. Include decoder workspace, TLS and kernel socket memory, durable-store heap/mappings, coexistence disk, descriptors, ephemeral ports and expected transfer volume. On the 32 GiB development host, begin with a 24 GiB workload ceiling, reserve 8 GiB for the OS and measurement infrastructure, and reduce admission when measured availability requires it. Reject an oversized requested population instead of silently shrinking it.

Calibrate the 500k corpus at 16/64/256 producers before selecting paced workloads near 80%, 100% and 125% of the measured sustainable rate. Measure generator utilization explicitly: Python producer actors alone do not prove simultaneous network requests or server capacity. Bound pending work and count scheduled work refused by occupied producer slots separately from server admission. Use fixed histograms instead of unbounded latency samples.

## Ownership, edge cases and security

Retain admitted slot state through every original callback. Shutdown stops admission, cancels accepted requests, reaps originals, attempts bounded Close, drains coordinators and joins owners before releasing accounts. Exercise partial launch, held Open/Next/Close, truncation and cancellation. Reuse TLS hostname verification and terminal trust classification; never record bearer tokens. Bound responses, retry deadlines, evidence and diagnostic samples.

Record frozen source/dependency fingerprints, effective limits, seed, host contention, CPU, RSS, mappings, socket memory and egress. Report CPU-seconds per committed change and delivered GiB. Reuse the current dependency baseline; further library extraction is outside this component.

## Complete-component acceptance

1. Deterministic framing/control negatives and held-original drainage, with exact oracle and receipt reconciliation.
2. A 500 → 2,000 → 10,000 real TLS session ladder with raw/zstd, cold/warm coverage and fairness.
3. A 30-minute mixed run with 500k knobs, 256 producers and a preflight-sized durable cohort.
4. Stable occupancy below capacity, bounded refusal above it, and settled selection/reservations/debt matching retained owners.
5. Frozen builds with native scale evidence and reduced sanitizer/Linux/FreeBSD checks, followed by one consolidated component review.
