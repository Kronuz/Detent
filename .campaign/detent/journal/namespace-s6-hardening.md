# namespace-s6-hardening: journal

State: waiting on jobs 261117-121439-c334b3 -d3c6d0 (tests), mutants 261117-121444-739234 (new, F1) 261117-121452-7c08c8 261117-121453-54d979 -d4a5f8, gates 261117-121447-835ddc (native) 261117-121447-455b19 (pins)
Quest: .campaign/detent/quests/namespace-s6-hardening.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review F1 (correctness): fixed at 892e121b, main merged (5c19e6cc). plan_frames sends no Dropped to a same-cluster store above the drop's position (Ahead rule). The probe became a scenario in test_namespace_history (voter 2 cut off across the re-creation; store at the new incarnation gets no request, a store of the old one still gets Dropped). Jobs resubmitted at 892e121b, with a new mutant removing the guard. The first native gate failed test_authority_transport_peers and test_authority_multivoter under load (3 of 3 passed alone); rerun at the new head.

## Next

0. When the jobs finish: if native fails again, name the tests here; if the F1 mutant passes the test, tighten it; then set ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261117-075322-afbd3a, 261117-075322-b090a5, 261117-075327-25508b; fails without: 261117-075322-55a56b, 261117-075322-92def8 |
| A2 | same tests; fails without: 261117-075322-9e1c41 (early apply; 261116-214157-4dc912 is the older run) |
| A3 | same tests; fails without: 261117-075323-d98365 (pending) |
| A4 | same tests; fails without: 261117-075323-bc058a, 261117-075323-0f145c (pending) |
| A5 | include/detent_distribution/history.hh tier comment rewritten to the F1 rule (Ahead first, whatever the incarnation); checked by the lead 2026-11-17: Ahead is tested first for a same-cluster store above head whatever its incarnation, and FullCopy only for another incarnation at or below head, which is the cutover review F1 fix (position before identity) [manual] |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- F1: the F1 probe lives in test_namespace_history.cc (in my set), not a new test file (CMake is outside it). F2 (Dropped through the HTTP service) is a non-blocking gap, left for the lead.

- src/detent_authority/service.cc is outside my file set but A3/A4 need it: the request-building moved to `plan_frames` (frames_export.cc, in set), service.cc keeps a 3-line call and a `FramesBehind` catch (503 source_behind). Lead to confirm or reassign.
- A4 answer is 503 `source_behind` (existing client mapping SourceBehind), not a retry frame: no client change, whole-answer, directory cursor untouched. Applies to followed names and to directory entries `all` brings.
- A3 is a new v2 FrameKind::Dropped (4) frame (no piece), sent only when the session holds a store of a dropped namespace; session_frames_version stays 1 (no users).
- A2: `next_namespace_session` takes a `FramePreparer` and stages all frames (ledger-admitted) until the response is verified, then publishes together (F9's unit: prepared generations). The agent's real preparer lands with P4.
- A1: encoder takes an optional ledger (private default when none: relay's call in namespace_relay.cc is outside my set; P2 should pass its ledger). Decoder/client use Limits::storage_account. Added Ticket::absorb to merge incremental admissions.
- The 503 mapping moved out of service.cc into `frames_failure` (frames_export.cc), tested in test_namespace_history (FramesBehind -> source_behind, else distribution_unavailable); service.cc calls it. Lead confirmed service.cc is in my set.

Follow-up for the lead: namespace_relay.cc's SessionFramesEncoder call still uses the encoder's private default ledger (out of my scope); P2 should pass the relay's ledger.
