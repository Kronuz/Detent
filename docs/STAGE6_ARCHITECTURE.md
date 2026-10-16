# Validated Architectural Blueprint: system qualification

**Date:** 2026-10-14
**Status:** Lead Software Architect design GO. Implementation follows six complete components, each with consolidated review.

Stage 6 qualifies the implemented fixed-voter authority, durable relays and host-local readers under sustained traffic and failures. Two local VMs provide process and asymmetric VM-loss qualification. Three independent failure domains, device power loss and a physical 10k-agent fleet require separate hardware certification.

## Components

1. **Bounded runner and frozen evidence.** Add `tests/detent_system/` orchestration and `detent_fleet_load`, reusing the authority load runner, real network fixture, production clients/coordinators and complete-state oracle. Bound outstanding work, subscriber buffers, oracle state and artifact bytes. Record source/dependency revisions, seed, configuration hashes, backend, compiler, filesystem, CPU/RAM, FD limits and concurrent workloads. Preflight before launch; drain authentic accepted work on launch failure.
2. **Sustained delivery and overload.** Calibrate throughput before selecting an operating point. Each producer has one outstanding command and retries unknown outcomes exactly. Distinguish offered, admitted, acknowledged, rejected, unknown and reconciled outcomes. Below capacity, queue age, publication lag and debt must stabilize. Above capacity, bounded rejection and recovery are the gate.
3. **Fixed-voter failures and restoration.** Exercise leader death with pending writes, minority isolation, majority loss, delayed peers, restart and nonempty snapshot catch-up. Keep membership fixed. Restore into a fresh three-voter group under a new epoch using validated export/checkpoint mechanisms and bounded bootstrap transactions. Fence old-epoch requests and authorize agent reset separately. The current offline admin tool requires one voter and does not supply this workflow.
4. **Selection and resource conservation.** Apply heap, mapping, disk, session, connection, history and pinned-reader pressure independently, then together. At settled boundaries compare actual selection, complete semantics, producer receipts and an independent named-space census. Reservations drain; protected, corrupt and foreign artifacts retain debt; resources return to the retained-owner baseline.
5. **Reproduction and packaging.** Pin every dependency and build clean profiles without live overrides. Cover authority, relay, managed agent, reader-only SDK and legacy consumers. Test installation, foreground/daemon/status behavior, directories, restart and compatible upgrade/rollback. Freeze the established completion baseline; adopting the separate `sync_files` update requires its own durability-contract gate.
6. **Product contract.** State topology, capacity profile, retry/result horizon, staleness, restore, audit limits and reader-before-writer upgrade requirements. Dynamic membership and further library extraction stay outside this component.

Managed authority failover has a separate [validated component blueprint](MANAGED_FAILOVER_ARCHITECTURE.md). It adds bounded endpoint selection and periodic quorum revalidation; a single configured endpoint cannot survive leader changes without external routing. The next sustained-load component follows the [bounded fleet qualification blueprint](FLEET_QUALIFICATION_ARCHITECTURE.md).

Fresh three-voter restoration follows the [validated restore blueprint](RESTORE_ARCHITECTURE.md). Its implementation and executable qualification remain pending; the design uses the current qualified dependency pins and keeps activation separate from bootstrap.

## Executable presets

| Preset | Workload | Initial gate |
| --- | --- | --- |
| Smoke | Three voters, two relays, isolated/empty bundles, 16 writers, durable readers | 60 seconds, complete semantics and drainage |
| Writers | 500k knobs, 256 producers, uniform/hot namespace contention | Calibrate, then 10 minutes below/near/above plateau |
| Fan-out | 500 → 2,000 → 10,000 TLS subscribers, raw/zstd | 10 minutes per level, integrity, fairness and reconnect |
| Mixed pressure | 500k knobs, 256 writers, transport probes and a preflight-sized durable cohort | 30 minutes, sparse mutation and pressure recovery |
| Failure | Mixed workload with scripted voter/relay/agent failures | Reconcile every acknowledged/unknown outcome and debt |
| Soak | Highest qualified local operating point | 60 minutes, stable occupancy and drained shutdown |

```sh
build/benchmarks/detent_fleet_load \
  --scenario tests/detent_system/presets/mixed.json \
  --seed 17 --duration 1800 --output .scratch/stage6/mixed-17
```

Seed through supported bootstrap or verified production recovery. Include integer/boolean defaults, rollout presence, typed overrides, namespace prefixes and empty scopes. Mix sparse 1/10/1,000-definition updates, repeated transactions, CAS conflicts, no-change and deletion/reinsertion. Pause intake for a quorum barrier and complete-state comparison. Report CPU, RSS, admitted capacities, queue age, delivered bytes, storage amplification and warm/cold work separately.

The relay's fixed connection/session limits need finite startup configuration or a documented multi-host topology. Ten thousand transport probes qualify transport behavior, not ten thousand durable stores. The measured image alone requires roughly 1.17 TB for 10,000 full copies before coexistence; durable cohorts must be sized from measured local resources.

## Attribution and acceptance

Controlled contention attributes the native 500k feedback failure to the fixture observer deadline: Close arrives where the server expects feedback, masking the observer failure. Two 60-second fixtures fail under 32 CPU workers; the same frozen production libraries pass both with a 240-second fixture budget (135.86 seconds total). The [measurement record](../benchmarks/paged-contention-measurements.json) retains source/binary fingerprints and reproduction. Production deadlines remain unchanged. Earlier Linux shared-host pressure failures still require separate attribution.

The supported initial product is one regional fixed three-voter authority with namespace-scoped atomicity and last-known-good local reads. Authorization denial fences distribution without promising erasure of retained SDK mappings. Producer result retention is bounded by operation count; it does not promise a time-based retry horizon or unlimited audit history. Restore uses a new epoch. Latency and fleet-capacity targets remain unqualified until the corresponding workload and hardware gates pass.
