# namespace-s3b: journal

State: ready for review
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
| A1 | 261111-051250-4bc9ac; fails without: 261111-040321-691e64, 261111-040334-2d05d2, 261111-040350-2b8dbd, 261111-043035-1c50d1, 261111-043046-19869a, 261111-043055-08888c, 261111-043059-1948b6 |
| A2 | 261111-051250-4bc9ac; fails without: 261111-040323-ea4bb5, 261111-043037-1283fc |
| A3 | 261111-051250-4bc9ac; fails without: 261111-040326-143d93, 261111-040342-1d49fe, 261111-040347-1c7ab1, 261111-043040-67ea55, 261111-043053-edb02c, 261111-043057-f6eafc |
| A4 | 261111-051250-4bc9ac |
| A5 | 261111-051250-4bc9ac; fails without: 261111-040321-691e64, 261111-040323-ea4bb5, 261111-040326-143d93, 261111-040329-08b129, 261111-040331-d34469, 261111-040334-2d05d2, 261111-040337-0c1e48, 261111-040339-25460b, 261111-040342-1d49fe, 261111-040347-1c7ab1, 261111-040350-2b8dbd, 261111-043035-1c50d1, 261111-043037-1283fc, 261111-043040-67ea55, 261111-043042-8c5ad6, 261111-043044-5beecf, 261111-043046-19869a, 261111-043048-3e4cbb, 261111-043051-353ca6, 261111-043053-edb02c, 261111-043055-08888c, 261111-043057-f6eafc, 261111-043059-1948b6 |
| A6 | `docs/status/namespace-s3b.md` ("Merge onto Dense pages") |
| A7 | `docs/mechanisms/trusted-restart.md` ("Repair of a damaged pack", "The scrub") |
| A8 | 261111-051250-4bc9ac; fails without: 261111-040353-e75ec9, 261111-040356-c5d8c6, 261111-040359-4a2353, 261111-040402-53fe1b, 261111-040404-294409, 261111-043101-6ae7eb, 261111-043104-860cb6, 261111-043106-1b7fa1, 261111-043108-fc0915, 261111-043110-03f599 |
| A9 | 261111-051250-4bc9ac; fails without: 261111-040407-8381b2, 261111-043112-5f9ecf |
| A10 | 261111-051250-4bc9ac; fails without: 261111-040329-08b129, 261111-040331-d34469, 261111-043042-8c5ad6, 261111-043044-5beecf |
| A11 | 261111-051250-4bc9ac; fails without: 261111-040410-f17c76, 261111-040412-407465, 261111-040415-37efb5, 261111-040418-648763, 261111-040420-34cf6e, 261111-040423-9439e3, 261111-040427-49bd0f, 261111-043114-d0448d, 261111-043117-07d3c7, 261111-043119-c6b02e, 261111-043121-afe029, 261111-043123-8eede4, 261111-043125-8e6e56, 261111-043128-40b677 |
| A12 | 261111-051250-4bc9ac; fails without: 261111-040430-e8f565, 261111-043130-cdf29b, 261111-050346-194d0d |
| A13 | 261111-051250-4bc9ac; fails without: 261111-040329-08b129, 261111-040331-d34469, 261111-040353-e75ec9, 261111-040356-c5d8c6, 261111-040359-4a2353, 261111-040402-53fe1b, 261111-040404-294409, 261111-040407-8381b2, 261111-040410-f17c76, 261111-040412-407465, 261111-040415-37efb5, 261111-040418-648763, 261111-040420-34cf6e, 261111-040423-9439e3, 261111-040427-49bd0f, 261111-040430-e8f565, 261111-043042-8c5ad6, 261111-043044-5beecf, 261111-043101-6ae7eb, 261111-043104-860cb6, 261111-043106-1b7fa1, 261111-043108-fc0915, 261111-043110-03f599, 261111-043112-5f9ecf, 261111-043114-d0448d, 261111-043117-07d3c7, 261111-043119-c6b02e, 261111-043121-afe029, 261111-043123-8eede4, 261111-043125-8e6e56, 261111-043128-40b677, 261111-043130-cdf29b, 261111-050342-d42170, 261111-050344-178b45, 261111-051251-0f7bb2, 261111-051253-5543fa |
| A14 | 261111-051250-4bc9ac |

Not evidence: 261111-052522-6d49d4 failed with "patch does not apply": the reviewer's probe patch adds the delta-clears test that 68c8370 committed, so the suite at 2aff687 (4bc9ac) is D1's confirmation.

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
