# dense-pages: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Complete native full/snapshot/segment build composition and actual 100K/5M output against the corrected model.

Adopted `phase/dense-pages` at Detent:c4fe881; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Finish composed targeted baseline and strengthened native model/hash checks.
2. Complete full-build and accounting regression jobs; extract and reproduce 5M corpus.
3. Report exact page/pack/metadata hashing and memory. Coordinate final measurements with the lead.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Codec/packer fixtures and actual 100K full build: 3,245 pages, 6,209,298 page bytes, six packs totaling 6,217,662 bytes. Historical scoped evidence only; final composed jobs still required.

Historical ledger: `docs/status/dense-pages.md` on `phase/dense-pages`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## Full-builder resumption (2026-11-05)

Reconciled Detent:7ea40e5 and a clean worktree. Peer checkpoints are composed.
The complete 5M corpus and model packs remain available in the parked scratch
worktrees; no extraction is needed. The corrected model expects 162,367 pages,
315,432,480 page bytes, 302 packs and 315,759,038 pack bytes, root
`67c70ad305faa8db45b77749073a144c8e12759ca52bce290f5d7633a3ab7f07`.

Native full-builder proof now checks all three exact hash categories, each
external placement after model-to-native segment-id substitution, canonical
padding, the final root occurrence and model pack count. Its default fixture
includes truly empty admitted input. Reader fixture linkage now includes the
builder, and disk-fixture ownership declares the current format, as requested
by the reader task and approved by the lead.

Baseline job: `261105-150551-bfb8c9` at Detent:7ea40e5 (running). The first
submission `261105-150515-44cd1a` was cancelled: campaign resolved literal HEAD
against the home checkout, so it named main and is invalid candidate evidence.
Until that tool issue is fixed, submit the explicit `git rev-parse HEAD` result.
