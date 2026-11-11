# async-drivers-design review: design

Commit: Detent:1ece2ff4ccb07408f9523b273a8f50f34e544aeb
Verdict: CHANGES
Date: 2026-11-10

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

Re-review of the fold of Xapiand's review (X1-X15), Detent:f48f7cb..1ece2ff only. The design was
accepted at f48f7cb; that review's F1-F15 and N1-N9 are not repeated. The fold is mostly sound:
the new placement rule, the close and teardown rules and the blocking scheduler each have a
passing job and a control that fails as it should. Four holes remain, each in a mechanism the fold
added, and each needs a few sentences and one criterion, not a re-plan:

1. Re-partition at restart (X4) drops journal's "lost" verdict (R1).
2. A dialer with a stale view of a peer that shrank L finds no listener, so no hello tells it the
   new L (R2).
3. D5 step 5's new exception rests on a predicate that task cannot evaluate (R3).
4. `close` drains two kinds of waiter and leaves out reads, `create`/`recover`, and the plain
   `receive`/`applied` calls (R4).

## Evidence I relied on

The bench files these jobs build are unchanged from the job commits to head:
`git diff c351fc1..1ece2ff` touches only `waiter_test.cc`, and `9e7237c..1ece2ff` touches only
the design and journal text. I read each log under `~/.scratch/campaign/jobs/<id>/a1/`.

- X2: 261110-042826-5c2da9 (plain), 261110-042827-fb0e1d (ASan+UBSan) and 261110-042829-5f86fb
  (TSan) exit 0 at Detent:9e7237c. Close: 1,796 and 1,014,239 schedules with 60 and 29,000
  `closed` outcomes and no violation. Teardown: 319 and 133,406 schedules with 27 and 6,623
  waiters linked at teardown and no dangling link. The controls break 977 of 1,824 (close-naive)
  and 27 of 319 (teardown-naive) schedules. These match D8's text.
- X3: 261110-031128-58310d and 261110-031130-32585e (TSan) exit 0. In the TSan run the
  cross-thread cancel takes 60.2 ms, the 200 ms deadline 203.0 ms, and the lost wake with no
  token stalls in 0.0 ms. 300 rounds all end `cancelled`, the slowest in 3.4 ms. The
  `inline_scheduler` control drops 1 `post_call` and stalls; 261110-031132-189172 (`inline`)
  exits 1.
- X4: 261110-031134-9f199a exits 0: map for L 1..4,096; share with a worst extra of 1;
  remap over 15/19/20/5/0 crash points; hello. 261110-031136-e8b259 (`mod`) exits 1.
- X5: 261110-031138-999c25 (`lone_voter` on consensus:ab05507) exits 1 as expected; the model's
  `first-election` case takes 0 advances with the fix and 1 (1,500 ms) without it (58310d).

Together these establish the placement arithmetic, the close/teardown waiter protocol in the
single-threaded explorer, the blocking scheduler's cross-thread wake, and the model's lone-voter
rule. They do not cover the cases in R1-R4.

## What I checked myself

The probes are in `.scratch/review-async-drivers-design-design/` in the home checkout, and
`run_probes.sh [name|all]` runs them as one job. It copies the bench into `$JOB_OUT` and adds the
three targets. Jobs: 261110-044744-df2ea9 (all three, at Detent:1ece2ff, light) and
261110-044953-75c0a8 (`token_predicate` again, after I fixed its requires-expressions). Each
probe exits 1 while its finding stands.

- `repartition_lost.cc` (R1) uses journal's real RFTW0001 log at the bench's pins. It runs
  journal's own `damage()` scenario (`test/turn_log_test.cc:158-178`), which loses group 1. It
  then runs step 2 as D10 states it, copying every recovered group's live records into a fresh
  log, and reopens that log. In the old generation, `damaged=1` and `lost={1, 6}`, and an append
  of id 1 is refused. In the new generation, `damaged=0` and 1 is not lost, and an append of id 1
  at sequence 1 is ACCEPTED. Exit 1.
- `shrink_dial.cc` (R2) counts, for each restart that shrinks L, the groups whose stale-view
  target loop has no listener. Of 100,000 groups that is 25.0% for 8->6, 81.2% for 16->3,
  25.2% for 16->12 and 66.6% for 3->1; growing 6->12 gives 0. Exit 1.
- `token_predicate.cc` (R3) compiles against task at the pin and asks whether `cancel_token`
  offers any "a source can still request this" predicate. It finds none of `possible`,
  `stop_possible`, `can_request` or `requestable`. Exit 1 (75c0a8).

Answers to the lead's risk questions:

- **Risk 1 (X4).** The placement rule is sound. The three-step re-partition is crash-safe in
  the model, but the model stores only live records, while the real log also carries a per-group
  `lost` verdict that exists only as damage in the old generation (R1). The hello's
  `(base port, L, loop index)` and the `misrouted` refusal are unchanged and still fine when both
  ends agree. Growing L is fine. Shrinking L has no path to the hello (R2). Anything that still
  assumes a fixed L is wording only (R9).
- **Risk 2 (X2).** Yes for `propose` waiters. The close model resolves every unadmitted waiter
  exactly once, and a cancel racing close is safe. The claim's loser does nothing, and a later
  hop finds the waiter unlinked and touches no group state. The 9e7237c fix is faithful. D3/D6
  completions post back to the group's loop, so counting a write in flight on loop A at issue and
  at barrier is the design's rule, and stricter than "nothing of its own is queued". The
  coverage gaps are the waiter kinds and plain calls the model does not have (R4), and close is
  never run on real threads (R11).
- **Risk 3 (X3).** Steps 1-4 on `blocking_scheduler` are consistent with D5, and the prototype
  shows it. Step 5's exception is not consistent with Limits, and task cannot implement it as
  written (R3).
- **Risk 4.** X5 is precise apart from the witness mismatch with `needs_peer` (R6). X1 is
  precise enough for quest 5's functional tests. Its security wording has two loose ends (R7).
- **Risk 5.** X6-X15 introduce no contradiction with D1-D10 beyond the wording items R5, R8 and
  R9. The X10 Sink rule interacts with close (R4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | gap | D10's re-partition step 2 writes "each group's live records" into the next generation. Journal's recovery also returns a `lost` set: groups with a sequence gap, or with records in a damaged block, that "rejoin as a new incarnation". That verdict lives only in the old generation's damage. The `lost_` set is rebuilt from recovery at each open, and `append` refuses a lost id. A clean new generation therefore forgets it, so after the commit the lost group's old id appends again from sequence 1. Under consensus this is a member with its term and vote forgotten under the same incarnation, which can vote twice in a term. `placement_test remap` models no damage and no `lost`, so its crash matrix cannot see this. | probe 261110-044744-df2ea9 `repartition_lost`; journal:e5f6ff1 `turn_log.h:61-70` (TurnRecovery), `:176` (`lost_` from recovery only), `:509-519`; `test/turn_log_test.cc:172-177`; `placement_test.cc:96-140` | Step 2 carries the recovery's `lost` set durably into the next generation, for example as a lost marker per group in its new log or as a list in the layout record. Without that, step 1 refuses to re-partition a damaged generation. `placement_test remap` gains a damaged old generation: a lost group stays lost after the commit and after a crash at every step. Add both to quest 3's re-partition criterion. |
| R2 | yes | gap | "A dialer whose view of the peer's L disagrees closes with `l_mismatch` and dials again with the hello's L" works only when the dialed loop exists. After a restart that shrinks L (16->3, 8->6), the stale view sends many groups to `base port + j` with j >= the new L. That port has no listener, so the dial is refused and no hello ever names the new L. If another process owns the port, it is worse. `placement_test hello` counts these dials as `misrouted` (`placement_test.cc:220-223`), which is a group-open refusal on a live connection, not this case. The lower node id dials, so a peer whose view is stale stays stuck whenever it has the lower id. | probe 261110-044744-df2ea9 `shrink_dial`; `async-drivers.md:428-431`; `placement_test.cc:212-233` | Name the path. For example: a refused or failed dial falls back to loop 0 (`base port`, present for every L >= 1), whose hello carries L. Or say where the address with L is refreshed and what the dialer does until then. Make `placement_test hello` count unbound targets separately and show the redial reaching the owning loop. Quest 5's `l_mismatch_test` includes a shrink with a stale lower-id dialer. |
| R3 | yes | gap | D5 step 5 waits instead of throwing when "a linked waiter holds a token another thread can still request" (journal ruling 23). task cannot evaluate that. `cancel_token` and `cancel_source` share one refcount, and the only test is whether the token is empty. A real entry can therefore only use "the token is non-empty". A token whose source lives only in the caller's own frame, or that links a process-wide shutdown source, then turns every lost wake into a sleep until shutdown. The bench's `blocking.hh` takes the predicate as a lambda the test supplies, so no job exercises it. Limits still says "the blocking entry throws `stalled` instead of waiting (D5)", which contradicts step 5. Quests 3 and 4 make "throws `stalled` on a lost wake" a criterion over `blocking_scheduler`. | probe 261110-044953-75c0a8 `token_predicate`; task:bdafa5f `task.hh:995-1058`; `blocking.hh` `drive(done, can_wake)`; `async-drivers.md:264-265`, `:578`; `blocking_entry.cc` `blocking(…, requestable)` | Choose one. (a) Quest 1 adds a source count to `cancel_state`, a `cancel_token::possible()` like `std::stop_token::stop_possible()` that counts linked parents. Step 5 and Limits then say that a lost wake under a live source waits until it is requested, and the `blocking_entry threads` case uses the real predicate. (b) Step 5 always throws `stalled`, and a cross-thread cancel wakes only step 4's sleep toward a deadline. Recommendation: (a), which is what ruling 23 intends. Either way, Limits matches step 5. |
| R4 | yes | gap | Close rule 2 and the D8 row "waiting when its group closes" name two kinds of waiter, "not yet admitted" and "staged". D8's own table has others that are neither: a read awaiting its read index or the apply that reaches it, and a `create`/`recover` waiter in progress. A literal close never resolves those waiters. Its rule "frees … once nothing of its own is queued" then either never frees the group (the waiter stays linked) or frees it under them. The plain calls stay on `Group&` (ruling 20). Under X10 the application calls `applied(Group&)` later through `post_call`, and the transport calls `receive(Group&)`, so either can land after close freed the state. The close model has only `propose` waiters and no `applied`. | `async-drivers.md:103-109`, `:118-121` (Sink, X10), `:322-329`, `:358-363`; `waiter_test.cc` `group_model` | Rule 2 resolves every waiter that holds no staged record with `closed`: unadmitted proposals, reads at any stage, and `create`/`recover` once its current durable step ends. Only waiters with a staged record drain. `receive` and `applied` either take `GroupId` or name the table entry that outlives the state, and a closed group drops them and counts the drop. Say whether close waits for the one `Committed` range the application holds. `waiter_test close` gains a read waiter and a late `applied`. |
| R5 | no | nit | The D8 row "staged: completes when its record commits, as row 2" fits close, but not shutdown, where rule 3 cancels every waiter. Row 2 also completes the caller at once with `cancelled`, not at commit. | `async-drivers.md:328`, `:364-365` | Split the row. On close, a staged waiter completes at its commit. At shutdown it is `cancelled` and its record is still written, as row 2. |
| R6 | no | gap | `needs_peer` fires on "a voter other than this node". A witness votes (`MemberRole::Witness`) but is not a `Voter`. A lone voter with a witness therefore passes the `needs_peer` check, X5 does not campaign at Start because a witness is present, and the entry ends in `stalled`, not `needs_peer`. | consensus:ab05507 `types.h:70-77`; `async-drivers.md:266-267`, quest 4 | Use one predicate for both: "a voting member (voter or witness) other than this node". |
| R7 | no | gap | The X1 rule accepts a frame naming "a group and destination incarnation the loop holds", while the X8 rule opens a stored, closed group on a frame. "Holds" should read "stores". Core treats `Receive::authenticated_peer` as authenticated (`core.h:756-759`), but node-level TLS now authenticates only the node, and nothing ties a frame's source incarnation to it. | `async-drivers.md:466-474`; consensus:ab05507 `core.h:756-759` | Say "stores". For quest 5's security review: where the membership names the source incarnation, its node must be the authenticated node; an unnamed incarnation (a joiner's leader) is admitted as now. |
| R8 | no | nit | D4's heading still says "the one timer mechanism", against X7's list and Q6 (b). D3's "three seams" still lists the wheel, which `Seams` no longer carries (X14). | `async-drivers.md:140-141`, `:155`, `:97` | Reword both. |
| R9 | no | nit | Leftovers from the fixed-L rule. Quest 5 says "a peer restarted with a different L is refused with `l_mismatch`", but D10 now redials. `TurnDriver // one per core` should name the loop. Quest 4 names "first proposal after `recover`", while `lone_voter` runs `create` then `Start` and checks leadership. | `async-drivers.md:89`, `:428-431`, quests 4 and 5; `lone_voter.cc:62-75` | Align the wording. `lone_voter`, or quest 4's test, covers both `create` and `recover`. |
| R10 | no | nit | "returns `cancelled` within 55 ms of the request's 50 ms, … under TSan". The TSan job measures 60.2 ms, and the plain job 51.2 ms. | 32585e, 58310d | Quote the measured numbers. |
| R11 | no | gap | The TSan job's `threads` mode never closes or tears down. Close and teardown are explored only on the single-threaded manual loops, so no sanitizer run sees close racing a cancel on real threads. | `waiter_test.cc` `threads()` | Quest 4's close criterion adds a threaded close-versus-cancel run under TSan, beside the ASan explorer. |
| R12 | no | gap | Keepalives run once per node pair (X12), but each loop pair has its own connection. A half-open loop-pair connection is then found only when its own traffic times out, and with Q4's quiescent groups that may be never until a frame is sent. | `async-drivers.md:446-449` | Quest 5 says what detects a dead loop-pair connection, and tests it. |
| R13 | no | nit | RFTW0001 finds its segments by scanning one directory for `turns-<n>`. Logs named by (generation, loop) under one per-host directory (Q3 (b)) therefore need a naming change or a subdirectory per log. | journal:e5f6ff1 `turn_log.h:92-104`, `:376-378` | Quest 3 names which. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- No mutants of the new waiter code: the contributor's controls (close-naive, teardown-naive) and
  the F14 mutants at Detent:672e7b7 stand, and the paths those mutate are unchanged.
- I did not test R4 as a model case: it follows from the text and from the model having no read
  waiter. R12 and R13 come from reading only.
- No Linux numbers. Detent's owner-loop migration (quests 6-7) is outside this diff.
- A8 (Xapiand's re-review of this fold) is the Xapiand lead's, not this lens.
