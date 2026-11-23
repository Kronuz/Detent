# consensus-quiesce-election-stall: journal

State: waiting on jobs 261122-220103-9e4235 261122-220104-c91461 261122-220105-51067d 261122-220105-a4c641 261122-220106-1794eb 261122-220106-91745d 261122-220146-4f043d
Quest: .campaign/detent/quests/consensus-quiesce-election-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 (review CHANGES: F1, F2, F4) at consensus:ab47d8a, pushed; consensus main (e35356a) merged at consensus:e2743bf. Code done; only jobs and CI remain. Then: fill Evidence, State "ready for review".

The rule now (core.h `reacknowledge`, receive_message(AppendResponse)):

1. Follower: when its rest ends and it keeps its leader and term, on a wake or on a stale frame (adds nothing to its log or commit), it owes acknowledgements: at once on a wake (or as soon as its persistence completes), then after every heartbeat interval with no frame from that leader, until a frame rests it. `acknowledge_at_` holds the next one (maximum: none owed); next_deadline includes it.
2. Leader: an unprompted success clears that peer's `told` and records `woke` (the last token issued); only an answer to a flight sent after it (`rpc > woke`) sets `told` again.

Why it closes the class (a leader at rest over a follower's running election timer): a leader rests only once every replica answered a frame that rests it. A follower awake under it left rest after that answer without leaving its leader, by a wake or by a later frame from that leader. A frame that adds to its log is the leader's newer one, so that leader is awake and replicating and rests again only on an answer to a later resting frame; a wake or a stale frame owes acknowledgements (1). Each goes out within a heartbeat interval, wakes a leader at rest, or keeps an awake one from resting until it heartbeats the follower (2). So with acknowledgements delivered, the leader rests over a running timer for at most a heartbeat interval: busy (s2), overtaken (s4), lost and resent (s5), stale frame with its answer lost (s7). Only acknowledgements lost for a whole election timeout let the follower elect, as lost heartbeats do for an awake follower.

## Next

0. Landing prep: main merged cleanly at consensus:ca24f80 (no conflict; main added peers-wake and touched CMakeLists.txt, so jobs reran) and pushed. Jobs above rerun at it; the 5 duplicate reruns 261122-220050-601d23 261122-220051-367bc6 261122-220051-47dfa1 261122-220052-2d9294 261122-220052-cd8967 were cancelled (they repeated the same commands).

1. When the jobs finish: fill Evidence and the review rows; State "ready for review".
2. CI run 44991647211 at ab47d8a.

## Fixes since the review

| Finding | Fix | Test | Evidence |
|---|---|---|---|
| F1 (busy follower) | Follower re-acknowledges on a wake as soon as its persistence completes, then every silent heartbeat interval until rested (consensus:00a59a8, ab47d8a) | quiescence_test s2; mutant wake-ack-dropped-busy | 261121-215930-dc004e (mutant, --fails-without); 261121-215907-6a75bb, 261121-215904-ed1f54 (pass) |
| F3 (lost / overtaken acknowledgements) | Periodic re-acknowledgement (s5); leader counts only answers to frames sent after an unprompted acknowledgement (s4) | quiescence_test s4, s5; mutants wake-ack-overtaken, wake-ack-once | 261121-215924-1cf081, 261121-215927-4866dd |
| F1 class: stale frame | A stale frame that ends a rest owes acknowledgements too | quiescence_test s7; mutant stale-frame-silent | 261121-215933-7dd2bc |
| F2 (1080 caught by accident) | s6 (the exact 1080 ordering) as a deterministic case; the old rule's mutant updated | quiescence_test s6; mutant wake-ack-unanswered | 261121-215936-868140 (s6 alone, --fails-without), 261121-215921-70138c (whole test) |
| F4 (doc claim false) | quiescence.md, Rest: "A follower that leaves rest under its leader re-acknowledges" and "A follower's running timer is fed", the rule and the bound stated exactly | quiescence_test s1-s7 check the bound (exposure <= heartbeat) | consensus docs/mechanisms/quiescence.md |
| F7 (token collision) | Moot: a collision can only set `told` early; the next re-acknowledgement repairs it | s1-s7 | none needed |
| Reviewer's checks | sweep 77ca8b and probe s1-s6 rerun at the fix | | 261121-215942-13c686 (exposure), 261121-215938-7eb2c6 (probe; its s5 immediate check cannot pass under any rule: the leader receives exactly what it would from rested followers, so s5's pass is the quiet check) |
| Idle cost | at_rest: messages from a write to rest and in 80 election timeouts after, core.h at main e35356a, dbb22e7 and the fix | | 261121-215945-2d65fc; local at the fix: 8 to rest, 0 after |

## Blocked on

Nothing.

## Campaign tool notes (for campaign-v3)

1. `campaign brief` lists the older jobs at dbb22e7 (261121-193329-*, 261121-193330-*) as evidence that "ran other code" next to their identical successors, so a rerun of the whole list queues duplicates (same cmd, same commit); `job rerun` does not dedupe an identical job already queued, and a cancelled job still shows in the generated Evidence table.
2. A2 (untagged, only `--fails-without` jobs) showed "jobs: ;" and no evidence until a plain job was tagged `--for A2`: the message does not say a passing job is needed beside the mutants.
3. A5 "CI passes" names no CI job, so the brief says "names no job" even with a green run: the row needs `ci:NAME` per job, the names are not in the quest text.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | At consensus:ca24f80 (ab47d8a merged with main ca24f80; main changed CMakeLists.txt, so every job reran): 261122-220103-9e4235 (named seed 1080 at rest), 261122-220104-c91461 (1-3000 at rest, Release), 261122-220105-51067d (ASan+UBSan), 261122-220105-a4c641 (TSan). Local at ab47d8a: 1080 ok; 1-250 at rest ok, longest leaderless 183 ms |
| A2 | Passing at consensus:ca24f80: 261122-220146-4f043d (seed 1080 at rest, then quiescence_test, `--for A2`). `--fails-without`: 261121-141023-81b8cd at the pre-fix commit consensus:de24723, seed 1080 at rest exits 1 "no leader after settling". At ab47d8a, per mutant on quiescence_test: 261121-215921-70138c (wake-ack-unanswered, the old rule), 261121-215936-868140 (the same, s6 alone), 261121-215924-1cf081 (wake-ack-overtaken), 261121-215927-4866dd (wake-ack-once), 261121-215930-dc004e (wake-ack-dropped-busy), 261121-215933-7dd2bc (stale-frame-silent), 261121-141837-c1601b (wake-ack-unanswered, seed 1080). Each was caught locally first |
| A3 | At consensus:ca24f80: 261122-220106-1794eb (TSan ctest: quiescence, simulation, driver, driver_close*, liveness, driver_replace parts), 261122-220105-a4c641 (replace 1-3000 under TSan), 261122-220106-91745d (quiescence_test under ASan+UBSan). Local Release at ab47d8a: quiescence_test ok, simulation_test 200 seeds ok |
| A4 | consensus docs/mechanisms/quiescence.md, Rest, "A follower that leaves rest under its leader re-acknowledges" and "A follower's running timer is fed" |
| A5 | CI run 44991647211 at consensus:ab47d8a, all 8 jobs green (carried across the clean merge of main at ca24f80): ci:linux-tsan, ci:linux-asan-ubsan, ci:freebsd-x86_64, ci:macos-arm64, ci:macos-x86_64, ci:linux-arm64-gcc, ci:linux-x86_64-gcc, ci:linux-x86_64-clang |

Other jobs: 261121-134216-fb35b7 (failed, as intended): the reviewer's quiesce_trace.sh at consensus main 8780e44 reproduces seed 1080's "no leader after settling".

## Result

consensus:ab47d8a (on consensus main e35356a). core.h: `reacknowledge` (a follower that leaves rest under its leader on a wake or a stale frame re-acknowledges its durable log until a frame rests it; `acknowledge_at_`, in next_deadline), and the leader's `Peer::woke` (an unprompted success clears `told`; only answers to later flights set it). Docs: docs/mechanisms/quiescence.md (Rest: the re-acknowledgement rule and the fed-timer bound), CHANGELOG.md (own bullet, 2026-11-21 Fixed). Tests: test/quiescence_test.cc (ctest consensus_quiescence: at_rest, s1-s7), `driver_replace_test [seeds] [first] quiesce` and its ctests. Mutants: test/driver_mutants.py `wake-ack-unanswered`, `wake-ack-overtaken`, `wake-ack-once`, `wake-ack-dropped-busy`, `stale-frame-silent`. Job script: .scratch/detent/consensus-quiesce-election-stall/idle.sh (home).

## Rulings I made

1. The cause is a leader resting over an awake follower, not a follower missing a silent leader: the fix is on the leader's side (answer the wake acknowledgement), the follower's rules are unchanged.
2. Any unprompted success clears `told` (also late or duplicated answers): it costs at most one extra heartbeat round, and tells a woken follower's acknowledgement from a stale one without a wire change.
3. A2's pre-fix commit is consensus:de24723 (the test mode, no fix); the persistent mutant is driver_mutants.py's `wake-ack-unanswered`, run at head.
4. Election bound in the oracle: 3 x election_max (600 ms) without a leader while settling; measured max 329 ms at rest, 236 ms plain.
5. (Round 1) F1's fix is the reviewer's follower-side rule, widened to every way a follower leaves rest under its leader except a frame that adds to its log (a stale frame included), plus a leader-side ordering rule (`woke`): the leader counts only answers to frames sent after an unprompted acknowledgement. Owing acknowledgements after a log-growing frame was tried and dropped: every write then cost an extra heartbeat round before rest (simulation_test quiescence::at_rest failed: "replica 1 rests again").
6. The re-acknowledgement is silence-based: a frame from the leader pushes it a heartbeat interval out, so an owed follower of a replicating leader sends none.
7. quiescence_test is a new file (test/quiescence*), not simulation_test's quiescence namespace, which is outside the file set. It is a ctest (consensus_quiescence) and takes CONSENSUS_DRIVER_MUTANT.
8. The bound the tests check is "the leader rests over a running election timer for at most a heartbeat interval", plus no term moves in ten election timeouts, every replica rests again, and 0 messages at rest after; s1, s2, s4, s6 also check the leader does not rest at all on that ordering.
