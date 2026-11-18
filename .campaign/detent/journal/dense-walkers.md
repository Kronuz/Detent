# dense-walkers: journal

State: waiting on jobs 261111-013718-1582cb 261111-013732-376f7f 261111-013739-bb67c4 261111-013747-1068a8 261111-013754-2cd5dd 261111-013803-aa091e
Quest: .campaign/detent/quests/dense-walkers.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Lead ruled msg-22 (a): lazy_page_access.cc and docs/mechanisms/dense-pages.md joined the file set.
Detent:bbf7b8c routes first-touch verification through NodeAt and moves the how-it-works note to
dense-pages.md "In-page walks" (one-line pointer in paged_node_step.hh). Detent:51535b0 refreshes
a6-order and a10-cursor-default-limits, which no longer applied over the NodeAt walkers.
Jobs at Detent:51535b0: A1 grep 261111-013720-736885 passed (only NodeAt::decode and the
definition). Pending: native 261111-013718-1582cb (A2), 5M model 261111-013732-376f7f (A2), 5M
delta oracle 261111-013739-bb67c4 (A2), A3 mutant 261111-013747-1068a8 (--fails-without), A4
prepare 261111-013754-2cd5dd then exclusive measure 261111-013803-aa091e. Fuzz smoke
261111-012103-71570c at 34c6318 stands (its inputs did not change). Superseded jobs at 34c6318
cancelled.

## Next

1. Read the jobs; fix what they show; put the A/B numbers in Result.
2. Paste `campaign brief`'s Evidence table; State "ready for review".

## Blocked on

Nothing. msg-22 answered by the lead: option (a), done in Detent:bbf7b8c.

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
8. a6-order, restated: drops `require(p::NodeAt::child(frame.node, slot) == s.position)` whole,
   as the original dropped `require(slot == s.position)`; a10-cursor-default-limits only gets new
   context. Both apply at Detent:51535b0 (they are dense-pages evidence, not this quest's).
