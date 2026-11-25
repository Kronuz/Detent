# namespace-followups review: correctness

Commit: Detent:b0a2e4c3aa0ccfbf4692757fb8167e431db45f2a
Verdict: APPROVE
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1, A2: 261117-010638-a2e07b (history tests at b0a2e4c3: power_cut, layout, cluster, large_base,
  transport) passed; mutants no_bounds 261117-010643-82f97f, seal_unbounded 261117-010647-bddad5 and
  no_count_cut 261117-010651-144dd2 killed at b0a2e4c3 (exit 134, asserts live in that build).
- A3: 261117-012018-6adf36 (grep, relay build, test_relay_service with its `disk_*` refusal) passed at
  95da4ba6, whose code is b0a2e4c3.
- A4: 261117-012018-21ab2b (test_relay_mirror and the crash model) passed; stage-poisons-adopt
  261117-012018-251683 and stage-poisons-repair 261117-012019-5f6244 killed, each by the store
  answering "needs a restart" to a later read.

## What I checked myself

Scripts and patches in `.scratch/review-namespace-followups-correctness/` (home checkout); rerun one
with `campaign job submit --quest namespace-followups -- bash <that dir>/probe.sh NAME`.

| Probe | Mutant | Result |
|---|---|---|
| 261117-012638-2ca5d9 | a1-no-table: drop the table bound check | survived (exit 0) |
| 261117-012644-d5c583 | a1-no-outputs: drop the block/base bound check | killed (134), by `seal namespace count`, not the bound |
| 261117-012650-98465f | a1-swap-kind: check a base against the block bound and back | killed (134), by `seal namespace count`, not the bound |
| 261117-012654-4c5b67 | a4-repair-leaks: the repair's staging catch keeps its temporary | killed at test_relay_mirror.cc:356 (listing) |
| 261117-012659-5596bb | a4-adopt-stage-leaks: an adoption's staging failure skips cleanup | killed at test_relay_mirror.cc:343 (listing) |

Answers to the lead's questions:

1. A1: `History::seal` computes the table bytes, then `bounded` checks the seal file, every
   `step.written` output (base or block, by its `Output`) and the table before `kept` or `queue`
   (history.cc:557-575). `Layout::seal` only reads through `Files` (history_layout.hh:15-19), so the
   check sits before the first write. `previous` is `layout_.table().policy`, the in-memory table that
   `Layout::apply` advances at every seal (history_layout.cc:292, 299), which matches the admission check
   in state.cc:1887. The restore's replay (history.cc:476-482) is not checked; it recomputes seals that
   a writer already checked (`require(step.seal == recorded)`), and fetched files are bounded at fetch.
   No write path is missing.
2. A2: `cut_every` cuts at every operation of the uninterrupted run (1..`operations`) at checkpoint
   cadences 1 and 3, recovers, replays and compares the whole directory with the uninterrupted bytes;
   `restarted` covers a restart from the last seal and from the start. The Growth log takes 8 count
   cuts and gets 1640 power cuts (log of 261117-012638-2ca5d9). It compares bytes after replay to the
   end, the same standard as the existing `cuts` pass.
3. A3: no `disk_*` field, `CompletionScheduler` or `completion_port` remains in the relay
   (`git grep` at b0a2e4c3). The only doc still naming `disk_*` health fields is the dated audit
   docs/dev/docs-site-audit.md:677. See N3 for the scheduler class now left without a user.
4. A4: staging errors remove their temporaries and rethrow without poisoning, in adopt
   (mirror.cc:227-237, `clean`) and in repair (mirror.cc:316-324). `PosixIO::remove` ignores ENOENT,
   so a failed `create_exclusive` (name set, no file) cleans safely. Poisoning starts at the link loop
   (adopt) or the rename (repair). Both leak probes are killed by the test's listing checks.
5. Both runners pass through the test's own status on a kill, return 0 when the mutant survives and
   126 when there is no verdict (mirror_mutant.sh:14-36, service_mutant.sh:10-31,
   test_history_mutant.sh:12-24). The evidence jobs above show kills recorded as fails-without passes.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| N1 | no | gap | The table bound check has no test: removing it survives the suite. The output check and the base/block choice are killed only by accident: the tight-limits case then fails decoding (`seal namespace count`), not on the bound. The code is right by inspection (history.cc:508-515). | 261117-012638-2ca5d9 survived; 261117-012644-d5c583, 261117-012650-98465f abort on `std::invalid_argument` | Make the tight case assert that the refusal names a base file. Add a `refused_seal` case where only the table bound trips, if limits can reach it; otherwise record that the table check is defensive. Rerun the three a1 probes. |
| N2 | no | nit | `bounded` runs `find_if` over the outputs once per written file, O(n^2) per seal. With 4096 namespaces, seal 0 does about 8M name compares. `step.written` and `step.seal.outputs` are sorted by the same names (history_layout.cc:296-298), so `written[i]` is `outputs[i]`. | history.cc:509-513 | Index both arrays together, or use a binary search. |
| N3 | no | gap | `CompletionScheduler`, `CompletionPort`, `CoordinatorOptions::completion_port` and test_completion_scheduler no longer have a production user. The relay was the last one; coordinator.cc:128 only checks the option. | `git grep CompletionScheduler b0a2e4c3 -- src include`: only its own files and coordinator.cc | Owner's call: delete them in a follow-up (greenfield, one mechanism) or keep them for a planned user. |
| N4 | no | nit | docs/dev/docs-site-audit.md:677 still lists `disk_*` among the relay health fields; relay_health_check.sh excludes the file. | docs/dev/docs-site-audit.md:677 | Leave it as a dated audit, or add a line saying D43 removed the fields. |
| N5 | no | nit | An ENOSPC at the first `linkat`, before any link succeeds, still poisons the store and leaves the staged temporaries until restart. This is conservative and matches "from the first link". | mirror.cc:241-250, 287-289 | None needed. Optionally treat a failure before the first successful link like a staging error. |
| N6 | no | nit | `clean()` and the repair's `remove` run inside `catch`. If a remove fails, its error replaces the staging error and the remaining temporaries stay until the next restart. | mirror.cc:219-224, 321-323 | Ignore remove errors during cleanup (log them) and rethrow the original error. |
| N7 | no | nit | Seven other mutant runners still exit 0 on a kill (one_knob, agent_store, history_mutant, hash, repair, dense and bindings `_mutant.sh`), so two conventions now coexist. They are outside this quest's file set. | `grep exit tests/**/*_mutant.sh` | Convert them in a follow-up so every runner uses the fails-without convention. |

## Not run

The full native suite and sanitizers (CI's, running at bf324a85). Relay end-to-end behavior under a real
full disk (the test uses RLIMIT_FSIZE). Whether a table-only bound trip can be reached with consistent
limits (N1). The pre-existing test_delivery_repair_scrub-worker failure and the snapshot_catchup flake,
which the journal attributes to main and ci-test-flakes.
