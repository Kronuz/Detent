# Namespace S6 cutover: the authority answers W5/W6 from history

The authority's half of S6 in [namespace delivery](../design/namespace-delivery.md) (W5, W6, W7, W13). Removing version 1, poll mode and the old cursor is namespace-s6-v1-removal (D93). A version-2 session (`/v2/distribution/session/{open,next,close}`) is answered on the owner from S5.2's history: per namespace, the tier the store's cursor falls in, chosen by the cut. Code: `src/detent_authority/namespace_history.{hh,cc}`, `frames_export.{hh,cc}`, `namespace_sessions.{hh,cc}`, and the version-2 path in `service.cc`.

## A namespace's history on a voter

`AuthorityHistory::freeze` takes one namespace's D6 history at the latest seal and implements `distribution::NamespaceHistory`:

```text
base [0, p] | blocks, then level-0 sections (p, s] | ring entries (s, h]
```

- The cut p, the blocks and the sections come from the sealed layout table (`history()->layout().table()`), read with `history::read_tier`. A namespace with no table entry for its incarnation (created or re-created since the last period seal) has p = s = its origin and no spans.
- The full copy is the sealed state's records for the namespace, which equal the merged tiers (`test_authority_namespace_history` checks it against `Layout::answer`).
- The ring entries come from `ChangeHistory`, trimmed to the positions contiguous from s + 1. A capture gap holds the head back to the contiguous end (`held_back()`); the session answers up to it and the next seal covers the rest (S5.2 review F5).
- Freeze returns nothing when positions are not enabled, the table is not the sealed state's, or the namespace is absent or dropped.

## Which answer (D1)

`select_answer` picks the tier. A same-cluster store above the head is Ahead and gets no frame, whatever its incarnation: positions continue across incarnations, so a voter whose apply lags a drop and re-creation sees a store at the new incarnation above its head, and must not send it the dropped incarnation. Another incarnation at or below the head, another cluster, or no store gets a full copy (W7).

## One answer

`FramesExport` encodes a session answer (`SessionFramesEncoder`, W6) in bounded steps into 64 KiB chunks the delivery ledger admits, then sends them as DTDIST01 fragments of `UpdateKind::Frames`, since a fragment header carries the body's total length. An answer past `delivery.bytes` fails with 503. A next whose namespaces are all current and whose directory has not moved waits for a change until `wait_ms`, then answers 204.

Response headers: `Content-Type: application/vnd.detent.frames`, `X-Detent-Session-Version: 2`, `X-Detent-Session`, `X-Detent-Coding: raw|zstd`.

## Sessions

`NamespaceSessions` holds each session's principal (the SHA-256 of the credential's principal, and its policy epoch) and coding, at most one next in flight, and an idle and maximum lifetime. Every next carries its own cursors, so a session holds no position. Its maps are allocated from the delivery ledger. Results map to statuses: unknown or expired 410, another principal 403, a changed policy epoch 409, a next in flight 409 `session_busy`, the table full 503.

## Grants (W13)

A credential's `namespaces` are W13 patterns (an exact name, a whole-component prefix `p.*`, or `*`) built into `NamespaceGrants`. Version-2 sessions, `/v2/transactions` and `/v2/read` check `grants.allows(name)`. A session with `all` gets the directory entries its grants allow, and a full copy of each live granted namespace it does not follow, whenever the directory moves.

## Positions

The enabling entry (D88) is proposed by the leader the first time a version-2 next needs positions, at most once a second.

## Evidence

- `test_authority_namespace_history`: three voters, five namespaces through seals, roll-ups, cuts, a capture gap, a drop and a re-creation. Every cursor (none, another incarnation, each R from 0 to h + 1) answers byte for byte the same on every voter, in the tier the cut chooses, with the records S5.2's layout answers, and applying it yields the store at the head. W7: a store of the dropped incarnation gets a full copy of the new one under the new cursor.
- `test_authority_frames_service`: the HTTP endpoint over TLS against an oracle built from the voter's own `layout` table and files on disk plus the producer's writes. It checks every cursor's body byte for byte, raw and zstd, plus W13 grants (`all` under a narrow grant brings only granted names), and the session statuses.
- `test_authority_lagging_incarnation`: a voter cut off before a drop and re-creation answers a store at the new incarnation with Ahead and no frame.
- `test_distribution_session_frames` (`other_incarnation`): another incarnation is a full copy at or below the head and Ahead above it; another cluster is always a full copy.
