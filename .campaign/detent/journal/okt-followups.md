# okt-followups: journal

State: waiting on jobs 261112-131901-4e7ea5 261112-131902-b2e1a4
Quest: .campaign/detent/quests/okt-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Suite 261112-115015-ce950b failed in test_local_view: "invalid local image row". Cause: my `app:ramp7` record did not set the override start (r+32) to the running override index (2, after app:bool's two rows); view.cc:261 requires it. Not the A4 LeafBody check. Fixed at 233c39c7. Jobs at 233c39c7: suite 261112-131901-4e7ea5; A2 mutant rerun 261112-131902-b2e1a4 (the earlier exit 0, 261112-114424-6f60ca, was killed by the invalid row, not by the check). A1 mutant 261112-114417-f899ed and A4 mutant 261112-114429-cba3de are at code the fix does not touch (test_paged_definition, test_paged_leaf_body); A3 bindings 261112-115019-543828 and A3 mutant 261112-115028-7f7f86 passed at 532c5c27 (binding tests and read_view.hh unchanged since).

## Next

1. When they finish: paste `campaign brief` Evidence table, write Result, State ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

1. C1: replaced the two header-position varint cases with the same two encodings in an override row (the header ones were refused for length anyway).
2. C2: added a third flat record `app:ramp7` (default 1, 50%, value 7) to test_view's image instead of a new test file.
3. C4: LeafBody tracks `ramped_` (knob-wide or any group percentage) and refuses at the end of Overrides unless it equals "has a rollout value"; skipped for the namespace watermark body.
4. one_knob_mutant.sh now reverts its patch on any exit (trap), so a build failure no longer leaves the slot dirty; bindings_mutant.sh is the same for the bindings suites.
