# namespace-s6-hardening: journal

State: waiting on jobs 261117-075322-afbd3a 261117-075322-b090a5 (tests), mutants 261117-075322-9e1c41 -55a56b -92def8 261117-075323-d98365 -bc058a -0f145c, gates 261117-075327-c3d9ec (native) 261117-075327-25508b (pins)
Quest: .campaign/detent/quests/namespace-s6-hardening.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Built and green once; two mutants survived (no staging charge, no directory admission), so bfcb4159 adds tests that kill them (frames reporting 32 MiB against a 16 MiB ledger; the directory block's size pinned). All evidence, mutant and gate jobs resubmitted at bfcb4159.

## Next

0. When the jobs finish: if a mutant job fails (test passed without the fix) tighten its test; fix any red test; paste `campaign brief`'s table into Evidence; set ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A5 | include/detent_distribution/history.hh tier comment rewritten to the F1 rule (Ahead first, whatever the incarnation); lead to review [manual] |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- src/detent_authority/service.cc is outside my file set but A3/A4 need it: the request-building moved to `plan_frames` (frames_export.cc, in set), service.cc keeps a 3-line call and a `FramesBehind` catch (503 source_behind). Lead to confirm or reassign.
- A4 answer is 503 `source_behind` (existing client mapping SourceBehind), not a retry frame: no client change, whole-answer, directory cursor untouched. Applies to followed names and to directory entries `all` brings.
- A3 is a new v2 FrameKind::Dropped (4) frame (no piece), sent only when the session holds a store of a dropped namespace; session_frames_version stays 1 (no users).
- A2: `next_namespace_session` takes a `FramePreparer` and stages all frames (ledger-admitted) until the response is verified, then publishes together (F9's unit: prepared generations). The agent's real preparer lands with P4.
- A1: encoder takes an optional ledger (private default when none: relay's call in namespace_relay.cc is outside my set; P2 should pass its ledger). Decoder/client use Limits::storage_account. Added Ticket::absorb to merge incremental admissions.
- The 503 mapping moved out of service.cc into `frames_failure` (frames_export.cc), tested in test_namespace_history (FramesBehind -> source_behind, else distribution_unavailable); service.cc calls it. Lead confirmed service.cc is in my set.

Follow-up for the lead: namespace_relay.cc's SessionFramesEncoder call still uses the encoder's private default ledger (out of my scope); P2 should pass the relay's ledger.
