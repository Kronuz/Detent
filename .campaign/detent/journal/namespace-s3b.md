# namespace-s3b: journal

State: waiting on jobs 261111-043027-eeba12 261111-043027-41364c 261111-043027-8547c1 261111-040648-91a17b 261111-043117-07d3c7 261111-043130-cdf29b
Quest: .campaign/detent/quests/namespace-s3b.md (the lead's; never edit it)

## Doing

Waiting on the gates at `1e2ea54` (native running, asan and tsan queued), two mutants
(k4-scrub-on-owner, k5-reset-keeps-pending, running), the exclusive q2pub A/B (queued for the
host) and CI run 43020922333 (all green but macos-x86_64, still running). 24 of 26 mutants at
`1e2ea54` are killed (exit 0), m9 included.

## Next

1. Read the gate, mutant and q2pub results; paste `campaign brief`'s table; fill the ledger's
   q2pub numbers; set State to "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| Gates at 1e2ea54 (A1-A4, A8-A12, A14) | native 261111-043027-eeba12, asan 261111-043027-41364c, tsan 261111-043027-8547c1 |
| A5, A13 mutants (job passes iff killed) | m1-scrub-no-trigger 261111-043035-1c50d1; m2-first-touch-no-trigger 261111-043037-1283fc; m3-repair-intent-ignored 261111-043040-67ea55; m4a-snapshot-admission-uses-prior 261111-043042-8c5ad6; m4b-snapshot-capture-uses-prior 261111-043044-5beecf; m5-collector-refuses-damaged 261111-043046-19869a; m6-scrub-hashes-mapping 261111-043048-3e4cbb; m7-scrub-keeps-verified-bits 261111-043051-353ca6; m8-sink-selected-not-repaired 261111-043053-edb02c; m9-every-report-wakes 261111-043055-08888c; m10-stage-in-place-repair 261111-043057-f6eafc; m12-no-forced-replace 261111-043059-1948b6; k1-no-conservative-fallback 261111-043101-6ae7eb; k1-conservative-marks-nothing 261111-043104-860cb6; k1-no-automatic-retry 261111-043106-1b7fa1; k1-close-keeps-deadline 261111-043108-fc0915; k1-constant-backoff 261111-043110-03f599; k2-no-exception-translation 261111-043112-5f9ecf; k4-obsolete-report-kept 261111-043114-d0448d; k4-scrub-on-owner 261111-043117-07d3c7; k4-close-skips-scrub-wait 261111-043119-c6b02e; k4-directory-no-retry 261111-043121-afe029; k4-open-no-retry 261111-043123-8eede4; k4-read-no-retry 261111-043125-8e6e56; k4-short-read-no-retry 261111-043128-40b677; k5-reset-keeps-pending 261111-043130-cdf29b |
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
