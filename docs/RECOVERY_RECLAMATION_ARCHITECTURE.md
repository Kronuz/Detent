# Validated architectural blueprint: recovery reclamation pacing

**Date:** 2026-10-24. **Status:** Source review complete; implementation and measured acceptance pending.

Authority image decoding must drain replacement-node retirement work between input slices. This uses the existing state collector and preserves current work budgets, image bytes and publication boundaries. The scheduling experiment remains deferred until memory behavior is understood.

## Evidence and mechanism

`ImageDecoder::step()` processes up to 128 rows and requests collection using newly allocated nodes plus an allowance. `State::collect(size_t)` also imposes a 64 KiB byte budget; map cleanup charges 128 bytes per node, limiting that call to 512 map nodes. Persistent AVL insertion replaces an ancestor path, so one slice can generate more retirement work than collection consumes. This is a source-level pacing mismatch; its effect on measured peak memory still requires a controlled comparison.

A saved 5M-knob trial completes replicated ingestion and authority verification. An isolated recovery sample records a 14.8 GiB voter footprint. A subsequent three-voter restore reaches revision 39064, then its resource guard stops the trial at 21,891,428,352 bytes of aggregate service RSS against a 16,625,211,801-byte ceiling. Footprint and RSS describe different quantities and must remain separate.

Before decoding another row slice, perform one collection slice using `State::collect(CommandWorkBudget&)`. If retirement remains pending, return without consuming more input. Preserve collection after decoded rows. A parsed image becomes finished only after its retirement tail drains. Cleanup-only steps must not request duplicate input or change validation state. Queries after successful `take()` must remain safe.

## Review checks

| Check | Required condition |
| --- | --- |
| Scalability | Decoding cannot repeatedly admit row slices while older replacement work accumulates. Measure heap, footprint, RSS, CPU and wall time across corpus sizes. |
| Edge cases | Preserve fragmented input, incomplete rows, producer receipts, empty images, final input, trailing/truncated rejection, discard and small row budgets. |
| Dependency and state | Reuse existing collection; keep pins, canonical ordering, accepted bytes, application boundaries, receipts and live-root ownership unchanged. |
| Security and scale | Preserve admissions, authorization and fencing. Expected rejection and malformed input must not publish partial state or strand cleanup. |

## Qualification

1. Extend the existing image round-trip fixture to cover the default 128-row slice as well as one-row decoding, with complete canonical byte equality and final retirement checks. Preserve a failing baseline.
2. Measure one decoder process over the saved application image before attempting another three-voter restore. Stream input and canonical output; avoid additional whole-image copies. Compare identical binaries/configurations except for the candidate and record cache conditions. A timed-out baseline is censored evidence, not an exact speedup.
3. Require matching canonical bytes, revision, applied boundary, identities and receipts, plus existing malformed-input and cleanup checks. Run relevant sanitizers and composed CI before landing.
4. Attempt multiple voters only after measured single-process headroom supports the aggregate limit. Diagnose relay/agent resets separately with concrete reason, export phase, ledger occupancy and cursor evidence.

## Limits

This change does not make AVL construction linear or qualify sustained 5M operation. It does not establish that the 128 MiB delivery heap is insufficient: authority export streams frames incrementally. Recovery scheduling and retained startup network actions remain separate hypotheses. No implementation speedup or memory reduction is claimed before measurement. All measurements are development-host evidence; no deployed-service qualification is implied.
