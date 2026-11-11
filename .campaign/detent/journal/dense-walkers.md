# dense-walkers: journal

State: waiting on jobs 261111-012056-86bd1f 261111-012107-84ea8e 261111-012113-342ad4 261111-012130-6b72b5 261111-012139-b3859f
Quest: .campaign/detent/quests/dense-walkers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs at Detent:34c6318. Done: A3 mutant dw-no-progress 261111-012058-48b6e9 KILLED by
test_local_dense_walkers; fuzz smoke 261111-012103-71570c passed (ASan/UBSan, 1,273,973 runs in
301 s, 0 findings). Pending: native 261111-012056-86bd1f (A2), 5M model check
261111-012107-84ea8e (A2), 5M delta oracle 261111-012113-342ad4 (A2), A/B prepare
261111-012130-6b72b5 then exclusive measure 261111-012139-b3859f (A4). Local: 58 dense/paged/
lazy/reader tests passed on a Debug dev build; both refreshed overlay mutants killed locally.

## Next

1. Read the jobs; fix what they show.
2. On the lead's msg-22 ruling: (a) convert lazy_page_access.cc (patch ready, see Blocked on),
   `campaign job rerun`, submit A1; (b) submit A1 with the narrowed grep.
3. Push the branch for CI; State "ready for review".

## Blocked on

Lead: `src/detent_local/lazy_page_access.cc:211` calls decode_node (first-touch semantic scan,
`at = node.end`) and sits outside this quest's file set (reader-s6 holds `lazy_page_access.*`,
blocked). A1 cannot pass without it. Options: (a) add `src/detent_local/lazy_page_access.cc` to
dense-walkers' files: a 3-line change (`p::NodeAt at;` / `at.decode(...)` / `NodeAt::after`),
reader-s6 rebases over it as it already must (DES-10); (b) narrow A1 to the file set and let
reader-s6 convert it when it rewrites `resolve` (DES-7). Recommend (a): A1 holds at landing and
reader-s6's rebase touches the same lines anyway. Asked as msg-22. The (a) change:
`for (p::NodeAt at; at.offset() < nodes_end; at = p::NodeAt::after(node))` with
`node = at.decode(bytes, bounds)` and `#include "paged_node_step.hh"`.
Also for the lead: docs/ is outside the file set, so the how-it-works note for the step lives in
`paged_node_step.hh`; one "In-page walks" paragraph in docs/mechanisms/dense-pages.md would need
that file added too.

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

1. The shared step is a type, `paged::NodeAt` (src/detent_local/paged_node_step.hh), not a set
   of functions: an in-page offset can only come from the page start, `after(node)` (node.end) or
   `child(parent, slot)` (inline, `slot >= parent.end`), and `decode()` is the one caller of
   decode_node. Stored offsets (OverlayReference, walker frames, packer cluster start) carry the
   type, so no walker can decode at an unchecked slot. Header-only: same compares as before.
2. Progress is `slot >= parent.end`; the old `slot <= offset` half was implied (end > offset).
   decode_node already bounds every node by the node region, so that is the page-end half.
3. A refused step throws std::invalid_argument("inline slot does not progress"), like every
   decode_node framing failure. Read state already maps all walk failures to ReadSourceFailure;
   the cursor's progress failure was ReadSourceFailure and now is invalid_argument, as its decode
   failures already were. Contextual reuse and the assembler's corresponding-path walk had no
   progress check and now refuse back edges.
4. Preorder validators (verify_page_structure, PageValidator, packer cluster scan) keep their
   exact `slot == position` check, now as `NodeAt::child(parent, slot) == position`.
5. Visit bounds stay per walker (overlay key count: header.nodes); the quest asks for progress
   and page end, and the DFS walkers that reach unverified pages are budgeted.
6. The packer's InjectCopy slot rebasing parses the already-scanned cluster bytes without
   decode_node and follows no slot; left as is.
7. Mutants cor1-unguarded-parent and a10-overlay-default-limits no longer applied; restated
   against NodeAt (same intent). Both still killed locally by dense_overlay_bounds/dense_walkers.
