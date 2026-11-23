> Status: CURRENT (2026-11-11) Ledger for namespace step S4 (D12.2: no tier re-validates its own build; hash counters). Evidence is the campaign journal's table; this file says what changed and what the numbers are.

# namespace-s4

- Design: `docs/design/namespace-delivery.md` D12.2. Mechanism note: `docs/mechanisms/trusted-restart.md` ("A tier's own build"). Quest: `.campaign/detent/quests/namespace-s4.md` (A1 to A3).
- Branch: `quest/detent/namespace-s4`.

## What re-hashed a tier's own build

A cold bootstrap built a generation and then hashed it twice more, in two places; both are the tier's own build and both are gone:

1. **The plan builder's candidate validation.** `PagedSnapshotPlanBuilder` and the maintenance plan ran `CandidatePagedValidator` (a `GraphValidator`) over the image the encoder had just written, and `SparsePlanFinalizer` ran a `WarmGraphValidator` over a delta's new pages: every pack checksum and every page digest recomputed. The walk stays, because it derives the live counts, reachability and override histogram that `.live` records; the digest comparisons do not (`own_build` on both validators).
2. **The recovery after the publication** (`RecoveryEngine::recover` with the plan's admission): a full discovery and graph validation, which hashed every pack and page a third time. It now opens through `.read` and `.live` like a restart (`TrustedRestart::open`, `TrustedOwn`): the plan's reserved mappings fill the plan's registry, a warm prior's mappings are carried, and the packs just built are left without first-touch bits.

Frame SHA-256 is unchanged (`distribution` framing and the frame encoder), as is every format and wire byte.

## Counters

- `paged::hash_counts()` (pages, packs, metadata: calls and bytes), the new `paged::built_hash_counts()` (page and pack bytes hashed by builds to write their own generations), and `distribution::frame_hash_counts()` (frame SHA-256 bytes, encode and decode).
- `delivery::HashStats` (`include/detent_delivery/hash_stats.hh`) composes them: `built_bytes()`, `checked_bytes()` (everything else the layer hashed: re-validation, first touch, restart), `total_bytes()` (adds frames).
- Exposed per tier (a tier is a process): `CoordinatorStatus::hash`, the relay's `/v1/health` bundle fields `hash_page_bytes`, `hash_pack_bytes`, `hash_metadata_bytes`, `hash_frame_bytes`, `hash_built_bytes`, `hash_checked_bytes`, and the agent's log line after each publication.

## Evidence

Test `tests/detent_delivery/test_hash_once.cc` (ctest `test_delivery_hash_once_*`), over a scripted upstream with zstd frames. Journal rows hold the job ids.

<!-- numbers filled in by the journal's Result -->

## Open

- The relay hashes the frames it serves as well as the ones it receives (raw and zstd encodings), so its total stays above 1.2 times its build's bytes on a Dense store; the layer hashing itself is 1.000 times. Frame SHA-256 stays by D12.2.
