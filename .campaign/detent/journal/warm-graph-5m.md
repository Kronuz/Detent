# warm-graph-5m: journal

State: waiting on jobs 261112-143906-a7a2bf 261112-143954-efa99b 261112-144015-ab96d3 261112-144057-f1c829
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

## Next

1. Read the jobs: mutant 261112-143906-a7a2bf (A1, A2 --fails-without), suite 261112-143954-efa99b (A2, A3),
   A4 prep 261112-144015-ab96d3 (light: harness Detent:1e8d3285 built at the fix into .scratch/a4/build,
   nm-t9's 5M prepared store cloned to .scratch/a4/corpus-5m) then A4 261112-144057-f1c829 (exclusive,
   `round 1 full 0.5`, budget 540 s).
2. Delete .scratch/a4/corpus-5m and .scratch/h (1M store, subsets, instrumented build) when A4 is in.

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
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

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
