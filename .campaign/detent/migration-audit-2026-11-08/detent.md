# Detent migration audit (checks 0 to 4)

Auditor `detent`, 2026-11-08. Laptop session `remote-c5c46609-detent-ce5f34` (read-only; scratch in
`~/.scratch/audit-detent/` there). Migrated campaign read from `Detent:9d299e0`
(`wip/main-migrated-2026-11-08`, identical to the laptop's `main`).

## Verdict

The migration lost no code: every branch on the laptop is on GitHub at the same commit or inside a
GitHub branch (91 of 92; the one exception is a local backup of the migration's own commits); two
of the three dirty worktrees equal their `wip/*-parked` branches exactly and the third is a job's
build slot. Every ledger (37),
review and design doc is accounted for. The gaps are elsewhere. First, the results of the last
three days' measurements (S1/S2 cold bootstrap, the reader S6 soak, Dense against S1/S2) and the
harness that produced them exist only in the laptop's `.scratch`, while three live quests and the
wake-up note cite them there. Second, the map starts at Codex's 10-23 work: the client spec, Stages
0 to 5 and most of Stage 6's components have no milestone, M1b and M3 to M5 show "no quests yet",
and five history-only milestones (H0 to H4) stand in for the plan's own phases. Third, the four
library-adoption quests dropped the acceptance criteria of their release-blocker briefs. All three
are fixed in `fixes/`, rehearsed end to end on a copy of the migrated `main`: the map then shows 18
milestones in the plans' order, 58 quests, 38 landed, and `campaign check` is clean.

## Counts

| | |
|---|---|
| Laptop branches | 92: 68 equal on GitHub, 23 ancestors of a GitHub branch, 1 local only |
| Worktrees | 66: 63 clean, 3 dirty, all 3 saved exactly on GitHub or job mechanics |
| `.scratch` in Detent | 3,627 entries, 66 GB; 186 distinct `.scratch/` paths cited by committed records |
| Job store | 101 Detent jobs (all `dense-pages`); 75 cited ids, all present; numbers in the committed journal |
| Roadmap | 10 plan layers, 18 milestones; the migration showed 14 rows: 5 history-only (H0-H4), 4 with no quests |
| Prepared | 74 files: PLAN.md, 13 history quests (26 files), 8 planned quests, 9 edited files, 28 result files, 2 scripts |

## Check 0: the roadmap, from the first plan

Every plan the leads followed, oldest first, and where the corrected `PLAN.md` puts it:

| Plan (source) | Phases | Corrected milestone row | Status in the migration |
|---|---|---|---|
| `justknobs-implementation-spec.md` (2026-09-29, the first plan) | Build milestones 1-9 | Spec milestones 1-9 (`client-spec`) | GAP: missing |
| `docs/VALIDATED_ARCHITECTURE.md`, `docs/IMPLEMENTATION.md` (10-02) | Stages 0, 1, 2, 3, 4 (+ its prerequisite), 5A, 5B, 5C | Stage 0 to Stage 5 (8 history quests) | GAP: missing |
| `docs/STAGE6_ARCHITECTURE.md` (10-14), `docs/RELEASE_PLAN.md` gates (10-18) | Stage 6 components 1-6, managed failover, release-gate components | Stage 6: qualification components (to 10-29): 4 new history quests + the 7 of H0 | GAP: 4 missing; H0 a history-only name |
| `docs/RELEASE_PLAN.md` (10-27) | Exits RC-1 to RC-5 | M2 to M5 (the lead's) | partial: M3-M5 "no quests yet" |
| `docs/status/README.md` (10-23 to 10-29) | 15 qualification workstreams | history quests; not-quests 8, 10 | OK, startup capacity now a quest |
| `docs/handoffs/README.md` (10-23) | SC-1 to SC-5 blockers, 7 briefs | history quests; adoption quests; `rc3-sustained-capacity` | GAP: briefs' criteria dropped (fidelity) |
| Lead plan `docs/PLAN.md` (11-02 to 11-04, `aa00b0e`..`7c007ce`) | M1 (W1-W5, W2a), M1b, M2-M5 | M1, M1b, M2-M5 | M1 OK; M1b no quests |
| Owner asks 11-03/04 and the handoff (11-04) | reader S1-S9, namespace S1-S10, legacy waves B-I, guides; landing order 1-5; follow-ups | N1, Dense pages, N2; `reader-s7-s9`, `namespace-s4-s10` | GAP: later steps not quests; landed work under H2-H4 |
| Codex Dense campaign (11-05) | Dense S1-S5 and its tasks | Dense pages | OK |
| `website/src/content/docs/roadmap.md` | post-release feature lists | After the release (no quests yet) | GAP: missing (owner's to schedule) |

Corrected milestone order (rehearsed `campaign map`):
Spec milestones 1-9 → Stage 0 → 1 → 2 → 3 → 4 → 5 → Stage 6 components → M1 → N1 → Dense pages →
N2 → M1b → M2 (RC-1) → M3 (RC-2) → M4 (RC-3, RC-4) → M5 (RC-5) → After the release.

Moves (26, all in `fixes/roadmap-detent.sh`; syntax `campaign set ID 'milestone=NAME'`, checked
against `cmd_set`):

- To `Stage 6: qualification components (to 2026-10-29)`, from H0: reader-acl, relay-snapshot-lifecycle,
  seeding-waits, container-selection, recovery-delivery, prepared-reader, prepared-activation.
- To `M1 On the tip`, from H1: salvage, salvage-followups, retained-state.
- To `N1 Reader core, namespace delivery, legacy removal and guides (handoff order 1-4)`: from H2
  reader-design, reader-s1-s4, reader-s3, hash-once, reader-s5, namespace-design, namespace-s3,
  client-local-source; from H3 legacy-wave2; from H4 actors-docs, how-it-works, quickstart-managed;
  from "N1 Handoff landing" reader-s6, namespace-s3b, namespace-s1-s2, legacy-h.

Placement calls the lead may revise: the Codex-era components stay in a Stage 6 row (the plan's own
"components", before the 10-27 exits) rather than spread over M2-M5; `retained-state` stays with the
salvage that produced it (its remaining exit is RC-1); the 11-03/04 reader, namespace, legacy and
guide work sits with the N1 quests that finish it.

## Coverage (check 1)

| Source item | Where it went | Status |
|---|---|---|
| 37 ledgers on any ref | 25 history quests, 11 live quests, PLAN.md (REPORT table 1, rechecked against every GitHub branch) | OK |
| 39 reviews + 3 Dense reviews | history quests' Results, live quests' lenses | OK |
| 7 design docs, 2 models | the quests that build them | OK |
| `docs/handoffs/` 11 files | 4 cited; the 7 release-blocker briefs cited by no campaign file | GAP 3.1 |
| Plans (above) | corrected PLAN.md | GAP 2.1 |
| Branches: 92 on the laptop | 91 on GitHub or contained; `backup/migration-commits-2026-11-08` (`Detent:11bf09d`) local only | GAP 5.1 |
| Dirty worktrees: `Detent-wt/dense-s2` (mid-merge, 22 paths) | equals `wip/dense-s2-parked` (`Detent:f7bf999`): empty `git diff --stat` | OK |
| `.scratch/dense-t2/wt-base` (1 path) | equals `wip/dense-t2-base-parked` (`Detent:8a6abb0`) | OK |
| `jobs/slots/dense-refetch-baseline` (CMakeLists.txt) | a job's injected fixture target in a build slot, not work | OK |
| Laptop-only branches with work: `phase/dense-pages-{reads,deltas,integration}`, `phase/mechanism-notes`, `review/reader-s6-security` | ancestors of GitHub branches (`quest/dense-pages`, `main`, `reader/s6`) | OK |
| Codex's claude-lead-prompt.md | committed (`Detent:9a78885`, `edf685a`) | OK |
| Unmerged `dense-pages-first-read` (`Detent:6417ed8` syscall trim, findings doc) | on GitHub; REPORT O28 asks the lead to adopt or keep | OK (open) |

## Gaps, by severity

### 1. Evidence outside git (lost-evidence risk)

1.1 **The S1/S2 grouped-sync measurements and their harness live only in `.scratch`.**
`namespace-s1-s2`'s Outcome and Decisions 3 quote the 11-07 numbers (5M bootstrap 999-1,001 s to
545 s, restart 61.9-65.9 s to 0.46-0.97 s, 5,777 to 27 flushes) and cite
`.scratch/overnight/NOTES-r2.md`. The 2026-11-06 r4 screen/A/A/confirm pairs (28 runs, A/A within
0.5% at 5M), the 11-07 `run.sh` pairs (10 runs), the proof-recovery r2 (5 of 5 mutants), section 3's
sanitizers (10/10 ASan and TSan, 7,186 power-cut cases) and the ext4 VM probe at host cache off
(1,445 files: 717.6 ms batched, 4,300-4,438 ms per file) are in no committed file;
`docs/measurements/sync-groups-20261105/` stops at 11-05. The harness that produced them,
`coldboot.py` (SHA-256 518db64e, 13,993 bytes), is untracked in the S1/S2 worktree; `docs/perf/cold-bootstrap-5m.md`
cites a different, older `.scratch/coldboot.py`. A9 reruns the timings as jobs, which needs that
harness. Fix: `fixes/Detent/docs/measurements/sync-groups-20261107/` (13 files, 96 KB: README,
two CSVs, logs, the queue's SUMMARY and notes, the harness as run); `namespace-s1-s2.md` cites it.

1.2 **The reader S6 soak evidence lives only in `.scratch/overnight/done`.** `reader/s6`'s ledger and
`reader-s6` A12 cite it: soak 0/12 alone and 1/24 x4 at `Detent:3d8559a` (the fork-reply timeout,
`x4-1-4.log`), 32/32 at `Detent:b1d281c`, T3 20/20 under ASan/UBSan and under TSan, FIX-2's revert
killed. Fix: `docs/measurements/reader-s6-soak-20261107/` (10 files, 36 KB); `reader-s6.md`
Decisions 4 names it.

1.3 **The Dense versus S1/S2 cold bootstrap (11-07) lives only in `.scratch/dense-cb` (12 GB).**
Cited by `wake-up.md` and, through the migrator's `~/.scratch/preflight-detent/home-last.txt` on
this Mac, by `dense-sync-stack`. Same-run 5M agent selection: Dense 360.8 and 349.9 s, S1/S2 502.0 s,
before 748.6 s; Dense publishes 609 files with 1,219 device flushes, S1/S2 25. Fix:
`docs/measurements/dense-cold-bootstrap-20261107/` (README, `report.csv`, `run.sh`, SUMMARY, merge
preview); both citations repointed.

1.4 Not kept, by choice: the 5M corpora, coldboot run directories (1.8-4 GB each), job outputs
(`261106-113938-0f7177` 2.7 GB, `261106-132438-20983c` 0.9 GB) and ~180 other cited `.scratch` paths
(probe sources, build logs) whose numbers the citing ledger or review already states. The 75 job
ids `dense-pages` cites all exist; their numbers are in its committed journal and `job prune`
keeps named records.

### 2. Missing history and roadmap

2.1 **The map omits the roadmap before 10-23 and after M2.** Missing: the client spec's build
milestones (the first plan, `Detent:f789380`), Stages 0, 1, 2, 3, 4, 5A, 5B, 5C (accepted
`bd2ea89`, `e78dc25`, `5778aeb`, `238bd92`, `58a27e3`, `83b55c5`, `83ca8a7`, `a842c33`), and four
Stage 6 components with no ledger: managed failover (`4c9794f`), the bounded runner (`4cd8a4a`),
libraries A-E (`fbd15d5`), startup runtime capacity (`5721d53`, PLAN not-quests 11). Fix: 13
history quests (criteria from each stage's deliverable and gate in `VALIDATED_ARCHITECTURE.md` and
`IMPLEMENTATION.md`; 9 criteria left open where the record shows no evidence, e.g. the spec's gRPC
source and release readiness, Stage 4's pressure/churn gates), `fixes/history-detent.sh`.

2.2 **Future phases have no quests.** M1b, M3, M4, M5 showed "no quests yet"; M2's CL-2, consensus
transport and HTTP-log items and the designs' later steps (reader S7-S9, namespace S4-S10) were a
"not quests yet" list. Fix: 8 planned quests, criteria copied from the plan rows, RC exits and
design step tables; `needs` set where the plans state them.

2.3 **History-only milestones.** H0 to H4 split landed work from the live quests of the same phase
(H2's reader S1-S5 from N1's S6). Fix: 26 moves above; H0-H4 rows removed.

### 3. Fidelity

3.1 **The adoption quests dropped their briefs' criteria.** `docs/handoffs/{http-clients,bounded-codec,resource-primitives,persistent-map}.md`
(10-23) each carry "Acceptance and delivery" criteria that no quest names, e.g. TLS not mandatory
for plaintext-only users, allocations and connection counts on production paths, acknowledgment
timing unchanged, the persistent map's oracle and allocation-failure tests. Fix: 16 criteria added
(A9-A14, A9-A12, A11-A14, A8-A9), library-side ones `[manual]` with the library's CI named; the
briefs added to each quest's Sources. `segment-storage` and `ingestion-waits` feed
`rc3-sustained-capacity`; `container-safety` is closed by `container-selection`.

3.2 **`post-w2` A1 is closed by a later ledger.** Its ledger leaves the cluster and consensus pins
to Next 1; the journal cites `docs/status/m1-pins.md`, which does not exist at `Detent:43d1ce9`.
Fix: A1 open, pointing at `m1-pins`. The other 24 history journals: rows equal criteria, every
cited path exists at its landing commit, open rows match the ledgers (12 across 5 quests).

3.3 **Citations to non-durable paths.** `namespace-s1-s2` (`.scratch/overnight/NOTES-r2.md`),
`dense-sync-stack` (`~/.scratch/preflight-detent/home-last.txt`, this Mac), `wake-up.md`
(`.scratch/dense-cb/`). Fixed with 1.1-1.3.

### 4. Unrecorded decisions and plans (check 4)

Leads' last state: detent-d5 (`~/.claude/projects/-Users-kronuz-code-Detent/da895235….jsonl`,
63 MB; the `-Kronuz-Detent` directory holds only its memory) last worked on 2026-11-04 17:12 PST,
then sent two-hourly reports (quota-limited 11-05 and 11-06, logged out from 11-08); its final calls ("land S1/S2", "resume or park
Dense") are in `wake-up.md`. Codex (`rollout-2026-11-04T21-20-52`, last 11-07 01:34 UTC) restarted the
A10, A15 and attribution jobs, whose results the Dense journal records. The overseer (`kronuz-e9`,
`43b1ee4e`) finished `run.sh`, ran the Dense comparison and the stack trial for the owner.

4.1 **Owner rulings in the lead's memory, in no ledger.** 2026-11-04: the lead may push and land on
any Kronuz cpp-library repository, cpp-libraries included (`memory/cpp-libraries-push-authority.md`);
2026-11-03: a client pays only for what it is missing, and an owner proposal is rejected only with
numbers (`owner-rules-are-constraints.md`). Fix: PLAN.md D28, D29.

4.2 **The overseer's open item.** "Fold the fixture regeneration into the real stage.sh/coldboot.py":
the 1-day TLS fixture (`tls_certificates.cmake`, `-days 1`) stalled the r2 and r3 runs. Fix:
`namespace-s1-s2.md` Decisions 6.

4.3 OK: `~/.claude/todos` does not exist; no plan files beyond those above.

### 5. Cosmetic

5.1 `backup/migration-commits-2026-11-08` (`Detent:11bf09d`, the migration's 104 per-command
commits) is not on GitHub; its tree differs from `main` only in `MAP.md` and `wake-up.md`, where
`main` is newer. Push it as `wip/` or delete it.

5.2 `wake-up.md` mixed two runs (before 741 s and S1/S2 481 s from 05:36, Dense 361 s from 11:13);
corrected to same-run numbers.

5.3 `dense-pages-first-read:docs/status/dense-first-read.md` cites `sha256:35e8639`, rebased to
`sha256:07cdc83` on sha256's `main` (on GitHub); the AVX2 kernel is not lost.

## Applying

On the laptop, home checkout on `main`: `bash ~/…/fixes/roadmap-detent.sh && bash ~/…/fixes/history-detent.sh`,
then `campaign map --write`. Rehearsed on a clone of `wip/main-migrated-2026-11-08` with an
isolated `CAMPAIGN_JOBS`: 86 commits, `campaign check` clean (apart from the sandbox's missing
`quest/dense-pages*` branches), map as in check 0. `fixes/MANIFEST.md` lists every file.
