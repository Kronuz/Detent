# consensus-quiescence: journal

State: waiting on jobs 261120-125553-eb46b6 (suite at consensus:2ddf9b2, --for A1,A2),
261120-124658-a4a9be (A6 owners), 261120-124659-af6f59 (A4 1M rest, strict); CI on the push.
Quest: .campaign/detent/quests/consensus-quiescence.md (the lead's; never edit it)

## Doing

Final evidence at consensus:2ddf9b2 (docs-only past 077f84f). Done at head: A1 261120-124657-bd28b4
(36 checks), A3 TSan 261120-124658-a7fcbc (300 x 10,000, 150 quiescent, Driver 30 x 3,000: ok),
A5 261120-124714-2cb311 (926 cuts, 162 resumed, 462 stale), A6 261120-124658-f2feae (idle 5,719 B),
261120-124659-8f32a6 (100k leaders 1,308 MB).

## Next

1. Fill the Evidence table with the head jobs; "ready for review" once suite and CI are green.

## Seed 399 (lead's task, Xapiand's Driver simulation)

Harness, not the Driver; fixed on consensus main since consensus:85ed0e6 (2026-11-18, seed 399
a regression seed there). Xapiand pins consensus:5923b6c, 167 commits behind main, without it.
- Reproduced at consensus:5923b6c (journal 4272229 pin): 399 and 727 fail, job 261120-124431-f7bb46.
- Cause (diagnostic job 261120-124621-50220e): the heal loop's "healed" entry was placed at index
  126 term 6 (727: 108 term 6); a term-8 leader truncated it (log ends at 120) and the loop
  re-proposes only when the call failed or was not placed, so it waits 4,000 rounds and fails.
  "application 0" on node 3 is the witness (odd seeds found 2 voters + a witness): expected.
- 5923b6c plus 85ed0e6's heal loop (patch only, test/driver_simulation.cc): 399 and 727 pass,
  261120-124619-d71c0d.
- Consensus main consensus:e1f249d and this branch consensus:077f84f: 399 and 727 pass,
  261120-123953-fffee2, 261120-123955-a7c478 (main's sim is reworked: those seeds are other
  scenarios there; 399 also runs as main's regression seed).
- For Xapiand: repin consensus past 85ed0e6. Nothing to change here.

## Messages

- Coordinator (consensus:50f0a48 interface, deadline mutants): merged consensus-transport at
  consensus:7cdcc0e (open_of required, store(group, incarnation); groups_test already on it).
  test/transport/mutant_commit.py no-extension, uncapped, twice now match deadline.h's
  late_extension(): killed at their mutants, jobs 261120-105254-0b5e84, 261120-105255-26b670,
  261120-105257-405bde (deadline_test asserts). (The mutants live in mutant_commit.py, not
  tools/mutants.sh.)

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Head: 261120-124657-bd28b4 (reopen_barriers), suite 261120-125553-eb46b6. Core: consensus:b4314e4, simulation_test `closing` cases (lone voter 2 barriers vs 5 without the record; three voters no election; stale record elects; bytes). Driver: consensus:b8de3b8, driver_close_test `reopen_barriers` (a request issued with the reopening recover: 2 turn barriers = an open group's; without the record 5; damaged bytes Corruption), job 261120-095401-ec64ca; mutant without the admission fix (journal refuses the batch) 261120-095359-890b19 --fails-without; suite 261120-095339-49e5f9 |
| A2 | Head: suite 261120-125553-eb46b6, simulation in 261120-124658-a7fcbc. Core and Driver consensus:1e40220, detector consensus:d291fac. simulation_test: 300 seeds x 10,000 steps, 150 quiescent under a node-pair detector oracle (14,768 LeaderGone, 20,375 node-steps at rest), quiescence cases (rest, LeaderGone at one or both followers, restarts); driver_test leader_gone (8 groups); groups_test driver quiescent part: both groups led from one node rest 3 s, node stops, both elect 1.6 s later. Suite 261120-110212-6dc33a (d291fac). Mutants --fails-without: leader-gone-skips-one 261120-105231-8e1e83 (7 of 8), leader-gone-ignored 261120-105233-efd55f, node-gone-unreported 261120-110213-1b3000 (no election in 10 s) |
| A3 | TSan 261120-124658-a7fcbc at 3f3f664 (was 261120-105234-101b27 at 1e40220): simulation 300 seeds x 10,000 steps (150 quiescent), reopen_barriers, leader_gone, oracles clean. Driver 30 seeds x 3,000 and seed 399 clean |
| A4 | docs/perf/quiescence-idle-cpu.md (consensus:3f3f664). driver_close_test idle_cpu (consensus:8611768); strict 261120-111949-10693b at 1M at rest: 0.0 wakes/s, 0 handles, 0 group timers armed (created 350 s, RSS 8.2 GB); heartbeat 250k 261120-113138-07d7c2: 1,000 wakes/s, 45.7 CPU-s per s, 182.8 us per group per s; 1M 261120-113151-e99e39: 1,000 wakes/s, 167.7 CPU-s per s, 167.7 us per group per s, 1M timers; local 100k: rest 0 wakes/s, 0 timers; heartbeat 1,000 wakes/s, 146 us CPU per group per s |
| A5 | `docs/mechanisms/quiescence.md` (consensus:4c0ad67, b4314e4): RFTCLOS1 bytes, refusals, close and reopen rules; simulation_test `record_bytes`, `lone_voter`, `three_voters`; crash sweep driver_close_test close_crash (consensus:f6a4288): 8 seeds, 926 power cuts, reopens with the closed state resume the term in the live host; after a cut 162 resumed, 462 stale, never a term back; 261120-124714-2cb311 at 3f3f664 (close_crash and closed_bytes) |
| A6 | Head: 261120-124658-f2feae (idle 5,719 B), 261120-124659-8f32a6 (1,308 MB at 100k), owners 261120-124658-a4a9be. docs/perf/idle-bytes.md (consensus:ebd8632, 8611768): lone voter idle row 5,703 B (before 6,807 B; Xapiand 6,984 B), jobs 261120-101900-6202e7, 261120-101850-c631af (probe 7c14f40), owners 261120-101825-a635c0; three-voter leader flights deque to vector, 8.4 KB less per leader: 261120-111929-1f3e62 (probe 03e3b99, 2,143 MB at 100k) vs 261120-111932-71ec5b (1,306 MB) |

## Result

<pending>

## Rulings I made

- R1 The closed record is stored by the host (strata's bundle, a parked group's table entry) and
  handed back at reopen; consensus writes nothing at close. No journal or checkpoint format
  change.
- R2 The leader is recorded, never inferred from a lone voter's own ballot: a three-voter
  leader X of T can remove the third voter and then itself, leaving a replica the only voter with
  its own ballot in T while X still leads T (two leaders in T; a conflicting entry fences it).
- R3 The record is used only on an exact match (hard state, last index and term, membership and
  its index); a stale one is ignored (the group elects), corrupted or foreign bytes throw.
  Safety needs only the term (one leader per term); the match keeps the record truthful.
  Measured: a mutant accepting any same-term record passes the random simulation (it is safe),
  and fails the deterministic `lone_voter` stale case.
- R4 The leader at close need not vote in the membership in force: a leader leads until its own
  removal commits (the random simulation found such a record within 300 seeds).
- R5 The closed state's types and codec live in core.h while storage.h and types.h are outside
  the file set; they move there if the set widens.
- R7 Driver::closed_state requires no waiter and an empty inbox besides Core::closed_state: the
  group is at rest. recover(id, restore, closed) decodes before any recovery IO, so a refused
  record leaves a half-opened row that closes (a Core built by recover and never started never
  drains).
- R8 Propose and ChangeMembership plans always reserve the hard state (not only after a reopen):
  one plan shape, and a reservation is released at the append.
- R9 The barrier count is the host turn log's `stats().barriers`. A request issued after the
  recover completes costs 3: the journal re-stages the table's checkpoint record as the group's
  first record (journal recover_shared) and it takes its own turn; issued with the recover, it
  rides that turn (2). Documented in quiescence.md "Through the Driver"; Xapiand issues the request
  with the recover.
- R10 A6: rare state boxed (capture, install, transfer, snapshot result, founding); pack_,
  persist_, pending_ stay inline (hot path, an allocation per proposal otherwise); config copies
  (~320 B) and the journal's 3,052 B reported, not cut. idle_bytes is report-only: block sizes
  differ by platform, a bound would flake on Linux.
- R11 A2: quiescence is opt-in (Timing::quiesce): a host without a detector keeps its timers.
  A follower at rest holds its leader current (refuses pre-votes) except to that leader itself;
  a leader at rest wakes on any frame, so a restarted member's pre-vote wakes it.
- R12 A2: a removed or stepping-down leader sends nothing, so (1) a follower never rests under a
  committed membership where its leader is leaving or not voting, and (2) a follower at rest
  wakes on another member's (pre-)vote (still refusing it) and re-acknowledges its durable log to
  its leader (an AppendResponse with a fresh token), which wakes a leader that still leads; a
  leader that no longer leads ignores it and the follower elects at its timeout. Found by seeds
  2 and 110 (healed group without a leader). A woken leader rests again only after every replica
  acknowledges a fresh round (told reset).
- R13 A2 detector: the Control connection's end is the node-pair report (HostTransport::
  on_node_gone, every loop); no new keepalive. A transient reconnect costs a pre-vote round.
  The groups_test case runs inside `driver` (CMakeLists is outside the file set).
- R14 A4: measured on the manual clock (1 ms steps), leader side with the peers' answers
  synthesized; the peers' own Driver cost is not counted. Setup uses 256 MiB turn-log segments:
  journal's TurnLog::forward_slice scans every live group per turn once past retain_segments,
  quadratic setup at 20k groups with 16 KiB segments (journal's, reported, not fixed here).
- R15 A6: Peer::flights deque to vector (libc++ deque keeps a 4 KiB block per idle peer).
- R6 Term record: the first storage batch after a reopen carries the hard state (Core::persist),
  so the reopen adds no barrier. Mutant without it fails `lone_voter` (local).
