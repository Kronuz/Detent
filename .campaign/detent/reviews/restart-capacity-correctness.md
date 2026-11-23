# restart-capacity review: correctness

Commit: Detent:230019777e59
Verdict: CHANGES
Date: 2026-11-22

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The mechanism is right: one admission table, every replicated decision under the entry's carried
limits, peers refuse a mixed profile. One product line has no test that fails without it (R1).

## Evidence I relied on

- 261121-212128-2b5201 (Detent:2f64e0fd): test_authority_state, node, node_completion, service
  pass. The merge to 23001977 brings no `src/` change (only test_transport_https.cc,
  test_managed_process.sh, managed.py), so it stands for the code; my baseline below reran it at
  the head.
- 261121-212132-5fb9e1 (Detent:2f64e0fd): A4 system run, 1,000 to 2,000 keys, voters back in
  1.86 s, both agents keep cursor identity and advance, retry receipt equal. managed.py changed in
  the merge; the lead's `campaign job rerun` covers it at the head.
- 261121-212136-f963d5 (mutant Detent:eedaff36, CommandApplication decisions under configured
  limits): fails at test_state.cc:983 (full `Result` vector equality) and test_node.cc:369. Values,
  not counts.
- 261121-212139-b4d7b2 (mutant Detent:7bc53720, `profile_ = source.limits_`): fails at
  test_state.cc:1046 only; both node tests pass under it.

## What I checked myself

Probe job 261122-001439-2f9246 at Detent:230019777e59, script
`.scratch/detent/review-restart-capacity-correctness/mutants.sh` (rerun it on the fix):

| Run | Change | Result |
|---|---|---|
| baseline | none | 4 of 4 pass |
| m1 | node.cc: group commands decode under `options_.application`, not `decoder->limits()` | 3 of 4 fail (test_node.cc:369, test_service.cc:755): caught |
| m2 | node.cc: administrative entries use the four-argument constructor (configured limits) | 4 of 4 pass: survives |
| m3 | state.cc: only the `state_bytes` Capacity check uses `d.source.limits_` | 4 of 4 pass: survives |

Read, risk by risk:

1. `State::limits_` sites. Every `CommandApplication` decision (namespaces, producers, keys,
   state_bytes, results) reads `validated.limits_`; `AdministrativeApplication` decides under
   `profile_`; ImageDecoder admits then bounds rows by the configured limits, which are at least the
   recorded ones. node.cc's other configured uses (ProposalBuilder, `propose_administrative`,
   snapshot and image size bounds, history) are local or encode new entries, which then carry the
   configured limits, so admission and application agree. No sync path (`State::apply`,
   `apply_batch`, `administer`) has a caller in `src/` outside state.cc. The digest depends on
   `request_bytes`, `mutations`, `definition_bytes` and `overrides` only (state.cc:161, 341), none
   raisable: rule 6 holds. The only admission above a record is the in-force layout (R3).
2. Replication. `peer_configuration` binds `semantic_profile(application)` into the fingerprint
   (peers.hh:16), so an old-limit follower and a raised leader never exchange entries or
   snapshots; the same covers a voter joining during a membership change with other limits. A
   group with old voters and raised voters forms a quorum only among equals; the rest refuse at
   the transport and, once restarted raised, replay their log under carried limits. A lowered
   field refuses at recovery: from an image (`recover()` throws, test_node.cc restart_raise phase
   3) or from a log entry (worker fences; `fence_reason` names the field, test_node.cc
   changed-profile case and test_service.cc). Safe, not merely refused.
3. Proof. Tests assert full `Result` equality, receipt bodies and exact refusal strings. The
   node-test clock fix at Detent:2f64e0fd is a harness defect: the removed `lead()` serviced the
   node at tick 0, so a single voter's election timeout never elapsed. Production startup runs on
   the real clock, and the A4 system run restarts three real voters. No restart bug hidden.
4. Docs. Removing the `image_bytes` and `state_bytes` exceptions is what check-docs requires: the
   website page now names both keys (configuration.md:142); Docs passed at Detent:38d36498 (run
   44989878297). The operator text matches the code apart from R4-R6.
5. CI at Detent:38d36498 (run 44989878521):
   - linux-asan-ubsan `test_system_managed_authority_failover`: unrelated, but the label is wrong.
     The artifact's events.jsonl shows the killed leader, authority-0, restarted and exited with
     `bind: Address already in use` (code 1); the drain check then got connection refused. That
     is a port-reuse race on restart, not D144's isolated-voter agents. The quest touches no socket
     code. File it under that signature.
   - macos-arm64 `test_local_disk_store` "invariant at line 191" (lease collection): unrelated.
     The quest changes nothing under `src/detent_local` or `tests/detent_local`; it is
     local-disk-store-lease-flake.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | gap | A2's node wiring is unproven. node.cc:519 passes `recorded_limits(entry.payload, …)` to `AdministrativeApplication`; dropping it, so the entry decides under the raised configuration, passes every test. Without the line, a `SetLayout` that got `Conflict` before the raise applies on replay. Voters that recover it from an image and voters that replay it then disagree. | probe 261122-001439-2f9246 m2; mutant 7bc53720 leaves both node tests passing | Extend test_node.cc `restart_raise` (or add a case): record a `SetLayout` under the old limits that gets `Conflict`, keep it in the log tail, restart raised and assert the same result and `pending_policy()`. It must fail under m2. |
| R2 | no | gap | No test covers a `state_bytes` Capacity decision under carried limits. Reverting only that check to configured limits passes every test, and the outcome makes `state_bytes` raisable. | probe m3 | One more group in test_state.cc `restart_raise` that fails `Capacity` on `state_bytes` before the raise and replays with the same `Result` under `raised`. Fails under m3. |
| R3 | no | gap | A raise is admitted even when the in-force layout's history files then exceed a peer fetch. Startup checks only the configured `layout` (node.cc:92). A `SetLayout` policy admitted under the old limits can write files above `fetch_bytes` once `state_bytes` rises without `image_bytes`. A lagging voter then cannot fetch them (peer_history.hh:19-22). The mechanism doc states this; the operator docs do not. | mechanism doc "What it does not do"; history_format.cc:510-521 | Owner's call: (a) refuse at startup after recovery when `file_bytes(configured, policy/pending)` exceeds `fetch_bytes(configured)`, naming `image_bytes`; (b) keep it and add one warning sentence to CAPACITY_RUNTIME.md and configuration.md. |
| R4 | no | nit | configuration.md:140-144: the new `## Raising authority capacity` heading now owns the managed agent paragraph that follows ("The private managed agent configuration names…"). | file | Move that paragraph back above the heading, or the section below it. |
| R5 | no | nit | CAPACITY_RUNTIME.md:32 says each recorded "image keeps deciding under the limits it records". An image decides nothing: the recovered state runs under the configured limits (mechanism doc, Admission 3). | file | "group and administrative entry keeps deciding…; an image is admitted and the state runs under the configured limits". |
| R6 | no | nit | The mechanism doc's Peers section says snapshots "carry an image of the receiver's own profile". After a raise, the latest image can still record the old limits until the next checkpoint; it is admitted, not equal. | file | "an image the receiver admits". |

## Not run

- Sanitizers and the Linux and FreeBSD platforms (CI's; run 45001434178 at 23001977 was queued
  when I read it).
- The A4 system run at 23001977 after the managed.py merge change (the lead's rerun).
- A multi-voter raise in which a voter is down during the coordinated restart and catches up
  later. I settled it by reading (fingerprint, carried-limit replay), not by a job.
- Relays or agents whose own local capacity sits below the raised `keys`. The contract makes that
  their own setting; it is not checked here.
