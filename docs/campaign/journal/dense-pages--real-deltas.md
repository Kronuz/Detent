# dense-pages--real-deltas: journal

State: working
Quest: docs/campaign/quests/dense-pages.md (the game master's; never edit it)

## Doing

Close A10/design T4 with the real 5M corpus, the corrected model, and actual incremental writes.
The optional `--corpus BEFORE AFTER` mode preserves the default fast test. It compares every
encoded body through `PagedReadState`, deleted-key misses, the full-rebuild state commitment,
every corresponding-path page digest/length/keys/height, physical occurrence uniqueness,
unchanged extent identity, removal accounting, and exact new pack bytes reconstructed from
the full-build seal order with 8-byte alignment and 1 MiB packs.

## Next

1. Run the synthetic end-to-end smoke at the committed code.
2. Run the explicit one-update and mixed-churn model commands and native oracle on the pinned
   5M input, preserving all raw outputs and fresh after corpora under `$JOB_OUT`.
3. Record exact new pages/page bytes/pack bytes/off-path pages/off-path bytes and root digests.

## Blocked on

Nothing. Machine work queues under light holds; no timing claims are made.

## Evidence

| Criterion | Evidence (job id, test, commit) |
|---|---|
| A10/T4 | Pending committed smoke and 5M jobs. |

Pinned input corpus: `/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-a7d1b230a685025e4/.scratch/dense/corpus-5m.bin`,
SHA-256 `bb87ae5d3a30f666e06bb236f1f826d7afd0ba8d942f9fffcec1db76152bc835`.
Model: `docs/design/dense-pages-model.py`, SHA-256
`96dfd0a7aad2af1f8bd8dead7f85729ddd5e6aeac4ba03ce49ce1b7ea3906bd1`.

The reproducible driver is `bash docs/perf/dense-delta-real-corpus.sh MODE BUILD` (`smoke` or
`5m`). It prints the host, compiler, interpreter, binary/corpus/model digests, exact model and
native commands, and raw outputs. The 5M mode verifies its input digest before execution.

## Rulings I made

1. The corpus mode treats model-mutated definition bodies as opaque encoded bytes. Its value
   proof is exact body lookup, not public typed-value validity: model churn changes the first
   eight body bytes and can invalidate typed-body framing. No production code changes.
2. The corpus-mode ledger has a 4 GiB admission limit. Fixture `std::map`/pack vectors remain
   ordinary test allocations outside that ledger; this is an actual write oracle, not a resource
   accounting benchmark. The fast test retains its existing 256 MiB account.
3. Off-path classification uses changed after-keys plus both old neighbors of each deleted key,
   matching `mutate`/`churn_main` in the pinned model. Counts compare occurrences, not digest sets.
4. Weekly Codex quota was 96% at the first check; only one focused experiment is staged, with
   synthetic smoke gating the full real-corpus run.
