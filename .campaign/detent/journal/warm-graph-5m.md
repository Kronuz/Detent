# warm-graph-5m: journal

State: working
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

1. Fix: the finalizer checks the format's semantic bounds (strings unbounded, 256 overrides), leaving
   authorization to publication admission (disk_store.cc) and recovery.
2. Test tests/detent_local/test_warm_semantic_bounds.cc: delta past 128 MiB of strings on a small base,
   ~12K definitions x 256 overrides; job at main (A1, fails) and --fails-without (A2).
3. Suite (A3); A4 exclusive 5M job, arms full and 0.5.

## Blocked on

Nothing

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

<Decisions inside the quest's scope, so the reviewer can check them.>
