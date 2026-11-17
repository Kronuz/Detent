# transport-identity review: security

Commit: consensus:c84e64efe7c86ce9deba581045ebe64db7dc0d78
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

Read from each job's log, not the journal:

- At consensus:c84e64e: suite 261117-093903-345ea0 (53 of 53); cases 261117-093908-5f7414
  (`identity`: delivered 0 of 20 for cluster, group, epoch, profile, late, misrouted and spoofed,
  20 of 20 unnamed; `joiner` and `lagging` pass, 0 NotReady retries); relayout
  261117-093917-b0da65 (groups 1000 and 1001 moved loop, 1006 and 1007 stayed, all four commit
  on all three, 65 frames on node 1, 0 off their group's loop); TSan 261117-093923-dd6ee7
  (admission, identity, joiner, lagging, relayout, `--repeat until-fail:3`, no report, 0 NotReady).
- Ten mutants and two controls, each a one-hunk commit whose parent is consensus:1b94858 (checked
  with `git diff <c>^ <c>`), each exit 8 with the targeted assertion: `admit-*` at
  admission_test.cc:47 and the `settled` wait in `identity`; `open-*` at admission_test.cc:89;
  `closed-ignored` at `late_opened`; `open-once` at `reopened && !received(ok, "epoch2")`;
  `voters-only` fails `joiner` and `lagging` at "learner 4 never caught up" (spoofed 61 and 60).
- `git diff 1b94858 c84e64e` touches only test/transport/groups_test.cc: `Node::attempt` and
  the NotReady retry in `add_voter_4`. No mutant's kill site (admission_test, `identity`) and
  no control's failure step (learner catch-up, before the promotion) is in it, so all twelve
  stand for the head.

## What I checked myself

Scripts under `.scratch/review-transport-identity-security/` in the Detent home; rerun with
`campaign job submit --quest transport-identity --fresh --force -- sh <dir>/<script>`.

- `leftover.sh` + `patch_leftover.py`, job 261117-101819-c02b08 (exit 1: the defect shows; exit
  0 once fixed). Adds a `leftover` case to groups_test.cc: host 2 (one loop) and host 3 store
  and open g; node 1 connects over a raw `Links`, never sends g's open, and writes a control
  frame for g (source 1, destination 2) followed by a frame over host 2's 4 MiB bound. Host 2
  delivered node 1's frame: "delivered from incarnation 1 (never opened g): 1", 0 `unopened`.
- `lagging_only.sh`, job 261117-101903-3e7cc4 (exit 0): the `voters-only` control fails
  `lagging` at its joiner step, so I ran a rule that admits an unnamed source only to a joiner
  (a receiver whose membership does not name itself). `joiner` passes and `lagging` fails at its
  own step ("caught up from leader 4: NO"): the lagging case has the power A1 claims.

By reading host_transport.h at the head, against the five questions:

1. Every frame kind: control and data reach `Deliver` only through `admit()` in `deliver()`
   (host_transport.h:539-543); open goes only to `opening()`; closed changes only this host's
   own sending lane (`lane.opened`); credit only this host's own sending; any other kind or a
   credit, open or closed of another length aborts (483-486). The source check uses each frame's
   own `r.node`, so a peer cannot pass for another node's incarnation where membership names it.
   The `opened` check, though, uses the batch's peer, which S1 breaks.
2. The open exchange: a group opens per (node, lane, generation) only when stored and all four
   fields match; a mismatch erases that connection's entry first, so a peer closes only its own.
   No amplification: one 9-byte `closed` per refused group, deduplicated in `lane.closes`, and a
   sender re-opens at most once per `closed`, only before a frame it was already sending.
   Counters are 64-bit atomics.
3. `member_of` runs on the group's loop in the same task as `Deliver`, so admission reads the
   membership the Core holds at delivery. A frame admitted as unnamed and processed after a
   change that names its source elsewhere grants no more than the documented unnamed-source
   admission (Q7 trust model).
4. Restart with another L: `loop_of_group` is fixed for a process, so no group moves under a
   running transport; after a restart, frames that arrive before `store()` are counted
   `unstored` and answered `closed`, and the sender re-opens: refused and counted, never lost
   silently.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | yes | defect | Frames left in a loop's batch by a read that throws are delivered under the next read's connection. `Connection::deliver` throws on an oversized length (connection.h:271) after handing earlier frames of the same buffer to `arrived()`, which appended them to `here.in`; `on_read` is skipped, and the next `read()` on that loop, from any connection, stamps them with its own `Peer`. `deliver()` then checks `opened` against that peer, so an authenticated node gets a frame for g admitted without ever sending g's open (bypassing cluster, group, epoch and profile) whenever another node opened g there. A leftover open would likewise close g on the other node's connection, and leftover data credit returns on it. | Probe 261117-101819-c02b08: node 1's frame delivered, 0 `unopened`; host_transport.h:454-521, 538 | Tie the batch to its connection: stamp the `Peer` when `arrived()` first appends to a batch and flush (or drop) a batch whose peer differs, or have `Connection` run `on_read` for the frames it already delivered before it fails, or have `ended()` / a failing read clear `here.in`. Add the probe's `leftover` case to the suite. |
| N1 | no | nit | `lane.closes` (like the existing `grants`) grows one entry per distinct group id that a peer sends unopened or unstored frames for while not reading its socket. It is bounded by the peer's own bytes (89 per open, 25 or more per frame) and the Q7 trust model. | host_transport.h:425, 618 | Optional: cap `closes`, or end a connection whose refusals pass a bound. |
| N2 | no | nit | `store()` accepts all-zero cluster, group or epoch, which `encode_hello` refused; a host that forgets to fill one matches any peer that also sends zeros. | host_transport.h:175; hello.h:20-23 | Refuse zero identities in `store()` as `encode_hello` did. |
| N3 | no | gap | Before a group's membership loads (the test's `member_of` returns nullopt while the Driver has no state), every source is unnamed and admitted, including incarnations that the membership will place on other nodes. The doc does not tell hosts to call `store()` only once `member_of` answers. | groups_test.cc Node: `member_of`, then `store` before `create`/`recover` | One line in host-transport.md Identity: store a group once its membership is known. |
| N4 | no | nit | `forget()` stops new sends, but frames already queued in a lane still go out with the old open; the doc says "its frames are refused and not sent". | host_transport.h:189 | Reword, or drop the group's lane queues on forget. |

## Not run

- The other S1 variants (a leftover mismatched open closing g on the honest node's connection;
  credit returned on the wrong connection) were found by reading, not run; the same fix covers them.
- No fuzzing of the open and closed decoders (fixed-length, length-checked before decode).
- Linux and the CI matrix: CI's.
- No check of how a real host computes `GroupOpen` or `member_of` (Xapiand's, later quests).
