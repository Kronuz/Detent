# Managed prepared-reader activation

**Date:** 2026-10-26. **Status:** Validated architectural blueprint; implementation and qualification pending. **Decision:** GO for this bounded component; default activation requires the acceptance checks below.

Managed applications attach to publisher-prepared paged data. The publisher performs semantic recovery and upgrades old heads before advertising prepared-data readiness. Ordinary managed reads never silently invoke full recovery. The [accepted reader component](reviews/prepared-reader-component-2026-10-26.md) supplies checked attachment; its measured latency does not establish an unconditional deadline.

## Consequential defaults

1. Change `CoordinatorOptions::image_format` to `PagedV2`, including managed configuration's omitted-field behavior. Keep explicit `image_format: "flat-v1"` for compatibility; flat-specific fixtures select it explicitly.
2. Managed CLI reads/status and a named SDK managed-source factory use prepared attachment and `ViewLimits::prepared()`. Low-level standalone recovery remains explicit and unchanged. Add trusted `reader_validation: "full"` for flat/untrusted compatibility; never choose it because metadata is missing or malformed.
3. Prepared-data readiness requires a durable readable head. Process initialization and empty-store liveness remain distinct; an empty store can receive its first snapshot without claiming data readiness.

## Prepare once on the publisher

Extend [PagedPlanBuilder](../src/detent_local/paged_reuse.hh) with an explicit republish intent accepting an actual recovered paged base, fresh generation ID and existing factory/account. Share its 320-byte root copy/hash/seal engine. Preserve cursor, profile, tree references, counts, registry and histogram. Emit no segments and fabricate no `ValidatedDelta`.

After sealing, mint existing `StateEquivalenceProof` bound to the exact old descriptor/directory and new descriptor. Restrict minting to this builder, checking unchanged root semantics and exact recovered ownership. Expose evidence through the existing maintenance pattern. Retain the base lease through publication/recovery settlement. Work includes existing O(segment-count) dependency admission; it performs no knob traversal or payload rewriting.

In [coordinator.cc](../src/detent_delivery/coordinator.cc), add preparation after initial recovery and before prepared readiness. Compare existing `.read` against metadata derived from the verified generation. A missing record triggers republish; valid metadata is a no-op. Malformed, unsafe or unsupported metadata fences preparation and preserves evidence. A flat head uses the existing `PagedMaintenanceBuilder` migration once, on the configuration worker.

Publish through `PagedPublicationArtifacts` and `DiskStore`: lease, root, `.read`, descriptor, selected replacement and final barrier. Recover/confirm through existing managed recovery using the verified prior registry; require unchanged pages to reuse validation. Do not hand-construct a recovery capability or attach `.read` in place. Restart recognizes a completed prepared head and performs no further republish. An interrupted attempt retains conservative debt and the existing selection-recovery rules.

## Readiness and entry points

Add explicit prepared readiness to `CoordinatorStatus`; [managed_host.cc](../src/detent_agent/managed_host.cc) announces data readiness only after durable selection, actual recovery confirmation and metadata verification. Startup preparation runs without an upstream session. Identity-reset cases remain unready until a prepared head for the configured identity exists. Cancellation drains accepted IO; uncertain selection never reports ready.

Update `cmd_managed_get`, managed status, argument validation and `ReaderCapacity`. Initialize prepared logical bounds before applying individual explicit caps; preserve shared mapping admission and heap limits. Construct the synchronous source inside error handling: try durable attachment, optionally launch the user daemon, then retry construction within a finite startup allowance. Remove the redundant initial refresh. Corruption remains `SourceFailure`, including with baked defaults.

Use one validated finite managed startup allowance for the coordinator and launcher. The current 30-second detach timer and two-second retry window cannot qualify a roughly 60-second old-head preparation. Select the packaged allowance from measured preparation plus drain margin before activation; do not silently alter delivery deadlines. Timeout cancels preparation and reports unavailable without starting client-side recovery.

## Publisher identity and path trust

Use `geteuid()` for per-user managed sources. System sources default to `detent` on Linux/FreeBSD and `_detent` on macOS. Add trusted `publisher_user` configuration accepting an account name or numeric UID, with an explicit CLI override and numeric SDK option. Resolve names with bounded `getpwnam_r` storage; reject missing accounts and width overflow. Packages materialize numeric UIDs from their configured service account into the public reader configuration, keeping custom accounts synchronized without exposing daemon credentials. Never infer publisher identity from the data directory. Include resolution/configuration cost in startup measurements; NSS latency is not bounded by buffer size.

Extend the shared descriptor walker for root-controlled aliases such as `/var` and `/var/run`. Follow a link only from a pinned, root-owned, non-writable, ACL-checked parent and require root link ownership. Read with `readlinkat`, verify unchanged named identity, then walk target components from `/` or retained parent descriptors. Resolve `..` through the verified descriptor stack. Reapply directory checks to every target component; final data files remain no-follow. Bound expansion to 40 links, 256 components and 4096 target bytes; reject loops, races and untrusted links. Avoid generic canonicalization. Prepared selection probes must use this same trusted walk, not a preceding unchecked pathname open.

## Component acceptance and rollback

1. Qualify missing-metadata paged, flat and empty stores; already-prepared restart; unsupported metadata; offline preparation; wrong identity; explicit caps; and pressure/cancellation at every publication boundary. Verify no premature readiness and no repeated republish or complete paged re-encode.
2. Test per-user/system/custom accounts, numeric configuration, standard aliases, hostile links/ACLs, loop bounds and replacement races on macOS/Linux/FreeBSD. Repeat held-old-view collection and fresh-client first-read measurements through ordinary entry points.
3. Deploy v2-capable readers before the writer default changes. Pin explicit flat/full compatibility until that rollout completes. Reader rollback can select full validation of v2; changing writer configuration to flat is not a reverse migration. Downgrading to a flat-only binary requires a separately qualified export/rebuild or compatible backup, never editing `selected` or dropping live leases.

GO requires these checks and a measured startup allowance. Shared process source contexts remain a separate follow-up. No transport, shared-library or storage-format change belongs to this component.
