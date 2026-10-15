# Production operating contract

Detent targets one regional fixed three-voter authority, durable relays and host-local agents. The components pass their recorded safety and recovery checks. Sustained mixed-fleet qualification is active in [Stage 6](STAGE6_ARCHITECTURE.md); the repository does not yet establish a production throughput, propagation-latency or 10,000-durable-agent capacity guarantee.

## Writes and retries

Transactions are atomic within one namespace. Producers authenticate under configured credentials and use generations with contiguous sequence numbers. Persist the exact request until its outcome is known. Retry an unknown outcome with the same generation, sequence and contents; generating a new sequence can perform a second mutation.

The default result window retains the latest 256 results in a producer generation. It has no wall-clock retention promise. A result that leaves the window is unavailable and cannot be executed again under the old sequence. Conflicts and logical capacity rejections consume sequence numbers and retain their deterministic outcomes. Admission rejection and transport uncertainty need separate handling. See the [authority protocol](AUTHORITY_ARCHITECTURE.md) for the complete state machine.

Quorum-backed reads and result lookups require a functioning majority. A minority cannot accept successful writes. Membership is fixed; expanding or replacing a voter set in place is outside the supported contract.

## Delivery and local reads

Applications read their agent's selected local generation and configured defaults. They can continue reading last-known-good state while upstream is unavailable. This provides local availability with potentially stale values; applications requiring an upper bound on staleness must enforce one separately.

Authority identity, authorized scope and revisions bind each delivered update. Changing endpoints does not authorize identity reset or revision regression. Authorization denial fences further distribution, but it cannot erase bytes already mapped by a reader. Use a separate secret-management system for data that requires revocation to erase application access.

A single managed endpoint does not provide automatic leader failover. The [managed failover component](MANAGED_FAILOVER_ARCHITECTURE.md) remains pending. Relay readiness proves recovered storage and authenticated upstream availability at its last successful interaction, rather than continuous authority quorum availability.

## Storage and upgrades

Keep durable authority, relay and managed-agent generations outside runtime directories. Runtime files can disappear at boot. Service managers own foreground process lifecycle and logging; use the [service templates](SERVICES.md) and private credential permissions.

Deploy readers that understand both flat and paged images before enabling `image_format: "paged-v2"`. Flat writing remains the default. Selecting flat writing later leaves an unchanged paged selection in place until a forward update publishes a flat image. An older binary that cannot read paged images cannot safely consume that store. Retained reader mappings and old generations need coexistence capacity during transitions.

Process-kill and injected-I/O checks qualify their stated failure boundaries. They do not simulate device power loss or establish the guarantees of every filesystem and storage controller. Mapping, admitted heap, disk accounting and process RSS measure different resources; budget and observe each separately.

## Restore and audit

Restore creates a new authority epoch and fences requests carrying the old identity. Provision the new fixed voter set and grant agent reset explicitly. The current offline admin tool runs a single-voter workflow; restoration into a fresh three-voter group remains a Stage 6 implementation gate. Do not describe that tool as a complete clustered disaster-recovery procedure.

Retained producer results support bounded retry reconciliation. They are not an unlimited audit log. Operators needing complete change history must retain it independently. Back up and verify authority recovery artifacts together with their identity, configuration and credentials; copying a live runtime directory is not a backup protocol.

## Qualification limits

The measured 500,000-key image is roughly 117 MB before coexistence. Ten thousand complete copies alone require roughly 1.17 TB; transport-only subscribers do not establish durable-agent storage capacity. Two local VMs also provide only two VM failure domains, even when they host three voter processes. Independent-machine, device-power-loss and physical-fleet certification require the corresponding hardware.

Use the [measurement records](../benchmarks/paged-delivery-measurements.json) and [implementation ledger](IMPLEMENTATION.md) for exact source revisions and test boundaries. Capacity settings are finite admission ceilings. Sustainable operating points must come from the workload and topology being deployed.
