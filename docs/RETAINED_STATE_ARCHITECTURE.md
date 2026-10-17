# Validated Architectural Blueprint: retained-state resources

**Status:** Source design accepted; implementation and runtime qualification pending. This is the remaining consensus resource gate in [library adoption](LIBRARY_ADOPTION_ARCHITECTURE.md).

Lowering operating limits must preserve valid historical state while keeping recovery, live admission, checkpoint funding and delivery inside one explicit resource envelope. Consensus uses one implementation and unchanged storage/wire bytes. Local corrections `c2cbf2e` and `ef30b4f` cover shared checkpoint-entry validation and delivery subtraction; they do not close this component.

## Policy and validation

Add `RetainedStatePolicy` with independent command-byte, log-byte/count and batch-byte/count bounds, defaulting to the existing library ceilings. Append it to `WorkerLimits` and propagate it through Core, Recovery, admission and checkpoint codecs using defaulted arguments. Defaults authorize bounded historical state without allocating or reserving their maxima.

Put pure resource validation in `consensus/retained_state.h`. It may use membership codecs, but includes no Store, Journal I/O or storage-admission implementation. Share metadata, entry-shape, kind-aware payload and aggregate checks. Core retains historical group-limit interpretation by entry position; decoders enforce structural and resource validity. Configuration entries use their membership encoding bound.

Reject recovered counts and metadata before Core creates retained-log nodes. Validate borrowed payload lengths before decoding into owning strings or growing containers. Replay uses one parser and checks replacement position and final aggregate capacity before allocation. Incremental encoding keeps its validation cursor and record/byte budgets, with no constructor suffix scan.

## Live state and delivery

Apply the policy to proposals, membership entries, election NoOps, follower replacements and installation/capture transitions. Bound merged follower requests and their container count with checked arithmetic; empty requests cannot bypass admission. Store recovery uses the retained batch envelope, while new traffic keeps operating RPC limits.

Active retained state stays within policy. Preserve the existing temporary allowance for one bounded replacement batch alongside detached retirement debt; repeated merged requests cannot accumulate extra allowances. Worker output accounting must cover permitted historical entries without preallocating the maxima. An oversized historical entry is delivered alone.

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

Journal adds `Store::grow_replacement(id, minimum_bundle_cap)`, backed by one admission operation. It preserves the replacement identity and ticket, performs no I/O, and allows growth after settled application preparation. It rejects foreign/stale IDs, pending preparation quanta and growth after bundle preparation starts. Compute all accounting changes before mutation. Refusal leaves the existing reservation, candidate and counters unchanged.

This closes the race where the retained suffix grows while application preparation runs. Funding failure or a backend artifact bound produces pressure before freezing, while protocol progress and cancellation remain available. Do not reserve the roughly 17 GB ceiling for every checkpoint. Arbitrarily undersized storage quotas cannot guarantee checkpoint completion.

Detent retains its configured `maximum_size(o.consensus)` funding check and supplies one stable funded retained-state envelope after qualifying library defaults. This restores an allocation/reservation contract; it does not establish a whole-process RSS ceiling.

## Qualification

1. Lower operating command, RPC, log-byte and entry limits independently; replay, construct Core, deliver, checkpoint through Worker and reopen with unchanged receipts.
2. Reject out-of-policy descriptors, counts and payloads before corresponding library allocation. Invalid typed Core input creates no retained-log nodes.
3. Grow history during application preparation, including membership changes; prove funding precedes freeze and covers emitted bytes.
4. Refuse growth under pressure without fencing or losing the candidate; retry and cancellation conserve accounting.
5. Replace a full log while retirement debt exists, including merged follower traffic; prove bounded debt and eventual admission.
6. Pass configuration payloads above operating command limits through transport, admission, replay and checkpoints.
7. Preserve old fixture bytes, acknowledged prefixes, original-I/O settlement and shutdown conservation. Add resource-bypass and capture-before-funding mutants.
8. Run full library/application suites, compatibility, decoder/fuzz, sanitizer and platform checks. Measure this component separately from dependency-only comparisons.

Keep the named-file backend and existing transport mechanisms. Land the policy, reservation growth, Worker lifecycle and Detent integration as one coherent component before accepting dependency group F.
