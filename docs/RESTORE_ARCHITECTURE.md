# Validated Architectural Blueprint: fresh three-voter restore

**Date:** 2026-10-16
**Status:** Lead Software Architect design GO. Implementation and executable qualification remain pending; compilation and load runs are paused for concurrent benchmarks.

Restore validated definitions through bounded, ordinary replicated transactions into a quarantined new three-voter group. Each voter creates its own durable consensus state. Activate the group only after complete semantic comparison and independent offline checkpoints on all three members.

This implements the restore component of the [system qualification plan](STAGE6_ARCHITECTURE.md). Preserve the qualified completion `35d8e2a`, journal `57e6250` and consensus `b7073bb` pins. The later consensus wire changes are a separate [dependency adoption gate](DEPENDENCY_UPGRADES.md).

## Recovery and backup

Extract the offline runtime from `src/detent_authority/admin.cc` into private `admin_runtime.{hh,cc}`. Separate committed application recovery from the existing single-voter election path. Recovery acquires the storage lock, recovers the durable committed boundary and replays application state without advancing election time. A follower-local checkpoint uses `Node::checkpoint()` after application replay settles. Preserve legacy import/export behavior.

Add private `authority_backup.{hh,cc}` and an `admin backup CONFIG OUTPUT` command. Stream `ImageEncoder` through bounded chunks into a versioned envelope containing source context, semantic limits, applied index/term, revision, extent and checksum. Validate with `ImageDecoder` before creating target directories. The artifact represents one voter's durable committed cut; it can lag the group's latest commit. Credentials stay outside the artifact. Synchronize the completed file and parent directory through the qualified durability helpers; partial outputs remain invalid artifacts.

## Bounded bootstrap

A private `RestoreBatchCursor` traverses the validated source State through a resumable borrowed record cursor. Emit one namespace per transaction, bounded by 128 operations and the actual encoded request-byte limit. Preserve typed definitions, opaque override keys and empty scopes. Avoid `State::snapshot()` and full JSON materialization. New revisions, namespace watermarks and producer sessions belong to the new identity; retain old values only as backup provenance.

Add `tests/detent_system/run_restore.py`, reusing `Actors`, bounded `Authority.request` and exact retries. Parameterize authority and managed-cohort identities while preserving their current defaults. Require three fresh, physically distinct storage roots, distinct storage identities, consistent voter/profile configuration, and a fresh group plus restore epoch. Reject existing roots and filesystem aliases before mutation.

Initially configure only a private restore credential. Keep one outstanding command and retry identical bytes across leader changes. Capacity rejection or conflict fails the job. The first version refuses automatic resume into existing targets: an interrupted job stays quarantined, and another attempt requires fresh targets. This keeps durable command-progress recovery outside this component.

## Activation and managed readers

After a quorum-backed full comparison, stop all three members. Recover and compare every definition on each member, then checkpoint each independently. Write a durable completion receipt only after all three succeed. Configure normal credentials only after that receipt settles. Failure or cancellation at any earlier boundary leaves the job quarantined.

Old clients retain their selected generations and fail closed against the new identity. Changing endpoints does not authorize a reset. Reconfigure relays and agents with the new identity and their existing exact `reset_from` capability; select the new generation only after a full snapshot. Existing SDK mapping leases remain readable under the established retention contract.

## Architectural checks and component gate

| Check | Required boundary |
| --- | --- |
| Scalability | One validated source State, bounded record/batch buffers and fixed process ownership. Measure 100k/500k peak heap, RSS, disk coexistence, replay and checkpoint time. |
| Edge cases | Empty state/scopes, deleted namespaces, namespace prefixes, maximum definitions, opaque overrides, encoded-byte splitting and a source log with an uncommitted suffix. |
| Dependency and state | No old Raft hard state, ballots, storage identities or producer receipts enter the new group. Each member checkpoints its own state. |
| Security | Validate artifact ownership, framing and profile before target mutation. Keep secrets out of logs; use configured TLS endpoints and terminate on trust or identity failure. |

After the benchmark hold lifts, review the complete component against this consolidated matrix:

1. Malformed, truncated or incompatible backups leave targets untouched.
2. A real three-voter restore survives leader loss and exact retries; a minority cannot acknowledge writes.
3. Every member restarts with complete definition semantics and its own durable checkpoint.
4. Old requests and peer epochs are fenced; managed readers transition only after explicit reset.
5. Cancellation and interruption cannot produce an activation receipt or normal-service credentials.
6. Native, sanitizer, Linux and FreeBSD checks pass, with reproducible source and artifact fingerprints.

The current offline admin tool's `Runtime::ready()` elects a single voter, and `pump()` rejects outbound consensus sends. It does not implement this workflow. The source review establishes a design path, not executable restore evidence. Two local VMs also cannot establish three independent failure domains or device-power-loss recovery.
