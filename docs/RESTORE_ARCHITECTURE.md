# Validated Architectural Blueprint: fresh three-voter restore

**Date:** 2026-10-16
**Status:** Lead Software Architect design GO. Implementation and executable qualification remain pending. Compilation and load qualification resumed after the benchmark hold.

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

Add an opt-in `RestorePolicy` to `ServiceOptions`. Quarantine accepts only the designated restore credential on producer Open/Transaction/Read/Result/Close and a restore-only barrier route. Reject ordinary credentials and all distribution/subscription routes; expose quarantine separately from storage readiness. Active mode rejects the restore credential. The current `publish` flag only restricts writes and cannot enforce this policy.

Retain a restore configuration stanza binding the job ID, backup digest, target context, voter/profile digest and receipt path. Before public readiness, active startup validates a held, trusted receipt against its local voter/store identity and recovered committed/applied floor. Missing stores fail instead of taking automatic creation. Bind initial checkpoint descriptors as provenance while allowing later legitimate checkpoints and writes. Configuration and receipt ownership are trusted: removing the stanza is a privileged bypass, outside this first component's enforcement boundary.

The restore-only barrier uses `Node::read()` and its `Completion.index`. Stop bootstrap writes, obtain boundary B, and wait for every member's committed/applied indexes to reach B. Health guides waiting; independent offline recovery supplies authoritative proof: committed/applied at least B, complete semantic equality and a checkpoint through at least B on each voter. A quorum-backed read alone cannot attest every follower's local state.

Open the backup once under a held trusted parent, validate ownership and metadata, then hash and decode the same bounded chunks from that descriptor. Restore from the resulting owned immutable State without reopening its pathname. Create targets exclusively under held trusted parents, retaining distinct directory identities and checking named/held agreement. Write artifacts through pinned public `PosixIO::create_exclusive()`, `File::write_at()`, `File::sync()` and `PosixIO::sync_directory()`, retaining the owner lock through both durability barriers. The pinned macOS directory barrier requires that lock; legacy admin's raw `fsync` helper does not supply this contract.

Old clients retain their selected generations and fail closed against the new identity. Changing endpoints does not authorize a reset. Reconfigure relays and agents with the new identity and their existing exact `reset_from` capability; select the new generation only after a full snapshot. Existing SDK mapping leases remain readable under the established retention contract.

Empty authorized scopes are configuration, while namespace watermarks belong to the old authority history. Restore their configured accessibility and empty definition results without carrying historical watermark-only namespace entries into the new epoch.

## Architectural checks and component gate

| Check | Required boundary |
| --- | --- |
| Scalability | One validated source State, bounded record/batch buffers and fixed process ownership. Measure 100k/500k peak heap, RSS, disk coexistence, replay and checkpoint time. |
| Edge cases | Empty state/scopes, deleted namespaces, namespace prefixes, maximum definitions, opaque overrides, encoded-byte splitting and a source log with an uncommitted suffix. |
| Dependency and state | No old Raft hard state, ballots, storage identities or producer receipts enter the new group. Each member checkpoints its own state. |
| Security | Validate artifact ownership, framing and profile before target mutation. Keep secrets out of logs; use configured TLS endpoints and terminate on trust or identity failure. |

Review the complete component against this consolidated matrix:

1. Malformed, truncated or incompatible backups leave targets untouched.
2. A real three-voter restore survives leader loss and exact retries; a minority cannot acknowledge writes.
3. Every member restarts with complete definition semantics and its own durable checkpoint.
4. Old requests and peer epochs are fenced; managed readers transition only after explicit reset.
5. Cancellation and interruption cannot produce an activation receipt or normal-service credentials.
6. Native, sanitizer, Linux and FreeBSD checks pass, with reproducible source and artifact fingerprints.

Include premature active startup, missing/swapped receipts, one lagging voter, restore-token subscriptions, artifact replacement during validation and restart after legitimate post-activation writes as explicit negative or recovery cases.

The private offline runtime is extracted, and export uses follower-local committed recovery. Native three-voter fixtures cover semantic recovery, independent checkpoints and reopening with both synchronous and completion storage; the legacy import/export round trip also passes (3/3, 16.56 seconds). The fixture waits for pending durable protocol work before asserting equality with the live committed cut. Backup envelopes, bounded bootstrap and enforced activation remain unimplemented. Two local VMs also cannot establish three independent failure domains or device-power-loss recovery.
