# authority-pages-design review: design

Commit: Detent:12608a08
Verdict: CHANGES
Date: 2026-11-24 (round 2; the re-review at Detent:28ca4c31 and round 1 at Detent:65576604 below)

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Round 2 at Detent:12608a08

R1-R4 and R6 are fixed, and the strip mutant fails for R1's reason. R5 is fixed in intent, but rule
3's clock measures the wrong thing. The model shows it misfiring on every idle namespace, and the
doc's wording allows a second reading under which R5 stays open (T1). One cost the fix round added,
a separate root-entry proposal four times a second, is neither specified as free nor measured (T2).
The doc still has three unfilled slots for pass4, the decided pool beside the journal (T3).
`261124-111623-b18370` is still queued, so I did not read it. All three blocking findings are text
or one queued job. Option 2's shape is sound.

### Evidence I relied on

- The doc and journal at `12608a08` (the doc's last change is `67b76b6c`), and `root_model.py` and
  `test_root_model.py` there.
- The jobs the journal names at `67b76b6c` were all queued when I read them: the R1 strip mutant
  `261124-111650-5bc59c`, the tests `-111638-e08fc4`, pass4 `-111623-b18370` and the suite
  `-111629-fea86a`. So I ran the model's tests myself (below) and relied on no result of theirs.

### What I checked myself

Under `quiet light`, from `.scratch/detent/review-authority-pages-design-design/r2/` (`run_all.sh`
runs everything; the model files are copies from `12608a08`):

- **The tests pass, and the strip mutant fails for R1's reason.** The five cases pass. With
  `ROOT_MODEL_RULES=strip`, three fail. The two divergence cases fail because the agent publishes
  the relay's wrong records (`take` returns true), which is R1. The rootless case fails because the
  agent publishes a rootless answer. The mutant changes two rules at once, so I split it in two.
  Relay-only strip (`relay_only/`) fails two cases, but only on the failure counter, (0, 0) for
  (0, 1): the agent's rule 2 alone keeps the wrong state unpublished. Agent-only rootless
  (`agent_only/`) fails the rootless case. Each half is killed, and safety needs either rule, not
  both.
- **Rule 3's clock** (`probe_clock.py`):
  - P1: a namespace verified at t = 1 s and idle until t = 12 s is reported `unverified` with
    nothing unverified published. Its first write after the idle gap (t = 21 s) publishes nothing
    until the next root arrives.
  - P2: a builder stall publishes tails until 11 s after the last check, then holds the position
    (14) with `unverified`. When roots resume, it catches up to the head (17) in one answer.
  - P3: the clock is the last check, not the age of the oldest unverified change.
  - P4: a new namespace (or a new incarnation) publishes nothing before its first root.

### R1-R6

| ID | Fixed | Where, and what shows it |
|---|---|---|
| R1 | yes | Agent and relay rules 1-2 and the relay row of the root path table: a relay never strips a root, and an agent publishes nothing on a missing, stale or failing root. The strip mutant fails for this reason, and each half of it is killed (above). |
| R2 | yes, with T2 | "The builder and the service loop" covers input (pending lists, a `persistent_map` snapshot), encode on the pool, an output queue drained once per turn, memory and scheduling. The cost of its one proposal per round is T2. |
| R3 | yes | "Step-down": a cancel flag, the queue and the result in hand dropped, heads freed, at most one cold build at a time. The step-down tests are named for authority-builder-pool. |
| R4 | yes | Rule 4: the agent keeps what it published, counts the failure, takes a full copy from another source and checks it first, and reports the namespace unverified. The model's wrong-tail case covers it, and Limit 1's row now reads "detection, not retraction". |
| R5 | partly: T1 | Rule 3 exists and the model's lost-roots case trips it, but its clock misfires on idle namespaces (P1), and the doc's text admits a reading under which a relay that keeps sending its last root never trips it. |
| R6 | yes | "The root entry": no position, kept beside the head, carried by a snapshot and a cut, rides D1's header field, stale entries ignored. |
| N1-N4 | N1 waits on T3; N2-N4 yes | pass4 in the paced harness; the scheduling sentence; relays build at rooted positions; "1.7-1.9 times". |

### What a user sees under rule 3 (for the owner)

Rule 3 makes option 2 fail closed: delivery of a namespace now depends on roots arriving, where
before it depended only on records. Per failure, with the clock as T1 fixes it:

| Failure | Who | What a user sees |
|---|---|---|
| Builder stall on the leader (pool stuck, starved or crashed; commits continue) | every agent of every namespace with writes, fleet-wide | values keep updating for 10 s (unverified), then freeze, with health showing the namespace unverified, while the authority keeps accepting writes. Asking another source cannot help: no source has a root. When roots resume, everything catches up in one answer (P2). Today, and under rule 2 alone, values keep updating unverified. |
| A relay loses root entries (mirror bug) | agents below that relay | the same freeze after 10 s, for that relay's namespaces only. Another relay with roots restores them at once, provided it is reachable. |
| Authority partition, leader in the minority | everyone | no commits, so no new values under any rule. Under the clock as written, every idle namespace is also flagged unverified and asks another source after 10 s (P1): a fleet-wide false alarm and a request burst. |
| Authority partition, new leader in the majority | everyone with writes during the gap | values keep updating unverified until the first root, if that comes within 10 s; otherwise a freeze until the new leader's first root. |

**Is 10 s derived?** No. It is "the window's 2.1-6.2 s with margin", but the time between two checks
across an election is a sum of several parts:
1. The last root's age when the leader fails (root lag p99 0.33 s plus its commit, p99 0.31 s).
2. The election timeout, 1-2 s (`consensus/types.h:114-115`).
3. The window, 2.1-6.2 s at 5M on this host, which grows with the store and shrinks with the pool.
4. The new root's commit, up to 0.31 s.
5. Two relay tiers' mirror delay and the agent's poll interval, neither measured.

Parts 1-4 come to 4.4-9.2 s, so in the strict four-thread case 10 s leaves 0.8 s for part 5. A
larger store, or a pool starved on a small host (one thread took 11.6-15.1 s), freezes visibility
at every election. The owner's options, with the numbers above:
1. Keep 10 s, stated as tuned to 5M on this host.
2. Derive the bound from those parts at the deployment's store size, as configuration.
3. Tie it to the measured root-lag metric (a multiple of its p99 and of the last window).
4. Drop rule 3 and keep rule 2's unbounded unverified tail, which leaves R5 open (a relay that
   loses roots goes unnoticed).

None of these is blocking; the choice is the owner's.

### Sweep: the lifecycle's states and rule 3's clock

- **Reachable and tested.** The agent and relay rules are modeled and tested: five cases, plus my
  four probes. The authority's states F, LC, LS and LB are reached by the harness's lag mode
  (`cold` is LC to LS; `warm` is LS, and LB when a round outruns its interval, as with one thread).
  Step-down from LC or LS, a root entry lost at a term change, a stale incarnation, and a snapshot
  install carrying the latest root are reached by nothing in this quest. They are text, with tests
  named for authority-builder-pool and authority-root-entries. For a design that is acceptable; the
  implementation quests must carry those tests (U4).
- **Transitions that skip or misuse the clock.**
  1. Idle time runs the clock with nothing unverified (P1, T1).
  2. Re-checking the same root resets the clock under one reading of the doc (T1).
  3. An agent restart: the doc does not say whether v, the tail past it and the clock persist. A
     restart that resumes with its published state as v skips the clock for the tail, although the
     next root still detects a wrong record, since it covers the whole state (U2).
  4. A repair resets the clock at a checked root, which is right.
  5. A switch to another source whose newest root is older than v publishes nothing, which is right.
  6. A new incarnation has no root until its first pass (P4). That is a visibility cost, not a skip
     (U3).

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| T1 | yes | defect | Rule 3's clock runs from "its last passing check". In the model that is the last check of a newer root. An idle namespace therefore trips the bound with nothing unverified: health reports it unverified and the agent asks another source every 10 s of idleness, on every idle namespace of every agent. The first write after 10 s idle also publishes nothing until its root arrives (P1), so rule 2's "no visibility cost" fails for low-write namespaces, which pay the root lag plus its commit (p99 about 0.64 s against the 1 s target through two tiers). The doc's text also admits the other reading, where re-checking the held root counts as a passing check. Under that reading a relay that loses root entries keeps sending its last root, and the clock never expires, which is R5 again. | `probe_clock.py` P1, P3; `root_model.py` `take` (`verified_at_s` set only when r > v); doc line 117 | The clock is the age of the oldest change the agent published past its last verified root. It starts at the first unverified publish, stops when a root covers that change, and is never reset by re-checking the held root. Test (agent-root-check, and the model now): P1's idle case publishes its first write at once and stays verified, and the lost-roots case still trips the bound. |
| T2 | yes | gap | Root-entry proposals have no stated cost. The handoff makes one root-entry proposal per round, four a second ("The builder and the service loop" item 3). "Why roots go through the log" says they are "carried by group commits already in flight", and Limits 6 estimates their cost by size. The loop admits one prepared proposal at a time (`service.cc:3290`, the `preparing` slot), and on this host the journal pays per durable sync (133 ms p50), not per byte. If each root entry is its own entry with its own sync, four a second take about 0.5 s of sync time a second from the group commits. Neither the paced rows nor pass4 propose root entries. | doc lines 124, 128, Limits 6; `service.cc:3255-3297` | Say which it is. Root entries ride in the next transaction group's entry (a field of the batch, no extra sync), or they are separate entries, measured in a paced row with root proposals beside the journal. Test (authority-root-entries): that row inside the A/A band, or a batch carrying a root entry decoded and applied. |
| T3 | yes | gap | ⟨PASS4-DELTA⟩, ⟨PASS4-JOB⟩ and ⟨SEAL-LAG⟩ are unfilled in the Recommendation, the Decision's facts and option 2, and in Limits 7. These are the numbers for the decided pool beside the journal (N1, C2). `261124-111623-b18370` is still queued. | doc lines 96, 203, 242, 248, 253 | Fill them from `b18370` (`check.py pairs`) and read pass4's delta against its A/A band. If it falls outside the band, Decision option 2's line changes before the owner reads it. |
| U1 | no | gap | Rule 3's "asks another source" has no jitter or rate, and in a builder stall or a partition every agent below every relay asks at once while no source has a root. That works against SCALE_TARGETS' storm control. | doc line 117; `docs/SCALE_TARGETS.md` storm control | Jittered and rate-limited; a records answer, not a full copy, unless a check failed. |
| U2 | no | gap | Rule 2 keeps the state at v and the records since. The doc neither prices that on agents (a second generation sharing pages, plus a records log) nor says what persists across an agent restart. A restart that resumes with the published state as v resets rule 3's clock for the tail. | doc line 116 | Persist v, the tail and the clock's start, or treat the tail as unverified from the restart; one line pricing the second generation. |
| U3 | no | gap | A new namespace or incarnation is invisible until its first root (P4), so its first values pay the root lag (p99 0.33 s plus commit). Rule 2's "no visibility cost" should except it. | `probe_clock.py` P4 | One sentence. |
| U4 | no | gap | Step-down, a root entry lost at a term change, stale incarnations and a snapshot install are reached by no model or harness here. Their tests are named for the implementation quests, which must carry them. | doc lines 165-174, 229-232 | Keep them as acceptance criteria when the lead writes those quests. |
| U5 | no | gap | The pending lists grow without bound during a builder stall (2,000 records/s against "times the root lag"). | doc line 125 | A bound, past which the namespace is cold-built again. |
| U6 | no | nit | Rule 3 says "elections faster than the window"; it means elections whose window runs longer than the bound. D133's row still cites `namespace_relay.cc:413` and `coordinator.cc:685-692`, which are 414/426 and 683-692 at this head. | doc lines 117, 224 | Reword; update the line numbers. |

### Not run

- `b18370` (pass4), the strip mutant job, the tests and the suite at `67b76b6c` were still queued;
  the model results above are my own runs of the same files.
- No strict or exclusive probe. Root-entry proposals (T2) were not measured.
- Reviewer ran as a subagent of the lead, not a session titled `detent · reviewer
  authority-pages-design design`.

## Re-review at Detent:28ca4c31 (CHANGES)

Every round-1 finding is fixed, and the numbers the doc cites match their jobs. What blocks now is
the mechanism the fix round added: the root entry, the split answer and the window rule have no
lifecycle, and one rule in it, the relay's answer to its own divergence, reopens Limit 1 exactly
where the root should close it. All six blocking findings are text in the doc, each with the test
the implementation quest that owns it must carry. The owner's decisions (D156, D157) stand; none of
these findings reopens the choice of option 2.

### Evidence I relied on

- I re-read the summary lines of every job the doc's new rows cite (script
  `.scratch/detent/review-authority-pages-design-design/summaries.py`): steady and burst lag
  `261123-193520-a9b30c`, `261123-193523-d6ce25`; window `261123-193525-e60802`,
  `261123-193528-1675da`; exclusive `261123-193531-962367`; paced pairs `261123-193517-519b4d`
  (exclusive), `261123-193507-f0eafa`, `261123-193510-65f196`, `261123-193513-95a0ae` (strict).
  Every cell of the LAG, WINDOW and paced tables matches: 1.34 / 1.75 cores, p99 328.1 / 564.2 ms,
  max 502.4 / 737.2 ms, the burst's worst second 669.9 ms; windows 6.2 / 3.3 s on four threads
  (`caught_up_ms` 6,203.8 / 3,345.3), 2.7 / 2.1 s on ten exclusive, peak RSS 995 / 1,185 MB; pairs
  pool -0.2 [-1.2, 0.7], aa -0.2 [-1.1, 0.7], write +37.1 [34.8, 39.4], none at 462 tx/s.
- The eight reruns at Detent:b5ce810d (`261124-104556-e167d6` to `261124-104600-0c7e60`) ended
  cancelled (e167d6 in its build, exit 143), so none produced a number to compare.
- PLAN D156 and D157 match the doc's Decision section.

### Round-1 findings

| ID | Fixed | Where, and what shows it |
|---|---|---|
| F1 | yes | "Commit latency and elections": the first rows' 0.09-0.23 seal/s is stated and their deltas are not used; the paced rows hold 0.91 seal/s median against an A/A arm; persisted pages are ruled out on +37.1 ms [34.8, 39.4] (`519b4d`), clear of the band, with both strict medians agreeing (+39.9, +40.2); `24d1dd` cited for saturation. Verified in the jobs. |
| F2 | yes | "After a leader change": the window measured end to end (`e60802`, `1675da`, `962367`), RSS per pool size, the two agent rules and Limit 1's wording under each. Verified. |
| F3 | yes, with R1 | "Where the root comes from": a row per path, roots as log entries (answers followers and unrooted positions), relays never fill the root from their own build, the threat model stated and decided (D157). Its "differs" cell is R1. |
| F4 | yes | Pass-through specified, priced and deferred; the authority serves records and roots, not pages (priced at about 500 MB at 5M); (b) restated as an added cost; Q4's row consistent with it. |
| F5 | yes | "What it changes": Limit 1, D1, D4, D6, D7, Q4, D34 (reason now the service loop), D133 with `namespace_relay.cc:414,426` and `coordinator.cc:683-692`, D145. |
| F6 | yes | Coalescing paragraph, LAG table, pool bounded at four threads. |
| F7 | yes | 971-2,650 ms over four jobs. |
| F8 | partly | Strict ranges stated beside every exclusive CPU number; the exclusive reruns at the final head were cancelled before running. Non-blocking, as before. |
| F9-F11 | yes | Line 52, Decision option 5 and alternative 3, Limits 4. |

### Option 2's lifecycle

The doc has no lifecycle table for the decided mechanism. This is it, per namespace, with the doc's
answer or the finding that names the gap. Roles: F follower, LC leader cold-building, LS leader
passing, LB leader behind (a round longer than its interval).

| State | changes commit at p | pass interval | root built for p | root entry committed | leader change | agent's check fails |
|---|---|---|---|---|---|---|
| F | applies them; no pages | none | none | applies it into history; serves it under D145 | elected: becomes LC from its state at a committed position c (snapshot and input: R2) | not involved |
| LC | commits continue; changes after c wait for the first pass (doc) | no pass until the cold build ends (doc) | the cold build is the base, not a root; the first pass after it gives the first root, 2.1-6.2 s (doc) | an entry of the old leader that commits in this term is accepted: roots are of committed prefixes, so deterministic (doc, line 162; not said for this case) | stepped down mid-build: R3 | agents in the window publish unverified and check at the first root (doc, rule 2) |
| LS | queued per namespace (doc) | one pass per namespace with pending changes (doc); order when passes outrun four threads: N2 | handed to the loop and proposed as one entry per round: the handoff is R2 | relays forward it, a relay checks its own build at p (doc); on a mismatch it strips the root: R1 | stepped down: pool, head and uncommitted entries: R3 | refuses, asks a repair; what stays visible and from whom it repairs: R4 |
| LB | coalesced into the next pass (doc) | the interval stretches to the pass time (implied by "turns excess load into root lag") | as LS | as LS | as LS | as LS |
| any | | | | an entry not committed at a term change: no root at p, the next pass names a later position (doc) | | no root reaches the agent for longer than the lag bound: R5 |

Every cell that names R2-R5 is an event the four quests must handle and the doc leaves to them. The
root entry's own place in history, which three of the four quests consume, is R6.

### What I checked myself

- `261124-105108-776fa1` (light, at `28ca4c31`, `many-namespaces.sh`): root lag and pool CPU for 5M
  keys over 10, 100 and 1,000 namespaces, four threads, 250 ms passes, 2,000 changes/s. Passes
  1,037 / 11,003 / 51,822 (1,727 a second at 1,000), pass p50 52.8 / 4.8 / 1.0 ms over 52 / 5 / 1
  changes; cores 1.84 / 1.90 / 1.89; lag p99 1,073 / 643 / 453 ms, max 1,722 / 932 / 786 ms; peak RSS
  881 / 564 / 558 MB. Light reads high (the 10-namespace row's p99 is 328 ms strict in `a9b30c`), but
  the shape holds: more, smaller namespaces cost about the same CPU and no more lag, because a pass's
  floor shrinks with the tree (1.0 ms at 5,000 keys).
- The doc's idle rows, for comparison: 16 changes over ten 500,000-key namespaces cost 33.8 ms of CPU
  (`261123-073248-5fdf61`, strict), 3.4 ms per namespace pass.
- The relay's answer path (`namespace_relay.cc:380-430`) and the agent's records check
  (`coordinator.cc:683-692`, which throws and requests a full copy on a mismatch and publishes
  nothing for that namespace in the cycle), and dense-distribution D1, D6, D7 and D9.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | defect | A relay that detects its own divergence strips the root. The table's relay row says that when its build at p differs from the authority's root "it serves that namespace as records with no root", and the agent row publishes a zero root "as today, unverified". So the one case the root exists for, a relay whose mirrored records are wrong, is the case where agents below it stop checking: they publish the relay's wrong records unverified until the relay's rebuild finishes, and an agent's repair from that relay arrives the same way. Forwarding the root is never worse: if the relay's records are right (a builder bug), agents building them pass the check; if they are wrong, agents refuse them. | doc line 107-108; `coordinator.cc:683-692` | A relay forwards the authority's root in every frame whose target is a rooted position, whatever its own build says, and on a mismatch refuses its pages there, counts it, and rebuilds; or it stops answering that namespace until rebuilt. Test (relay-authority-root, agent-root-check): a relay whose mirror applies one record wrong still sends the authority's root, and the agent refuses the state and publishes nothing for that namespace; a mutant that strips the root lets the agent publish. |
| R2 | yes | gap | The pool's threading against the service loop and journal is not specified. The doc says "off the service loop" and "below the service loop's priority" only. Unstated: what the pool reads (the applied state at a committed position, by a `persistent_map` snapshot or a copy of each pass's changes) and who encodes the records body the builder consumes (the harness encodes it; the cold encode at 5M took 581-1,760 ms, `519b4d` and `5fdf61` `cold` lines, so it cannot run on the loop); how a built root returns to the loop to be proposed (the node admits one proposal per service call, journal ruling 10) and what the loop does when the proposal fails; and what the pool pins in memory between passes. Limits 4 says contention with apply is not modeled; the design has to say what there is to contend for. | doc lines 20, 105, 166, Limits 4; `state.hh` (`persistent_map.hh`) | One paragraph naming the input (snapshot at the applied index, per-namespace change lists since the last pass), where the encode runs, the output path (a queue the loop drains once per turn into one proposal per round), and the memory each holds. Test (authority-builder-pool): deterministic interleavings of apply, pass and proposal under TSan, and a service-loop gap bound with the pool cold-building (the paced row's `worst_service_gap_ms`). |
| R3 | yes | gap | Leader change is specified only for the new leader. A leader that steps down mid-pass or mid-cold-build: does the pool stop, is the head (135-243 MB, 1.0-1.2 GB during a cold build) dropped or kept applying for a re-election, and what happens to its root results not yet proposed? The window table assumes one clean election; repeated elections (flapping, a leader restart loop) each pay the cold build and its peak RSS, and a deposed leader still building competes with the new one on a shared host. | doc lines 116-130, 159 | Add the step-down row: cancel the pool at the role change, drop results not yet proposed, free the head (or keep it, priced), and say what bounds repeated cold builds. Test (authority-builder-pool): step down during a cold build and during a pass; no root entry is proposed after the step-down and the memory returns. |
| R4 | yes | gap | "Agent's check fails" stops at "else asks a repair". Under rule 2 the agent has already published the unverified state r_prev→h it now finds wrong; the doc does not say what stays visible (that state, or a regression to the last verified one), where the repair comes from (D9's "one other source", or the same relay, which serves the same records), or what the agent does when every source fails. "Bounds how long a relay's wrong state can stand before every agent below it refuses it" is true of new publications only: the values already published stay until a repair succeeds. | doc lines 108, 135-137; dense-distribution D9 (`:244`) | State the agent's rule: it keeps its published state (no position regression), counts the failure, takes the full copy from another source per D9, and reports the namespace unverified until one passes; restate Limit 1's row as detection, not retraction. Test (agent-root-check): a records frame whose build misses its root publishes nothing, keeps the prior generation, and repairs from the second source. |
| R5 | yes | gap | Nothing bounds how long an agent goes without a root. The delay "at most the root lag" assumes a root reaches it; a relay whose mirror drops or mis-files root entries (a bug in its mirror, inside D157's threat model) sends zero roots forever and every agent below it publishes unverified, silently. The leader's root-lag metric cannot see it. | doc lines 108, 135, 160, 180 | An agent-side rule: positions or time since the last verified root past a bound (a multiple of the window, e.g. 10 s) is reported in health and makes the agent ask another source. Test (agent-root-check): a relay that strips roots trips the bound. |
| R6 | yes | gap | The root entry's contract is not stated, and three quests consume it before authority-root-entries lands. {n, incarnation, p, root} is given, not: whether it takes a position in the namespace's history (and so counts toward the 2,048-change seal and roll-ups), how it rides W6 frames, blocks and the base a relay mirrors, whether the latest root per namespace survives a snapshot install or a cut (a follower after a snapshot otherwise has none until the next round), and what a relay or agent does with an entry for a stale incarnation or a position older than one it holds. This is history format, the most expensive piece of option 2 to reverse. | doc lines 105, 192-195; quest file's Contracts section (empty) | State the contract in the doc (or as the quest file's Contracts, which the lead writes) and order relay-authority-root and agent-root-check after it. Test (authority-root-entries): a root entry round-trips through history, a roll-up and a snapshot install, and does not change any namespace's seal boundaries. |
| N1 | no | gap | The decided pool was never run beside the journal. The paced pool arm built once per seal on ten threads (0.72-0.76 core); the decided shape spends 1.34-1.75 cores. Limits 7 says so, but the Recommendation ("leaves group commit p50 where it was: the pool's paired delta is -0.2 ms"), Decision option 2 ("no commit latency outside the A/A band") and D156's reason attribute that delta to the decided shape. `24d1dd` bounds it from above only coarsely (strict, -5 to +21 ms). | doc lines 166, 205, 216; `519b4d` block lines (`arm":"pool"`, 50 builds per 5 seals) | Say "a builder at 0.72-0.76 core" where the delta is quoted, and give authority-builder-pool a criterion: the paced row at four threads and 250 ms, exclusive, inside the A/A band. |
| N2 | no | gap | The rows price ten namespaces; the doc gives no scheduling rule for when passes outrun four threads (fair order across namespaces, the interval as a minimum), and `state.hh` allows 4,096. My probe found no cliff up to 1,000: 1,727 passes a second at 1.0 ms p50 each, 1.89 cores and lag p99 453 ms under light, against 1.84 cores and 1,073 ms for ten in the same job. So this is a missing rule, not a missing cost. | `261124-105108-776fa1`; doc line 65-79 | One sentence on the order and the interval as a minimum; the 1,000-namespace row (strict) in authority-builder-pool. |
| N3 | no | gap | "Builds at rooted positions" (D7's row) does not say whether a relay builds at rooted positions instead of at its head, or both. Instead of is the cheap reading and fits the split answer (r→h goes as records); both doubles relay builds at the root cadence, four per second per touched namespace. Either changes the relay's build cadence, which the doc does not price. | doc lines 107, 184 | One sentence: relays build at rooted positions, not at head, and their cost follows the LAG table's pool rows. |
| N4 | no | nit | "Twice the peak RSS" for ten threads against four is 1.7-1.9 times (1,883 / 1,000 MB, 1,939 / 1,132 MB). | `a9b30c`, `d6ce25` | "1.7-1.9 times". |

### Not run

- The eight reruns at Detent:b5ce810d were cancelled before running; whether a number moves at the
  final head is unknown, and F8's exclusive reruns are still owed. The doc's rows are at
  Detent:7a4d6f8 and earlier; the last main merge changed src in detent_local, detent_reader and
  detent_testing (journal), which those reruns were to cover.
- No strict or exclusive probe (the brief allowed light only), so no rerun of the decided pool beside
  the journal (N1).
- No model or test of the lifecycle above: R1-R6 are read from the doc and the landed code, not run.
- Reviewer ran as a subagent of the lead, not a session titled `detent · reviewer
  authority-pages-design design`.

## Round 1 at Detent:65576604 (2026-11-23, CHANGES)

The direction holds: an authority-built root is the only option that closes Limit 1 for agents
taking pages, the build belongs off the service loop (inline stalls it in every job), and pages in
memory are simpler than persisted ones. What does not hold yet is the doc's account of what (b)
costs at rc3 load and after an election, which serving paths actually carry the authority's root,
and what the decision rows become. Those are text and measurement fixes, not a re-plan.

### Evidence I relied on

- Seal rows: `261122-210428-976f07`, `261122-210429-1ed7a8` (exclusive, `bf55a09`), strict reruns
  `261123-103432-c20472`, `261123-103435-b1e0ae`, `261123-103438-3b96b1`, `261123-102616-53d93e`,
  `261123-073248-5fdf61`. I re-read their JSON: the doc's per-seal p50s, bytes, relocations, (d)
  digest and the derived 59% / 2.95 cores / 1.65 seals/s / 19.5 s / 48 s arithmetic match.
- Commit rows: the eight strict jobs `261123-1025xx/1026xx` at `300f296b`. I re-tabulated every arm
  (`.scratch/detent/review-authority-pages/arms.py`); the medians and paired deltas in the doc are
  computed correctly from them. What they measure is the subject of F1.
- The NoChange fix and its fails-without pair (`261123-102550-885bdd`, `261123-102546-6e81cf`).

### What I checked myself

Scripts under `/Users/kronuz/code/Kronuz/Detent/.scratch/detent/review-authority-pages/`:
`bench/probe_bench.cc` is the quest's harness plus `coldpool` (a cold build on N threads) and
`satN` commit arms (N threads building 2,048-change seals back to back over their own 500,000-key
namespace while the arm runs); `build.sh` builds it against `$JOB_SRC`; `probe.sh coldpool|coalesce|sat`.

- `261123-145400-63b0f2` (strict, at `65576604`; `probe.sh coldpool` then `coalesce`). A 5M
  cold build (10 x 500,000) on 1 thread took 13.4 s and 16.3 s wall (12.3, 14.8 s CPU); on 2
  threads 8.6 s; on 4, 6.2 s; on 10, 4.6 s and 5.1 s wall at 1,807-1,844 MB peak RSS against
  426-492 MB on one thread. One builder pass over 18,432 changes (9 seals behind) cost 2,694 ms
  CPU p50, and over 102,400 (a 10 s burst) 4,523 ms, against 872 ms for one 2,048-change seal in
  the same job: 9 seals cost 3.1 times one seal, 50 seals 5.2 times.
- `261123-145405-24d1dd` (strict, at `65576604`; `probe.sh sat`): the commit path while 4 or 16
  threads build 500,000-key seals back to back (97-232 s of CPU in 23-26 s, the host's 16 hardware
  threads saturated), order none, sat16, sat4, none, sat4, sat16, none. Group p50: none 133.7 /
  144.6 / 139.8 ms; sat4 154.2 / 139.2; sat16 146.1 / 139.0. p99: none 357.5 / 230.9 / 329.5; sat4
  368.9 / 357.9; sat16 259.0 / 311.3. Group max: none 923.9 / 402.4 / 386.1; sat4 480.2 / 375.4;
  sat16 342.3 / 406.5. The probe's `worst_service_gap_ms` for the sat arms (2.4-7.3 s) is my
  artifact: the harness does not reset its last-pump time between arms, so the first gap spans the
  pool's own cold builds. Groups run back to back, so no stall inside a sat arm exceeded its group
  max, 480 ms.
- Re-read of every 5M cold-build sample in the quest's jobs: one exclusive (`1ed7a8`: 9.00 s CPU,
  12.04 s wall with sync); nine strict (`b1e0ae`, `5fdf61`, `3b96b1`, and the `cold` and
  `cold-write` lines of the four 5M commit jobs): 11.1-20.1 s CPU, 11.5-36.2 s wall.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The commit rows do not run the rc3 write load and their noise is larger than the stated floor. Every arm committed 139-474 tx/s, so 5 builder seals fell in 22-58 s (0.09-0.23 seal/s, a fifth to a tenth of rc3's 0.98, a twentieth of the burst); the builder thread was busy 5-15% of the run. Group latency is `F_FULLFSYNC`-bound and bimodal (about 135 ms or 260-300 ms) and switches inside a job under strict holds, which share the device: in `afdabb` none 134.6, thread 139.3, then inline 294.0, write 289.5; in `d23ae1` every arm reads 262-287 ms. So "the commit rows compare arms inside one job, which this does not bias" (doc line 9) is false for `afdabb`, and write's "+13.1 ms" is the median of {+154.9, +19.1, +7.0, -2.1}: not resolved. The thread arm's conclusion survives: my saturating pool (`24d1dd`) moved group p50 by -5 to +21 ms against the adjacent none arm, with p99 259.0-368.9 ms against none's 230.9-357.5 and group max 342-480 ms against 386-924. | `arms.py` over the eight `261123-1025xx/1026xx` jobs; `261123-145405-24d1dd` | State the commit rows' rate (about 0.2 seal/s) wherever their deltas are quoted; report write as "not resolved above a ±130 ms regime noise" and rule out persisted pages on simplicity and the 3.0 s root lag, not +13.1 ms; cite `24d1dd` (or a contributor rerun of it) for the builder at full load. |
| F2 | yes | gap | The root lag after a leader change is understated and its consequence for Limit 1 is not stated. The doc gives "about 9 s at 5M" from one exclusive cold build; nine strict samples read 11.1-20.1 s CPU, my one-thread runs 13.4-16.3 s wall, and seals keep committing during the cold build, so a single builder that builds per seal then trails by about 9 more seals (another 13 s at the doc's own 0.67 seal/s drain). A pool of 10 cut the cold build to 4.6-5.1 s, at 1.8 GB peak RSS against 0.43-0.49 GB, a spike the doc's memory row does not show; one coalesced pass over 9 seals then takes 2.7 s. Every answer in that window carries no authority root, and the doc leaves open (line 111) whether agents wait for it or publish records "verified against the next root". Waiting stalls visibility for that window, against SCALE_TARGETS' p99 publish-to-agent-visible under 1 s; publishing leaves Limit 1 open for the window, at every election and leader restart. | `1ed7a8`, `b1e0ae`, the 5M commit jobs' `cold` lines; `261123-145400-63b0f2` | Give the window with measured parts (pool cold build plus one coalesced pass: about 8 s on this host, at the RSS above); state the agent's rule when an answer has no authority root, and what Limit 1 becomes under it ("closed except for one window per leader change", or a visibility stall with its length), so the owner rules on it. |
| F3 | yes | gap | "The root in every answer is the authority's" is not specified on every serving path. (i) Followers serve relays: D145 (voter-read-freshness) has followers answer history and layouts once a leader confirms their read index, but under (b) a follower holds no root ("nothing on followers"). (ii) A relay fills the frame header from its own build today (dense-distribution D1: "the sender is the relay that built the target"), and the doc keeps relays that build "as a cache"; nothing states that a relay forwards the authority's root verbatim and refuses to serve pages whose root differs, which is the only thing that catches a relay that mis-applies records. Multi-tier relays need the same rule per hop. (iii) Positions committed between the old leader's last root and the new leader's cold build never get a root. (iv) The root is unauthenticated, so closure holds against a faulty relay that forwards headers, not against one that substitutes them. | doc lines 111, 131; dense-distribution.md:103, :158; PLAN.md D145 | State per path where the root comes from and what a relay and an agent do when it is missing or differs. One option that answers (i) and (iii) at about 32 B per touched namespace per seal: the leader proposes each built root as a log entry, so every voter holds and serves it and it survives a leader change. Say which threat model Limit 1's closure covers. |
| F4 | yes | gap | The pass-through relay, on which "(b) moves the build rather than adding it" (doc line 101) rests, is neither specified nor priced, and the Q4 row contradicts it. Q4 becomes "the authority adds roots, not a page server", yet a cold relay "takes the leader's canonical full copy" and pass-through relays serve "the authority's pages". A relay that does not build still needs D7.3's page window and D7.4's canonical copy, so either the leader serves page deltas, a window of superseded pages and a relocated full copy (none in the memory or wire columns; the wire column says +32 B), or the relay builds. Per update, pages from upstream are the cost D7.2 rejected: at 5M, 10,000 uniform keys are 108 KB of records against 6.71 MB of pages (dense-distribution Q1), and a 2,048-change seal rewrites 11.2 MB here. | doc lines 59, 93, 99-101, 132; dense-distribution.md D7.2-D7.4, Q1 | Specify what a pass-through relay fetches from the leader and when (full copies only, with records for deltas, is the cheap reading), and price the leader's added memory, CPU and egress per relay; or drop pass-through from the recommendation and restate (b) as an added cost, 59% of a core at rc3 on the leader. |
| F5 | yes | gap | The decision rows do not say what changes. D34's recorded reason is "builds stay out of the consensus process", and Q4 (b) was recommended because its option (c), "the authority builds pages", "puts builds in the consensus process"; (b) is that option on threads in the same process. The doc says D34 is unchanged and Q4 (b) "stands". D1's sender sentence and its rejected alternative "an authority-computed digest", D4's last paragraph, and D7.2/D7.4 change too and are not listed; D133, the current reading of D33 under which `dense-distribution-build` landed relays that build and fill the root, is not cited. | doc lines 127-136; PLAN.md D34, D133; dense-distribution.md:103-107, :158, :221-230, :365-371 | Rewrite the table: Q4 reopened (the authority builds, in-process off the service loop); D34's reason amended from process to loop; D1, D4 and D7 edits named; D133 cited; and the quests added with the landed code each one changes (the relay's root fill, the agent's check). |
| F6 | no | gap | The burst is sized per seal, but coalescing removes most of it. The doc's 2.95 cores and 19.5 s lag assume one build per seal; one pass over a 10 s burst's 102,400 changes cost 4.5 s on one thread (5.2 times a single seal, not 50), and 9 seals cost 3.1 times one. So one coalescing builder holds burst root lag at a few seconds on one core, and the pool earns its place only for the cold build (F2). That bounds the burst: CPU is capped by the pool size and the excess becomes root lag, never commit latency. | `261123-145400-63b0f2` | Replace "2.95 cores at burst" and "a pool divides this" with coalescing's numbers; name the pool's bound (threads, priority) and the root-lag metric (Limits 5). |
| F7 | no | nit | The inline arm's 2,650 ms is one job of four (971, 1,104, 1,507, 2,650 ms); the ruling stands on p99 in all four (1,020-2,303 ms against none's 234-490). | `arms.py` | Quote the range. |
| F8 | no | gap | The decisive CPU numbers (604.8 ms per seal, 9.0 s cold) are one exclusive job each, at `bf55a09`; strict runs read 1.2-2.2 times them. The doc says so; the 59% and 2.95-core figures could carry the strict range beside them. | `1ed7a8`, `b1e0ae` | An exclusive rerun at head, or the range. |
| F9 | no | nit | Build CPU includes the records encode (`authority_pages_bench.cc:265-302`: the CPU window opens before `encode_session_frames`), though journal ruling 3 says encode is timed apart. It inflates (b) slightly, so it is conservative. | file:line | Say so, or move the CPU start after the encode. |
| F10 | no | gap | (d)'s dismissal rests on 119 s for a cold agent to check pages, from SHA-256 without SHA-NI; a faster XOF or SHA extensions change it several times over. Limits 4 notes the construction; the decision request does not. | doc lines 91, 121 | One line in the decision request. |
| F11 | no | gap | The builder in the commit rows reads its own corpus, not the authority's applied records, so contention between the builder and apply or history for those records is not modeled. | `authority_pages_bench.cc:617-660` | A line in Limits. |

### Not run

- No exclusive rerun of the seal rows at head; the decisive per-seal CPU (604.8 ms) and cold build
  (9.0 s) remain one exclusive job each (F8).
- No Linux or SHA-NI host, no 3-voter replicated run, no small (2-4 core) host: the pool's effect
  on election timing on a small authority host is inferred from an 8-core probe only.
- No correctness or security review of the harness beyond what the numbers depend on.
- Reviewer ran as a subagent of the lead, not a session titled `detent · reviewer
  authority-pages-design design`.
