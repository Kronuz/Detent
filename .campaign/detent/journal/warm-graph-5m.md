# warm-graph-5m: journal

State: waiting on jobs 261112-154454-6606a0 261112-154455-151646 (lead's A1 mutant and suite reruns at 22a97d53); then paste the table and set "fixes ready"
Quest: .campaign/detent/quests/warm-graph-5m.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Cause found (A1, diagnosis): the failing `require` is the warm validator's Finalize check
`s.counts.strings <= s.semantics.strings` (paged_warm_validator.cc, Phase::Finalize). The delta's
SparsePlanFinalizer builds its WarmGraphValidator with `PagedSemanticLimits{}` (sparse_plan_finalizer.cc:116),
whose `strings` default is 128 MiB = 134217728, not the store's limit. The 5M corpus holds
161388910 string bytes (153.9 MiB; `.scratch/h/strings.py`), so any delta at 5M fails Finalize; 100K
(~3 MiB) and 1M (31 MiB) pass. Same class as the PagedCursor bug: a default sized for small stores.
Instrumented 1M run (local, light hold): `WG5M finalize records 974665 strings 31456865 semantic strings
134217728 ... own_build 1` -- the finalizer's validator, default semantic bound. Publication admission
(disk_store.cc:1702) already enforces the store's `recovery_limits.strings`/`overrides`.

## Review fixes

- F1 (blocking gap): `test_warm_semantic_bounds` now opens a `DiskStore` (default `recovery_limits`) on the
  installed base and hands it the 138,372,024-string-byte plan; `begin()` must throw
  `managed paged publication exact base or admission mismatch`, and the plan stays intact. disk_store.cc
  untouched. Confirm: F1=261112-154129-2cdfff (reviewer's mutant, narrowed to this test's target:
  MUTANT KILLED at 6c52a2b3; the mutant lets `begin()` proceed and the test dies on StoragePressure).
  Unmutated pass at the same commit: 261112-154256-ad53cb (done, exit 0). N1 is delta-plan-scaling's; N2, N3 moved to
  namespace-s7-ids (A4).

## Next

1. Read the jobs: mutant 261112-143906-a7a2bf (A1, A2 --fails-without), suite 261112-143954-efa99b (A2, A3),
   A4 prep 261112-144015-ab96d3 (light: harness Detent:1e8d3285 built at the fix into .scratch/a4/build,
   nm-t9's 5M prepared store cloned to .scratch/a4/corpus-5m) then A4 261112-144057-f1c829 (exclusive,
   `round 1 full 0.5`, budget 540 s).
2. Delete .scratch/a4 (5M store clone, harness build) when A4 is in; .scratch/h's stores are deleted.

## Blocked on

Lead: the test registration (4 lines) in tests/detent_local/CMakeLists.txt is outside the file set, and
`campaign status` lists it as a landing blocker. Options: (1) widen the file set to that file
(recommended: every test in this directory is registered there by hand, no glob); (2) fold the case into
an existing test file, also outside the set. The work is otherwise unaffected.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261112-154454-6606a0 (mutant restores the 128 MiB default; the new test fails, job passes iff killed); diagnosis above (Finalize `counts.strings <= semantics.strings`, 161388910 > 134217728 at 5M); fails without: 261112-143906-a7a2bf, 261112-154454-6606a0 |
| A2 | 261112-154455-151646; fails without: 261112-154454-6606a0 |
| A3 | 261112-154455-151646 |
| A4 | lead and contributor: the budgeted exclusive 5M run at Detent:6a3d64f2 published the 0.5 C delta (apply 514.8 s); since then the only change under src/detent_local and include/detent_local is namespace-s6 replacing agent_store.hh's cursor structs with aliases of the distribution types, off the plan and finalize path, so per D85 the 5M arm is not rerun |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The test moved into the `if(TARGET detent::local_publication)` block of tests/detent_local/CMakeLists.txt
  and links that library, since a DiskStore needs it (a file-set member).
- The F1 mutant script is the reviewer's, narrowed to build and run only `test_local_warm_semantic_bounds`
  (faster; kills only if this test does), at `.scratch/f1/mutant.sh` in the worktree.

- The fix uses the format's semantic bounds in SparsePlanFinalizer (strings unbounded, 256 overrides per
  definition, the histogram's and validate_definition's bound), not a plumbed store limit: the finalizer
  verifies physical construction ("Producers retain authorization", sparse_plan_finalizer.hh), and the
  store's `recovery_limits.strings`/`overrides` are enforced at publication admission (disk_store.cc:1702)
  and recovery (generation.cc). No check in the validator changed. The compaction plan shares the
  finalizer and gains the same fix.
- A1's "fails on main" is the mutant job: its patch restores main's sparse_plan_finalizer.cc exactly, so
  the tree it runs is main's source plus the new test.
- The test is scaled down in records, not bytes: 12,000 definitions x 256 overrides x 45-byte keys =
  138,372,024 string bytes (the bound is in bytes, so nothing smaller reaches it); 4.2 s and 966 MB peak
  RSS locally.
