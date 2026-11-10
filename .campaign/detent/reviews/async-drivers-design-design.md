# async-drivers-design review: design

Commit: Detent:b11ab50
Verdict: CHANGES
Date: 2026-11-10

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

Final re-review of fix round 2 (Detent:a5472e8..b11ab50) for F11-F14 of the review at 73e96d6.
All four are resolved, and the evidence is real: each fix has a passing job and a control that
fails at the old rule. One question remains, and the F12 fix itself raises it. D10 now fixes
placement as `slot(group) mod L`, but a host's turn log and (under Q3(b)) its directory are per
core, so L becomes part of both the wire contract and the on-disk layout. The design does not say
where a peer learns L, what happens when two hosts disagree on it, or what happens to stored
groups when a host's L changes (F15).

## Evidence I relied on

Every job is at Detent:672e7b7 (`git diff 672e7b7..b11ab50 -- docs/design/async-drivers-bench`
is empty), with the pins consensus:ab05507, journal:e5f6ff1, completion:560c288 and task:bdafa5f.
I read each log and exit file under `~/.scratch/campaign/jobs/<id>/a1/`.

- F11: 261110-005432-a6d855 `blocking_entry` exits 0. Under D5's rule, lost-wake gives `stalled`
  with 0 advances; needs-peer gives `needs_peer` with 0 advances; first-election advances once
  (1,500 ms) and the record is placed, written and committed; cancel-staged is `cancelled` with
  1 written and 1 committed; deadline is `cancelled` after 8 advances. The 73e96d6 rule, run as a
  control in the same job, fails 3 of 5: 100,000 advances on lost-wake and needs-peer, and
  `unwritten=1` on cancel-staged. 261110-005433-b5324c (`v1`) exits 1 as it must.
- F12: 261110-005434-15e1c5 `reader_test` exits 0: longest heartbeat gap 100 ms, 300 dropped.
  261110-005435-c43e84 (`today`) exits 1 with a 3,000 ms gap. 261110-005436-650916 (strict)
  `conn_cost 64`: context 29,408 B; an end costs 111,234 B over asio, 74,524 B direct and
  40,732 B released, each with 1.00-1.02 fds. The design's numbers match these.
- F13: 261110-005430-687d57, ASan+UBSan 261110-005428-f43eb1 and TSan 261110-005429-afba6f run
  `wheel_test` with `offloop`: 300 rounds, 150 created and 200 destroyed off the loop,
  `states_left=0`. 261110-005431-3ee08b (`offloop-embedded`) aborts with exit 134 on the wheel's
  owner assertion, `wheel.hh:109`.
- F14: 261110-005423-d753e6 (ASan+UBSan) and 261110-005426-635ab8 (TSan) run my
  `.scratch/review-async-drivers-design-design/waiter_mutants.sh` (home checkout) at the fixed
  commit. `explore` kills every mutant. Both resume-before-unlink mutants fail
  `waiter_test.cc:182 s.touched_after_free() == 0`, which is the right reason: the freed frame's
  link fields are written after the caller freed it. The claim mutant fails
  `:184 resumes == 1`. Under TSan the threaded mode still misses both resume-before-unlink
  mutants, which is why the explorer is the check that counts. 261110-005427-534e76 is the clean
  run: 131 and 51,014 schedules with no violations, and the naive control is caught in 47/88 and
  18,969/27,276 schedules.

## What I checked myself

- Risk 1, frame drops: Core already recovers from a lost Control frame. Flights reset after
  `rpc_timeout` (consensus:ab05507 `core.h:1059`, `:973`). A lost TimeoutNow is resent every
  heartbeat (`:1020`). A snapshot flight times out and retries (`:1057`, `:542`). Read probes
  ride on appends and their responses (`:383`, `:942-954`). Bulk frames are never dropped
  because each transfer has its own connection. So dropping is safe for liveness and for
  snapshot transfer. A sustained-load cost remains (N7).
- Risk 1, placement: with L a power of two on both hosts, loop i shares groups with exactly the
  loops D10 names, for any L_a and L_b. Membership changes need nothing extra, because a joiner's
  loop is computed the same way. Peer addresses are local `PeerOptions::addresses` (`peers.h:30`),
  not replicated, so each host's view of a peer's L is local configuration. Nothing in D10 says
  how that view is checked. With Q3(b) and a `TurnDriver` per core (D2 at `async-drivers.md:87`),
  a change in L also moves groups away from their stored data. That is F15.
- Risk 2: D5's step 3 is reached only after steps 1-2 reach a fixed point, and step 5 only after
  step 4 finds nothing, so neither path leaves a staged record behind. Checking `needs_peer` at
  the call is right for `propose` and `read`. `create`, `recover`, checkpoint and `close` need no
  peer. A single voter that proposes adding a voter is not refused at the call, but it ends in
  `stalled` (its timers stop counting), not in a hang (N6).
- Risk 3: `asio_scheduler` is defined as single-threaded ("All calls happen on the executor's
  thread", task:bdafa5f `task_asio.hh:49-51`), so `runs_here` names one thread and the wheel's
  owner rule holds. I traced `wheel_deadline` (`loops.hh:99-170`) through destruction on the loop,
  off the loop, during a fire on the loop, during a fire off the loop, and before a posted arm.
  None of them reaches a freed state or requests after the destructor returns. My grep of
  `task::deadline` across the repositories under `~/code/Kronuz` at origin/main finds the same
  users as D4's table: Detent `owner_loop.hh:72`, `https_client.hh:83`, `client.cc:291`; http
  `http_asio.h:366`, `:807`; `http_asio_tls.h:97`. The contract now stays any-thread, so none of
  them changes. One leak path remains (N8).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | resolved | Clock (`deadline<S>` on `S::clock`), the thread rule, detached-list `expire` and far deadlines are stated in D4 and are quest 1 criteria; the bench tests each, also under sanitizers. Its one remaining contradiction is F13. | 6240b4, c1d3b9, 2730b5; D4 | - |
| F2 | no | resolved | `expire` detaches due entries before firing; the probe survives both modes. | 2d7f1a; `wheel.hh:48-82` | - |
| F3 | no | resolved | D8 states the claim word, the hop to the group's loop, and unlink before resume. The protocol is right; its test has a blind spot (F14). | D8; `waiter.hh:51-69`; 28d8d8 | - |
| F4 | no | resolved | Machine-owned IO runs under the loop's shutdown token; the staged, read, create/recover and close rows are in the table. The blocking path's residue is F11. | D8 table | - |
| F5 | no | resolved | Driver calls run on the group's loop (D2); the blocking entry commits turns inline (5a484f) and owns its groups. Its termination is F11. | D2, D5; 5a484f | - |
| F6 | no | resolved | Today's `Peers` is measured and costed, and D10 decides the shared transport. Its open points are F12. | 400d3c; D10, Cost | - |
| F7 | no | resolved | The baseline is Detent's runtime per group, 17.5 cores per 1,000; the shared loop is its own row; the core-hours are restated correctly. | f87d0f; Cost | - |
| F8 | no | resolved | "at least 26 cores", with the reason. | Limits | - |
| F9 | no | resolved | The build plan says 3-5 add beside today's APIs and 6 takes the pin bump with `AuthorityPeers`. | Build plan | - |
| F10 | no | resolved | D5 step 2: a manual clock jumps, a steady clock sleeps. | D5 | - |
| F11 | no | resolved | D5 is now a five-step loop. It drains before it returns or throws, counts only operation deadlines and a lone voter's election as progress, and refuses multi-voter `propose`/`read` with `needs_peer`. Each case has a test, and the old rule fails 3 of 5 in the same job. | a6d855, b5324c; D5 | - |
| F12 | no | resolved | (1) Routing: placement is `slot(group) mod L` with a port per loop, so a connection joins two loops that share groups, nothing hops threads, and the cost is restated as `29 KB + H x 41 KB` from a measured 40,732-byte idle end. (2) Read side: each group has its own input slot and a full slot drops the frame, so a group with full admission no longer delays another group's heartbeat. Quest 5 has both criteria. The open part of the placement rule is F15. | 15e1c5, c43e84, 650916; D10 | - |
| F13 | no | resolved | Option (b) with the contract kept: `task::deadline` stays any-thread on every scheduler, and its entry lives in a refcounted cancel state that only the loop touches. Off the loop, the constructor posts the arm and the destructor posts the unlink after claiming the state against a fire. The quest 1 criterion matches. The deadline as first designed trips the owner assertion. | 687d57, f43eb1, afba6f, 3ee08b; `loops.hh:99-170`; D4 | - |
| F14 | no | resolved | Every cross-loop post is now a scheduling point, and freed frames are frozen and compared. `explore` kills all three of my mutants, the resume-before-unlink ones on `touched_after_free`. Quest 4's criterion names the mutants. | d753e6, 635ab8, 534e76; `loops.hh:30-35`, `waiter_test.cc:182` | - |
| F15 | yes | gap | D10's placement makes the host's loop count L part of the wire contract and of the storage layout, and settles neither. (1) Agreement: the dialer computes the remote loop from the peer's base port and L, which come from local configuration (`PeerOptions::addresses`, `peers.h:30`, not replicated). D10 says nothing about checking that view, or about what a loop does with a group-open for a group it does not own. If a peer restarts with a different L, stale views could send its groups' connections to the wrong loop: either a silent stall or a hop, with no error. (2) Change: a `TurnDriver` is per core (D2), and under Q3(b) so are the directory and owner lock. When L changes, `slot mod L` moves most groups to loops whose turn log and directory do not hold their records. Nothing moves a hot group off its loop either. This is a bytes-on-disk and protocol decision, the kind that is expensive to reverse. | `async-drivers.md` D10 *Placement and routing*, D2 `TurnDriver // one per core`, Q3(b); consensus:ab05507 `transport/peers.h:30` | (1) Carry `(base port, L, loop index)` in the hello. A group-open on a loop that does not own the group is refused with an error that names the owning loop, never routed or dropped silently. (2) Choose one: (a) L is persisted with the host's storage and fixed for its life, a different L refuses to start, and changing it is an offline migration (say so in Limits); (b) storage is keyed by slot range instead of by core, so an L change only remaps slots, with the turn-batching cost stated; (c) a group whose data is on another loop accepts a receive hop. State that placement does not rebalance. Add a quest 5 criterion: two hosts with mismatched L get a refusal, not a hang. |
| N1 | no | resolved | Q7 now checks membership in force (voters, learners, joiners), moves the hello's checks into the group-open exchange, and makes the per-group pins per-node pins. | D10 *Identity*, Q7 | - |
| N2 | no | resolved | The idle-bytes job e9dff1 is cited. Its rows match 1f63c8 and fdfe40. | Sources | - |
| N3 | no | resolved | Labelled "derived". | Cost, Idle CPU | - |
| N4 | no | resolved | It now says quest 1 adds the clock types. | D4 *Clock* | - |
| N5 | no | resolved | D8 now states the shutdown order: callers' loops outlive the resumes posted to them. | D8 | - |
| N6 | no | nit | Step 4's "owned single-voter group with no leader" should say *whose one voter is this node*. As written, a group where this node is a learner of a remote single voter re-arms its election forever and never throws `stalled` on a steady clock. A single voter that proposes adding a voter is not refused at the call and ends in `stalled`; say so. | D5 step 4, `needs_peer` paragraph | One phrase each. |
| N7 | no | gap | Under sustained load, a busy group's frames are dropped and then resent after `rpc_timeout` (`core.h:1059` resets the whole pipeline), so its flow control becomes timeout-paced retry instead of backpressure. That is safe but may cost throughput and bandwidth. Group-open and connection-level frames must never be dropped. | D10 *The reader never waits on one group* | Quest 5: measure a busy group's throughput under drop against today's reader, and state the exemptions. |
| N8 | no | gap | Teardown step 3 (`destroy_roots` on the owner's thread, after `close`) destroys frames off the loop. Each armed deadline in them then posts an unlink that `post_call` drops on a closed scheduler, and the post's reference leaks, because the bench lambda (`loops.hh:143`, `:157`) never releases it. | task:bdafa5f `task_asio.hh:51-56`, `:93` | Quest 1: a dropped call releases its reference, plus an LSan test of teardown with armed deadlines. |
| N9 | no | nit | `waiter_test` no longer runs `explore 3` (938,978 schedules before the explorer grew), and the freed-frame check sees writes, not reads. | 534e76; `waiter_test.cc:42-53` | Quest 4: keep a three-caller run as a job, even a long one. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- No new probe jobs. F11-F14 are settled by the contributor's jobs above, one of which runs my
  mutant script at the fixed commit. F15 and N6-N9 come from the design text and the cited
  `core.h`, `peers.h` and `task_asio.hh` lines.
- D10's per-loop transport is still a model (`reader_test`) plus one measured connection; quest 5
  builds it.
- No Linux numbers. A8 (the Xapiand lead's review) is outside this lens.
