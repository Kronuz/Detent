# okt-followups: journal

State: waiting on jobs 261112-115015-ce950b 261112-115019-543828 261112-115028-7f7f86 261112-114417-f899ed 261112-114424-6f60ca 261112-114429-cba3de
Quest: .campaign/detent/quests/okt-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A3 job 261112-114409-93a80c failed: health_snapshot expected 4 knobs, my two new knobs make 6 (python test_detent.py:118, rust integration.rs:116). Fixed at 532c5c27. Jobs at 532c5c27: suite 261112-115015-ce950b (A1,A2,A4); A3 bindings 261112-115019-543828; A3 mutant c3 261112-115028-7f7f86 (rerun: the earlier run could have been killed by the count, not the test). Mutants A1 261112-114417-f899ed and A2 261112-114424-6f60ca (done, exit 0) and A4 261112-114429-cba3de (running) are at 23559fd2; the code and tests they exercise are unchanged by 532c5c27.

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
