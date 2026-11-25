# okt-followups: journal

State: ready for review
Quest: .campaign/detent/quests/okt-followups.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

All jobs done; the brief shows every criterion met (only CI is open: branch not pushed).

## Next

1. Lead: review, push for CI, land.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261112-131901-4e7ea5; fails without: 261112-114417-f899ed |
| A2 | 261112-131901-4e7ea5; fails without: 261112-114424-6f60ca (killed by the invalid fixture row, not the check), 261112-131902-b2e1a4 (killed by "local view invariant failed") |
| A3 | 261112-132758-bc1392 and 261112-132801-03b771 (killed; MUTANT KILLED in its log) (rerun of 261112-115019-543828 at cf30e069); fails without: 261112-132801-03b771 |
| A4 | 261112-131901-4e7ea5; fails without: 261112-114429-cba3de |

## Result

C1-C5 closed; no behavior change except C4's refusal (nothing reached it before). Tests: test_paged_definition (overlong and past-64-bit varints in an override row), test_view (flat record `app:ramp7`, default 1, 50% to 7, in and out of the bucket for 64 hashes and no hash), Python and Rust binding tests (`get_bool` refuses 2 and -1; health counts 6 knobs), test_paged_leaf_body (value without percentage, percentage without value, and the valid pairing). Source: LeafBody `ramped_` check (paged_leaf_body.cc/.hh), export_encoder `flag_` -> `definition_`. Mutants in tests/detent/mutants/c1..c4; bindings_mutant.sh runs the bindings mutant. No docs changed. Leaves open: sanitizers and platforms (CI).

## Rulings I made

1. C1: replaced the two header-position varint cases with the same two encodings in an override row (the header ones were refused for length anyway).
2. C2: added a third flat record `app:ramp7` (default 1, 50%, value 7) to test_view's image instead of a new test file.
3. C4: LeafBody tracks `ramped_` (knob-wide or any group percentage) and refuses at the end of Overrides unless it equals "has a rollout value"; skipped for the namespace watermark body.
4. one_knob_mutant.sh now reverts its patch on any exit (trap), so a build failure no longer leaves the slot dirty; bindings_mutant.sh is the same for the bindings suites.
