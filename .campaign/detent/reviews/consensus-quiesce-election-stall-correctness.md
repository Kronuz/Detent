# consensus-quiesce-election-stall review: correctness

Commit: consensus:ab47d8a60570c125eb351ce319b7a85f83fa455d
Verdict: APPROVE
Date: 2026-11-22

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 2, re-check after fix round 1. The branch head is ab47d8a (00a59a8 and ab47d8a on top of
the main merge e2743bf); e2743bf itself carries none of the fix. Every job below ran at ab47d8a.
Round 1 reviewed consensus:dbb22e7.

## Evidence I relied on

- A1 at ab47d8a: 261121-215851-aed878 (seed 1080 at rest), 261121-215854-ecc90b (seeds 1-3000 at
  rest, Release), 261121-215857-8d21a5 (ASan+UBSan), 261121-215900-6ba0bc (TSan).
- A3 at ab47d8a: 261121-215904-ed1f54 (TSan ctest), 261121-215907-6a75bb (quiescence_test under
  ASan+UBSan: s5 and s7 exposed 59 units, the rest 0, against a 60-unit heartbeat; 0 messages
  after rest).
- Mutants at ab47d8a, each `--fails-without` and each failing on a value (logs read):
  - 261121-215921-70138c, wake-ack-unanswered: the s4 and s6 state checks fail.
  - 261121-215936-868140, the same mutant on s6 alone.
  - 261121-215924-1cf081, wake-ack-overtaken: the s4 state check fails.
  - 261121-215927-4866dd, wake-ack-once: s5 is exposed for 859 units, a term moves, and 15
    messages follow at rest.
  - 261121-215930-dc004e, wake-ack-dropped-busy: the s2 state check fails.
  - 261121-215933-7dd2bc, stale-frame-silent: s7 is exposed for 859 units, and 14 messages
    follow at rest.
- 261121-215942-13c686: my round-1 sweep (`exposure.sh`) at ab47d8a. Over 3000 seeds at rest it
  finds 0 spans in which the leader rests while a follower of its term is awake for election_min
  or longer; round 1 at dbb22e7 found 222. The longest span is 83 ms, against a 20 ms heartbeat and
  a 100 ms election_min. With quiesce off the count is 0 (control), and 613 of the 3000 seeds elect
  while settling, against 617 with quiesce off.
- 261121-215945-2d65fc: quiescence_test at_rest with core.h at main e35356a, at dbb22e7 and at
  the fix. Each sends 8 messages from a write to rest, and 0 in 80 election timeouts at rest.
- CI run 44991647211 at ab47d8a is green, as the lead reports; I did not open it.

## What I checked myself

The scripts are under `.scratch/detent/review-consensus-quiesce-election-stall-correctness/` in
the home checkout.

- **The contributor's claim about job 261121-215938-7eb2c6** (`wake.sh probe_wake.cc` at
  ab47d8a, exit 1 on s5 only): the claim is true. In s5, probe_wake.cc:155-172 drops both
  acknowledgements and then delivers the followers' answers to the leader's round, which they sent
  at rest. The leader receives exactly what rested followers send. Any rule under which an idle
  group rests must therefore rest here, so the check right after delivery cannot pass. What s5 can
  test is the bound. quiescence_test checks that bound (59 of 60 units), and wake-ack-once fails it
  with 859 units. Every other case in 7eb2c6 passes at the fix: s1, s2, s2c, s3, s4 and s6.
- **Question 1, the class (core.h:401-406, 556-578, 1120-1121, 1216-1232).** It is closed while
  acknowledgements are delivered.
  - `reacknowledge` runs after every step. It owes acknowledgements whenever a step starts at rest
    and ends awake with the same term, leader, last index and commit. That covers a `Wake`,
    another member's (pre-)vote, a stale or failing frame, the early returns in
    `receive_message(AppendRequest)` after `follower()` clears `resting_`, and a stale install.
  - The only exit from rest it does not cover changes the log or the commit under the same leader
    and term. A follower at rest has commit == last index, so only that leader's newer entries can
    do that, and that leader is not at rest.
  - On the leader's side, an unprompted success (this includes a late original flight's answer
    after a timeout or rewind) sets `woke`. Only an answer to a flight issued later can set `told`.
    A flight issued later has `previous` at or past `matched`, so it rests the follower, or the
    older frame arrives after it and counts as stale.
  - A busy follower defers the acknowledgement to `Persisted` (s2). A busy leader drops the
    acknowledgement, and the next re-acknowledgement covers it. When a follower's acknowledgement
    `rpc` collides with a flight's `rpc` (F7), the leader can take it as that flight's answer and
    rest early; the next re-acknowledgement, one heartbeat later, wakes it.
  - I found no remaining way for a leader to rest over a timer for longer than a heartbeat
    interval, plus the follower's persistence and the delivery time, with acknowledgements
    delivered. The sweep agrees: 0 spans of election_min or longer, down from 222.
- **Question 2, cost.** Only `reacknowledge` sends anything new, and only while
  `acknowledge_at_ < maximum`. Two things set it: a follower's `wake()` and a stale step. A write
  wakes no follower (`wakes()`, core.h:542) and grows its log, so it owes nothing. The leader's
  `woke` changes only on unprompted answers. An idle group at rest sends 0 messages (2d65fc, and
  at_rest's 1000 ticks). A write costs the same 8 messages to rest as at main (2d65fc). After a
  wake, every frame from the leader pushes the next acknowledgement a heartbeat interval out
  (core.h:1121), so an awake leader that is replicating gets no extra traffic.
- **Question 3, values.** Each case in s1-s7 asserts a state: the leader at rest or awake, or a
  follower at rest. `quiet()` also measures the exposure in units, checks that the term stays
  unchanged, checks that every replica rests, and bounds the message count at rest at 0. Each
  mutant fails on one of those values (see Evidence), not on a count alone.
- **Question 4, docs/mechanisms/quiescence.md:32-51.** The rule is stated as the code does it,
  with one sentence that overstates the bound (N1) and one older list that is incomplete (N4).
- **Randomized wake fuzz, `fuzz.sh` with `fuzz_wake.cc`, job 261122-211830-abb76a.** It runs three
  cores with random writes, pre-votes from node 9 and `Wake`s. The network delays messages by 0-15
  units, reorders them, and sends late duplicate copies that arrive as stale frames. Each
  persistence takes 0-30 units. The oracles: with no drops, no term moves and every exposure
  stays within a heartbeat interval plus persistence plus three delivery times. A second mode
  drops 2% of messages and only reports. It runs at the head, at dbb22e7 and at e35356a (to show
  it has teeth), and with all five mutants. Results, 300 seeds each:
  - At the head with no drops, 0 seeds fail: 0 term moves over 84,546 leader rests, 100,642
    wakes and 54,093 writes. The longest exposure is 14 units against a 60-unit heartbeat.
  - At the head with 2% drops: 0 term moves. The longest exposure is 132 units, and none reaches
    election_min. 132 units is more than one heartbeat interval, as N1 says, and far below
    election_min (600).
  - Quiesce off (control): 0 term moves.
  - The fuzz has teeth. At dbb22e7, 13 of the first 17 seeds fail, with exposures up to 1035
    units; with drops, 84 exposures reach election_min. At e35356a, 13 of 15 fail; with drops,
    143 exposures reach election_min and one term moves.
  - With no drops, every mutant passes. With drops, wake-ack-once has 34 exposures of
    election_min or longer and a term move, and stale-frame-silent has 1. Each mutant removes
    only one rule; the others cover what remains, except under loss.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | **Closed.** A follower woken while it persists sends its acknowledgement on `Persisted` (core.h:574). Any step that ends a rest without changing the log or the commit owes acknowledgements (core.h:405, 573). Round 1 found 222 sweep spans; this round finds 0. | 261121-215930-dc004e; 261121-215942-13c686; 7eb2c6 s2 | Done. |
| F2 | no | gap | **Closed.** s6 forces the seed-1080 ordering deterministically, and wake-ack-unanswered fails it on a state check. Each new rule has its own mutant, and each fails on a value. | 261121-215936-868140, 261121-215921-70138c, and the other mutant jobs | Done. |
| F3 | no | defect | **Closed.** An overtaken acknowledgement no longer counts (`woke`, core.h:1222, 1231; s4), and a lost one is resent every heartbeat interval (s5). | 261121-215924-1cf081, 261121-215927-4866dd | Done. |
| F4 | no | defect | **Closed, except N1.** The doc now states the rule as the code implements it: who owes acknowledgements, when they go out, until when, and what the leader counts. | quiescence.md:32-51 | N1. |
| F5, F6 | no | nit | Unchanged and still true. at_rest sends 0 messages; s3 rests again within two heartbeats. | 6a75bb | None. |
| F7 | no | nit | **Moot, as the journal says.** A collision can only set `told` early, and the next re-acknowledgement, a heartbeat interval later, wakes the leader. | core.h:578, 1216 | None. |
| N1 | no | defect | quiescence.md:45-48 says the acknowledgement "goes out within a heartbeat interval, whether a persistence deferred it" and that "a leader rests over a running election timer for at most a heartbeat interval". The election deadline keeps running while the follower persists, and the bound leaves out persistence and delivery time. The sweep's longest exposure is 83 ms against a 20 ms heartbeat. That is still below election_min, so liveness holds, but the sentence is not exactly true. | 13c686 (longest 83 ms); abb76a (132 units with 2% drops); core.h:336, 574 | Replace it with: "a leader rests over a running election timer for at most a heartbeat interval after the follower's persistence completes, plus the time to deliver the acknowledgement and the leader's heartbeat, and a further interval for each acknowledgement lost (`quiescence_test`, s1 to s7, with delivery at once)". |
| N2 | no | nit | at_rest prints the 8 messages from a write to rest but asserts only the 0 at rest after. A write that cost an extra round would still pass quiescence_test. The journal's ruling 5 says simulation_test's quiescence::at_rest caught such a variant; I did not check that. | quiescence_test.cc:153-160 | Assert `to_rest` against the value measured at main (8). |
| N3 | no | nit | The mutant wake-ack-dropped-busy delays the acknowledgement by a heartbeat interval; it does not drop it. Its exposure (59 units) stays within the bound, so only s2's state check catches it. That is enough, but the name misleads. | driver_mutants.py; dc004e log | Rename it `wake-ack-deferred-busy`, or leave it. |
| N4 | no | nit | quiescence.md:28-29 lists the events that end a follower's rest as "a frame that leaves it behind its leader's commit". A stale frame also ends a rest (s7); the next paragraph covers it, but this list does not. | quiescence.md:28; quiescence_test s7 | Add "or a stale frame". |

## Not run

- CI run 44991647211: I took the lead's word that it is green.
- Learners, witnesses and five-voter groups beyond reading the code. The sweep's odd seeds include
  witnesses.
- Persistence that takes longer than election_min. The follower can then campaign as soon as the
  persistence completes, as it can without quiescence.
