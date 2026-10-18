# Validated Architectural Blueprint: retained-state resources

**Status:** Source design accepted; implementation and runtime qualification pending. This is the remaining consensus resource gate in [library adoption](LIBRARY_ADOPTION_ARCHITECTURE.md).

Lowering operating limits must preserve valid historical state while keeping recovery, live admission, checkpoint funding and delivery inside one explicit resource envelope. Consensus uses one implementation and unchanged storage/wire bytes. Local corrections `c2cbf2e` and `ef30b4f` cover shared checkpoint-entry validation and delivery subtraction; they do not close this component.

## Shared ownership and integration

The October 18 architecture reconciliation assigns SYS-12 to this existing component. Continue the canonical resource implementation in the isolated `consensus-fix` checkout; the Journal maintainer integrates its paired `journal-fix` reservation growth. Track C consumes these helpers instead of introducing another recovery-bound API. The [shared decisions](SHARED_LIBRARY_DECISIONS.md) and adoption ledger record this ownership; Track C's work ledger still needs the corresponding assignment.

Track C owns membership-transition semantics and transport negotiation. Resource and structural checks precede historical membership checks, which precede mutation or activation. Keep transport identity and quorum logic outside the resource cursor. Membership recovery must preserve the existing historical fallback in `recovered_limits(index)` when no explicit group limits exist. Reject invalid follower transitions before mutating the log. Add combined tests for lowered operating limits, membership changes and checkpoint funding before integration.

This policy bounds one Worker's retained state. Future node-wide pools for many groups reuse these boundaries and admission mechanisms; this component does not establish a node-wide budget. Finalized version 2 reports peer limits, so Detent qualification must verify equal-limit fixed groups with those reports present. Continued absence of peer-limit reports is not a compatibility guarantee.

## Policy and validation

Add `RetainedStatePolicy` with independent command-byte, log-byte/count and batch-byte/count bounds, defaulting to the existing library ceilings. Append it to `WorkerLimits` and propagate it through Core, Recovery, admission and checkpoint codecs using defaulted arguments. Defaults authorize bounded historical state without allocating or reserving their maxima.

Put pure resource validation in `consensus/retained_state.h`. It may use membership codecs, but includes no Store, Journal I/O or storage-admission implementation. Share metadata, entry-shape, kind-aware payload and aggregate checks. Core retains historical group-limit interpretation by entry position; decoders enforce structural and resource validity. Configuration entries use their membership encoding bound.

Reject recovered counts and metadata before Core creates retained-log nodes. Validate borrowed payload lengths before decoding into owning strings or growing containers. Replay uses one parser and checks replacement position and final aggregate capacity before allocation. Incremental encoding keeps its validation cursor and record/byte budgets, with no constructor suffix scan.

## Live state and delivery

Apply the policy to proposals, membership entries, election NoOps, follower replacements and installation/capture transitions. Bound merged follower requests and their container count with checked arithmetic; empty requests cannot bypass admission. Store recovery uses the retained batch envelope, while new traffic keeps operating RPC limits.

Active retained state stays within policy. Preserve the existing temporary allowance for one bounded replacement batch alongside detached retirement debt; repeated merged requests cannot accumulate extra allowances. Worker output accounting must cover permitted historical entries without preallocating the maxima. An oversized historical entry is delivered alone.

Retirement must also make progress when a historical command or configuration exceeds today's command-byte budget. One first oversized item may consume a fresh nonzero byte budget, saturating its remaining bytes to zero. Share that allowance across output containers and retained-log retirement for the whole turn; do not reconstruct it from remaining units and bytes. Zero-byte predecessors consume work units and postpone oversized work to a fresh turn. Explicit zero unit or byte capacity cannot release a positive-sized item. Accepted retained-state bounds still cap each item.

The shared scope above is the terminal `Worker::retirement_turn`, which runs before active-service cleanup. Active `run_one` retains its existing separate cleanup quanta: one upfront and up to two Core steps on completion paths, with at most one additional storage-fault step. Each quantum has its own budget. Account for those quanta when measuring active-turn work; do not claim a single oversized allowance for the entire active service turn.

## Checkpoint funding

Keep the initial checkpoint reservation based on operating capacity. Add `Core::retained_suffix(LogBoundary)`, returning entry count and payload bytes from cached subtree accounting in logarithmic work. Use the same boundary-match rule as installation.

Immediately before capture, compute the encoded bound for the suffix that will survive. Grow the existing reservation if needed, then freeze in the same owner-executor turn:

```
Reserve operating capacity → Prepare application
                                  ↓
Query current suffix → Grow funding → Freeze → Encode → Publish
                           ↓
                      Pressure/retry
```

Core exposes a pure `capture_usage` query for each capture request, sharing rejection predicates with its handlers. A request already owed a rejection requires no replacement growth. The Worker still dispatches that request through Core so a higher-term caught-up installation persists its term before reporting rejection. Invalid local captures and stale-epoch captures likewise reach their correlated rejection under funding pressure. Valid requests retain the grow-before-freeze ordering.

Journal adds `Store::grow_replacement(id, minimum_bundle_cap)`, backed by one admission operation. It preserves the replacement identity and ticket, performs no I/O, and allows growth after settled application preparation. It rejects foreign/stale IDs, pending preparation quanta and growth after bundle preparation starts. Compute all accounting changes before mutation. Refusal leaves the existing reservation, candidate and counters unchanged.

This closes the race where the retained suffix grows while application preparation runs. Funding failure or a backend artifact bound produces pressure before freezing, while protocol progress and cancellation remain available. Do not reserve the roughly 17 GB ceiling for every checkpoint. Arbitrarily undersized storage quotas cannot guarantee checkpoint completion.

Detent retains its configured `maximum_size(o.consensus)` funding check and supplies one stable funded retained-state envelope after qualifying library defaults. This restores an allocation/reservation contract; it does not establish a whole-process RSS ceiling.

## Qualification

1. Lower operating command, RPC, log-byte and entry limits independently; replay, construct Core, deliver, checkpoint through Worker and reopen with unchanged receipts.
2. Reject out-of-policy descriptors, counts and payloads before corresponding library allocation. Invalid typed Core input creates no retained-log nodes.
3. Grow history during application preparation, including membership changes; prove funding precedes freeze and covers emitted bytes.
4. Refuse growth under pressure without fencing or losing the candidate; retry and cancellation conserve accounting. Already rejected captures must finish under the same pressure, including higher-term caught-up installs with durable term advancement.
5. Replace a full log while retirement debt exists, including merged follower traffic; prove bounded debt and eventual admission.
6. Pass configuration payloads above operating command limits through transport, admission, replay and checkpoints.
7. Preserve old fixture bytes, acknowledged prefixes, original-I/O settlement and shutdown conservation. Add resource-bypass and capture-before-funding mutants.
8. Run full library/application suites, compatibility, decoder/fuzz, sanitizer and platform checks. Measure this component separately from dependency-only comparisons.
9. Drain historical commands and configuration entries under lowered operating limits in finite retirement turns. Cover two oversized items, zero-byte predecessors and shared budgets across containers without subtraction underflow or renewed allowances.

Keep the named-file backend and existing transport mechanisms. Land the policy, reservation growth, Worker lifecycle and Detent integration as one coherent component before accepting dependency group F.

## Candidate qualification (2026-10-18)

The final isolated candidate passes 24/24 native tests in 107.39 seconds, 134 focused Worker checks and 285 retained-state checks. Coverage includes each independent operating-limit reduction through replay, delivery, checkpoint publication and reopen; growth during application preparation; invalid and stale-epoch local rejection under pressure; incoming stale-term and caught-up rejection; and a successful incoming image preserving a 12 KiB suffix that requires reservation growth. Configuration payloads larger than operating command limits cross transport, admission, Core persistence, replay and checkpoint round-trip, while malformed membership is rejected.

Negative mutations are caught for bypassed recovered metadata validation, missing funding guards and funding incorrectly required before a correlated incoming rejection. An incoming-only funding mutation initially survived because its 8 KiB fixture fit the initial reservation's conservative metadata allowance. The strengthened 12 KiB fixture catches that mutation. Preserve those results separately. The [qualification record](../benchmarks/retained-state-qualification-20261018.json) retains raw outputs and source archive hashes. Timing is functional qualification under concurrent host activity, not a performance comparison.

Journal growth is committed locally as `cb66f3d4e644ee5f3c3e86cd61a8c83d2b36be62`, with hardened macOS, macOS ASan/UBSan, Linux and FreeBSD preflight passing. Its grown-reservation completion matrix covers pending cancellation, stale completions, detached facades and uncertain original I/O. The independent consensus retirement-progress correction also passes all four environments.

The paired resource implementation has source review GO with no remaining P1/P2 findings. Its platform snapshot predates the final strengthened incoming fixture and Configuration transport seam; final fixture platform qualification remains required. Detent's stable funded envelope, default footprint and dependency closure remain open. A local Journal commit and source override do not establish a published dependency closure or release acceptance.
