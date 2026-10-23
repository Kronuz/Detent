# Online authority capacity

**Author:** Germán Méndez Bravo

**Date:** 2026-10-23. **Status:** Validated architectural blueprint; shared API design and implementation pending.

Startup capacity configuration is qualified, but changing `application_limits` on an existing group still fails its semantic-profile checks. Online growth must preserve those checks at every historical log position. This component raises active capacity within a common, pre-funded ceiling; it does not change that ceiling online.

## Compatibility and funding

Keep `application_limits` as the original bootstrap profile. An optional versioned capacity contract carries a common ceiling. Absence preserves existing behavior. The stable peer fingerprint binds the contract version, bootstrap profile, ceiling and supported application formats. It never binds the changing active profile.

Validate every voter's local replacement/checkpoint funding against the complete ceiling before startup, using the existing checked arithmetic. Committed application must not depend on a node's transient available memory or a different local funding policy. Keep the journal's synced durability and existing ownership rules unchanged.

Align adoption of the new static peer contract with the already-approved coordinated restart into finalized transport v2. Do not introduce a second flag day. The production DTC1 pin does not support a rolling transition to this contract. Relay, agent and reader capacities remain independently funded settings.

## State and activation

Initially permit increases only to `keys`, `state_bytes` and `image_bytes`. Freeze the other seven limits. Raising the receipt-window size, for example, would invalidate the existing exact retained-receipt rule on recovery and requires a separate design.

Use a versioned capacity command inside the existing opaque consensus command payload. It carries context, contract fingerprint, expected profile epoch, complete expected profile and replacement profile. The first accepted transition activates the feature; later transitions increase capacity. Require authenticated admission of every fixed voter under the new contract before first activation. A quorum or aggregate connection count does not establish this condition.

State owns the active profile, feature version, epoch and bounded last-transition receipt. A transition advances applied index/term without changing knob or namespace revisions. A stale expectation yields a deterministic conflict. Duplicate transition retries use the persisted receipt and current epoch rather than applying another increase.

## Durable format and replay

Persist the activation marker and transition metadata in a new application image version with its own application-format tag. `DTAIMG01` stores limits but cannot distinguish an activated group from a legacy group started at those same limits after compaction. Inference from active limits cannot make a downgrade fail closed. Account for the new fixed header in the image budget.

Legacy images require the bootstrap profile exactly. New images validate their contract, activation state and active profile against trusted configuration before allocating rows. Activation-disabled recovery rejects activated images and committed activation records even if configured limits equal the recovered active limits. Snapshot installation restores profile and contents together.

Preserve ordinary `DTABAT02` bytes and exact profile comparison. Decode each batch against State's active profile at its log position; apply transitions only between entries. A historical `Capacity` receipt remains a failure after an increase. Retrying its original producer sequence returns that original receipt.

## Proposal ownership

In capacity-enabled mode, gate application proposals after election on a successful current-term consensus Read barrier. The current Core can admit proposals before its election NoOp applies; the barrier resolves inherited transitions first. Enforce the gate in both Node proposal APIs.

During a transition, stop ordinary admission and preserve accepted bytes preceding it. Retain queued requests under their existing bounded ownership. Invalidate and rebuild unaccepted encoders, builders, prepared proposals and carry capabilities against the new profile epoch while preserving command identity. A client timeout must not reopen admission before the committed outcome is known.

## Shared library boundary

Current consensus main `61149a2` accepts one `SnapshotLimits.application_format` at recovery, reception and sourcing. Cluster main `f053a82` has the classic UDP substrate, not an alternative durable Worker. Strata's durability `95357d9` and peer-transport `d8384c8` do not supply format activation.

Extend the existing Worker policy with bounded old/new application-format admission and a tag attached to each local checkpoint capture. Preserve descriptor wire bytes and existing defaults. Captures made before activation may finish afterward under their original tag. Recovery, reception and sourcing must permit the admitted old format. Do not fork Worker or put incompatible application bytes behind an old format tag.

Use the existing finalized transport's authenticated per-voter state for activation readiness. A small bounded query may be needed if that state is not public. Agree the concrete additive API and record its consumer qualification before adoption.

## Service boundary

Extend the existing private authority parser and bounded HTTPS request admission. Add `capacity_admin`, default false; publishing permission does not grant capacity administration. A successful response follows committed application. Health/status reports bootstrap, active and ceiling profiles, activation state and the last transition outcome. Request preparation and Node decoding use the active profile; immutable resource envelopes remain startup configuration.

## Component qualification

1. Preserve legacy byte fixtures and default behavior; prove older readers and activation-disabled binaries refuse activated stores before and after compaction.
2. Refuse mixed-capability activation and insufficient voter funding before startup; check overflow, ceiling and frozen-field boundaries.
3. Preserve historical failed receipts; cover stale and duplicate transitions, partially prepared and carried commands, and timeout without premature admission reopening.
4. Kill a leader with an uncommitted transition and validate the next leader's barrier and outcome.
5. Publish a checkpoint captured before activation afterward; exercise old/new snapshot recovery and catch-up.
6. Run a real three-voter transition with relay, agent and SDK verification before qualifying continuous growth through the 1M checkpoint and 5M target.

The shared library extension, application/Node transition engine and service integration are separate reviewable implementation commits. Review the complete component at its boundary, not each commit.

## Limits

This design does not improve the measured ingestion or bootstrap costs, authorize a new operating ceiling, or change reader and relay resource funding. Its implementation follows the current scaling investigation. The current production transport cannot negotiate the new static contract during a rolling upgrade; use the agreed finalized-v2 restart. A new application image is unreadable by activation-disabled binaries, which must refuse it explicitly.
