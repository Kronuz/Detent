# namespace-s3b: journal

State: waiting on jobs 261111-051250-4bc9ac 261111-051251-0f7bb2 261111-051253-5543fa 261111-040648-91a17b
Quest: .campaign/detent/quests/namespace-s3b.md (the lead's; never edit it)

## Doing

Durability review D1 (blocking) fixed at `e7bcd9e`, tests at `68c8370`:
- `bind_base` clears `repair.pending` only when the confirmed plan is a snapshot
  (`PagedCandidateResult::snapshot`, from `PagedPlan::snapshot()`); a delta keeps its packs.
- `session_forced` resets once an offer on the session is published, so a later repair reopens
  a forced session.
- Tests: `test_delivery_repair_delta-clears` (the reviewer's probe, now also requiring completion
  and collection of the shared packs) and `forced-again` (second repair on a forced session; the
  scripted server can offer the delta on a forced session, `Server::forced_delta`).
- Mutants: `s3b-d1-delta-clears-pending`, `s3b-d1-forced-session-kept`; k5's patch follows the
  changed `bind_base` line.
N1-N5: left for the lead's follow-ups.

Jobs: probe at the fix commit `e7bcd9e` 261111-050227-374646 (exit 0: requested 1, completed 1,
pending 0, forced 1, deltas 1). k5 at `68c8370` 261111-050346-194d0d (killed). At `68c8370` the
suite 261111-050334-9be29c failed only `forced-again` line 984: it read `sink->fenced` after
close, which fences every sink; the d1 mutants there were killed at the completion waits (lines
923, 974), before that line. `2aff687` reads the fence inside the phase; suite 261111-051250-4bc9ac
and d1 mutants 261111-051251-0f7bb2, 261111-051253-5543fa rerun there. q2pub 261111-040648-91a17b
is still queued (exclusive).

## Next

1. Read the jobs; paste `campaign brief`'s table; add q2pub numbers if done; State "ready for
   review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| Gates at 1e2ea54 (A1-A4, A8-A12, A14) | native 261111-051250-4bc9ac, asan 261111-043027-41364c, tsan 261111-043027-8547c1 |
| A5 | suite 261111-051250-4bc9ac; m1-scrub-no-trigger 261111-052503-3b052c; m2-first-touch-no-trigger 261111-052503-91b8da; m3-repair-intent-ignored 261111-052503-69675d; m4a-snapshot-admission-uses-prior 261111-052503-84a894; m4b-snapshot-capture-uses-prior 261111-052503-5266ce; m5-collector-refuses-damaged 261111-052503-05c43f; m6-scrub-hashes-mapping 261111-052503-e8ea65; m7-scrub-keeps-verified-bits 261111-052504-897cd0; m8-sink-selected-not-repaired 261111-052504-550d04; m9-every-report-wakes 261111-052504-d394b6; m10-stage-in-place-repair 261111-052504-d41803; m12-no-forced-replace 261111-052504-6e3b62; k1-no-conservative-fallback 261111-052504-60cdbd; k1-conservative-marks-nothing 261111-052505-defe70; k1-no-automatic-retry 261111-052505-11f4ba; k1-close-keeps-deadline 261111-052505-145d31; k1-constant-backoff 261111-052505-e9aa10; k2-no-exception-translation 261111-052505-9e346a; k4-obsolete-report-kept 261111-052505-c78c66; k4-scrub-on-owner 261111-052505-c502f5; k4-close-skips-scrub-wait 261111-052506-dbb0c3; k4-directory-no-retry 261111-052506-a4a507; k4-open-no-retry 261111-052506-e16648; k4-read-no-retry 261111-052506-7b11ea; k4-short-read-no-retry 261111-052506-32bccc; k5-reset-keeps-pending 261111-052507-135749 |
| A13 | suite 261111-051250-4bc9ac; m1-scrub-no-trigger 261111-052503-3b052c; m2-first-touch-no-trigger 261111-052503-91b8da; m3-repair-intent-ignored 261111-052503-69675d; m4a-snapshot-admission-uses-prior 261111-052503-84a894; m4b-snapshot-capture-uses-prior 261111-052503-5266ce; m5-collector-refuses-damaged 261111-052503-05c43f; m6-scrub-hashes-mapping 261111-052503-e8ea65; m7-scrub-keeps-verified-bits 261111-052504-897cd0; m8-sink-selected-not-repaired 261111-052504-550d04; m9-every-report-wakes 261111-052504-d394b6; m10-stage-in-place-repair 261111-052504-d41803; m12-no-forced-replace 261111-052504-6e3b62; k1-no-conservative-fallback 261111-052504-60cdbd; k1-conservative-marks-nothing 261111-052505-defe70; k1-no-automatic-retry 261111-052505-11f4ba; k1-close-keeps-deadline 261111-052505-145d31; k1-constant-backoff 261111-052505-e9aa10; k2-no-exception-translation 261111-052505-9e346a; k4-obsolete-report-kept 261111-052505-c78c66; k4-scrub-on-owner 261111-052505-c502f5; k4-close-skips-scrub-wait 261111-052506-dbb0c3; k4-directory-no-retry 261111-052506-a4a507; k4-open-no-retry 261111-052506-e16648; k4-read-no-retry 261111-052506-7b11ea; k4-short-read-no-retry 261111-052506-32bccc; k5-reset-keeps-pending 261111-052507-135749; d1-delta-clears-pending 261111-051251-0f7bb2; d1-forced-session-kept 261111-051253-5543fa; D1's probe is the committed delta-clears test |
| A10 q2pub (exclusive, cold head vs warm m4a+m4b) | build 261111-040646-93e04c, run 261111-040648-91a17b |
| A6 [artifact] | `docs/status/namespace-s3b.md` ("Merge onto Dense pages") |
| A7 [artifact] | `docs/mechanisms/trusted-restart.md` ("Repair of a damaged pack", "The scrub") |

## Result

<pending>

## Rulings I made

- Merge of main (2026-11-11), onto Dense pages:
  - `paged_read_state.cc`: took main's `lookup` whole. `paged::NodeAt` bounds every field it
    decodes and the walk maps any exception to `ReadSourceFailure`, which is what this branch's
    header bounds (`prefix`, `children`, `body` against the page length, v2 offsets 16-28) did on
    DTPAGE02. Those checks are dropped as Dense-provided; K2's remaining half, the broad
    `std::exception` to `SourceFailure` mapping in `ReadView::try_get` and `try_eval`
    (`view.cc`), still applies (definition decoding and evaluation run outside `lookup`) and
    stays.
  - `test_trusted_restart mapped` ported to DTPAGE03: it finds a verified page by its magic, walks
    its nodes with `NodeAt`, and rewrites a branch's child count, a leaf's body length, or a
    definition body's kind and override bytes under the mapping.
  - Pack-count floors (`>= 10`) lowered to main's `>= 3`: a Dense 100K store has fewer packs.
  - `test_paged_segment_collection`: main's per-variant account and drain, with this branch's
    variants 14-16 and the damaged-pack sweep of variant 11.
  - `VerifiedPages::clear` kept on main's nibble encoding (zeroing a word clears every start code).
- Earlier round at 941e06f: 25 of 26 mutants killed; m9 (every report wakes) survived because
  on Dense the forced Replace finishes between two scrub reports. `heal` now checks one forced
  session; locally the mutant then fails heal every run. `heal` also waits for the watcher's
  first round (a Dense repair can land before it; it failed at line 681 under load).
- The fix round's syscall-fault probe and its 17 mutant scripts lived in a deleted worktree's
  `.scratch`. The probe is now a committed test, `test_testing_pack_scrub_faults` (production
  `pack_scrub.cc` compiled against failing `open`, `openat` and `pread` wrappers, eight cases plus
  real `RLIMIT_NOFILE` exhaustion), and every mutant is a committed patch run as a job. The read
  bounds mutant is dropped with the code it mutated (Dense-provided).
