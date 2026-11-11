# dense-walkers: journal

State: working
Quest: .campaign/detent/quests/dense-walkers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Shared step `paged::NodeAt` (src/detent_local/paged_node_step.hh) in place in every walker in
the file set; building and running the suite locally; new test tests/detent_local/test_dense_walkers.cc.

## Next

1. Local build and dense/paged tests green; commit.
2. Mutant tests/detent_local/mutants/dw-no-progress.patch (drops `slot < parent.end`); refresh
   cor1-unguarded-parent and a10-overlay-default-limits patches to the new code.
3. Jobs: native (A2), A1 grep, A3 mutant, fuzz smoke ASan/UBSan, 5M model oracle, A4 A/B.

## Blocked on

Lead: `src/detent_local/lazy_page_access.cc:211` calls decode_node (first-touch semantic scan,
`at = node.end`) and sits outside this quest's file set (reader-s6 holds `lazy_page_access.*`,
blocked). A1 cannot pass without it. Options: (a) add `src/detent_local/lazy_page_access.cc` to
dense-walkers' files: a 3-line change (`p::NodeAt at;` / `at.decode(...)` / `NodeAt::after`),
reader-s6 rebases over it as it already must (DES-10); (b) narrow A1 to the file set and let
reader-s6 convert it when it rewrites `resolve` (DES-7). Recommend (a): A1 holds at landing and
reader-s6's rebase touches the same lines anyway.

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
