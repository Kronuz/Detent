# consensus-quiescence: journal

State: waiting on jobs 261121-012400-f56485 261121-012401-85a7ab 261121-012403-b34836 (F1/F2 mutants), 261121-012404-fce70e (F2 fix), 261121-012405-a27269 (A3 TSan at 3b20d96), 261121-012405-7903bb (review probe at 3b20d96); CI on the push (suite, A1 A2).
Quest: .campaign/detent/quests/consensus-quiescence.md (the lead's; never edit it)

## Doing

Correctness review CHANGES at a020e14 (Detent .campaign/detent/reviews/consensus-quiescence-correctness.md).
Fixes at consensus:3b20d96, pushed:
- F1: Core::campaign() and lead() clear resting_ (every role change ends a rest, as follower()
  did). Test simulation_test quiescence `transfer` (third voter up and down). Mutant
  rest-survives-campaign consensus:37a7a69: 261121-012400-f56485 --fails-without.
- F2 (lead's ruling: notify peers per group): new Message Gone{term}, wire tag 9 (types.h,
  codec.h, admission.h with the hard-state plan). Driver::report_fence, the one place a fence is
  first reported, sends Gone in the group's term to every other member; Core::receive_message
  (Gone) from its leader in its term calls leader_lost(), LeaderGone's own path. Tests:
  simulation_test quiescence `gone`; driver_test fence_tells_peers; groups_test driver
  quiescent_fence (leader's application holds a command, followers rest, the application fails:
  local run, a follower elected 1,344 ms after the fence, 0 node-gone reports). Mutant
  fence-tells-nobody consensus:2f0738c: 261121-012401-85a7ab (driver_test), 261121-012403-b34836
  (groups_test) --fails-without; fix 261121-012404-fce70e.
- F4: heard_from_leader's unreachable rest clause dropped (and its parameter). F5: closed_state
  comments say rest is not required.
- F3, F6 (non-blocking): not done; F3's thin random coverage is answered by the deterministic
  `transfer` and `gone` cases.
- consensus-driver-install told of 3b20d96 (its msg-1).

Next: msg-1 (drop the rpc_host SIGPIPE ignore and the sigpipe-default mutant) once node-admission
(consensus:c669067) lands on consensus main.

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

- msg-1 (drop the rpc_host SIGPIPE ignore after node-admission): accepted; consensus:c669067 is
  not on consensus main at a020e14, so 82cefee's ignore and its sigpipe-default mutant stay until
  it lands and is merged (under Next).
- Coordinator (consensus:50f0a48 interface, deadline mutants): merged consensus-transport at
  consensus:7cdcc0e (open_of required, store(group, incarnation); groups_test already on it).
  test/transport/mutant_commit.py no-extension, uncapped, twice now match deadline.h's
  late_extension(): killed at their mutants, jobs 261120-105254-0b5e84, 261120-105255-26b670,
  261120-105257-405bde (deadline_test asserts). (The mutants live in mutant_commit.py, not
  tools/mutants.sh.)

## Blocked on

Nothing.

## Evidence

At consensus:a020e14 (consensus main bb961b3 merged). Mutant jobs ran at their mutant commits.

| Criterion | Evidence |
|---|---|
| A1 | Suite `ci:macos-arm64` (CI 44877489407); reopen_barriers in 261120-225122-c34e4e (36 checks, 0 failures). Core consensus:b4314e4 simulation_test `closing` cases; Driver consensus:b8de3b8 driver_close_test `reopen_barriers` (a request with the reopening recover: 2 barriers, as an open group; without the record 5; damaged bytes Corruption). Fails without: 261120-095359-890b19 (admission without the hard state) |
| A2 | Suite `ci:macos-arm64` (CI 44877489407); simulation and leader_gone in 261120-225122-c34e4e: 300 seeds x 10,000 steps, 150 quiescent, 14,768 LeaderGone, 20,375 node-steps at rest; driver_test leader_gone (8 groups, 16 checks); groups_test driver quiescent part (both groups led from one node rest 3 s, node stops, both elect). Fails without: leader-gone-skips-one 261120-105231-8e1e83, leader-gone-ignored 261120-105233-efd55f, node-gone-unreported 261120-110213-1b3000 |
| A3 | TSan 261120-225122-c34e4e: simulation 300 seeds x 10,000 steps (150 quiescent; 31,340 closes at rest, 16,389 reopens resumed their term, 320 records refused as stale), oracles clean; driver simulation 30 seeds x 3,000 plus the regression seed clean; reopen_barriers, leader_gone |
| A4 | docs/perf/quiescence-idle-cpu.md. Strict 261120-225122-0760b8: 1M groups at rest over 10 s, 0.0 wakes/s, 0 handles/s, CPU 0.000 s per s, 0 group timers armed (8.3 GB peak). Strict baseline 261120-225122-5cedce: 250k with heartbeats, 1,000 wakes/s, 345,294 handles/s, 5,000,000 answers/s, 68.8 CPU-s per s, 275 us per group per s, 250k timers. The doc's 250k row (07d7c2 at 3f3f664) has the same counts and 182.8 us: CPU per group moves with the machine (this run beside 7 light holds), the counts do not. 1M heartbeat row: 261120-113151-e99e39 (167.7 us per group per s) |
| A5 | docs/mechanisms/quiescence.md (RFTCLOS1 bytes, refusals, close and reopen rules); 261120-225122-eb821c on faultfs: close_crash 8 seeds, 926 power cuts, 1,067 table closes, 1,276 releases, after a cut the last closed state resumed its term 160 times, 464 stale, never a term back; closed_bytes 5,708.5 B open, 11.8 B per row after the range sweep; simulation_test `record_bytes`, `lone_voter`, `three_voters` in the suite |
| A6 | docs/perf/idle-bytes.md. 261120-225123-e76794: idle row 5,719 B at its checkpoint against Xapiand's 6,984 B; owners 261120-225123-17e4ea (Worker 1,536 B, Core 1,056 B, Journal 845 B largest sites); three-voter leaders 1,306 MB RSS at 100k 261120-225123-8fede5 (deque before: 2,143 MB, 261120-111929-1f3e62 at probe 03e3b99); lone voter before 6,807 B (261120-101850-c631af at probe 7c14f40) |

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
- R16 F2: Gone goes to every other member on any fence, not only a leader's (the fenced Core's
  role is Fenced, and a receiver acts only when the sender is its leader in that term).
- R15 A6: Peer::flights deque to vector (libc++ deque keeps a 4 KiB block per idle peer).
- R6 Term record: the first storage batch after a reopen carries the hard state (Core::persist),
  so the reopen adds no barrier. Mutant without it fails `lone_voter` (local).
