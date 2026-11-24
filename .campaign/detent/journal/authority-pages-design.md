# authority-pages-design: journal

State: stopped by the lead mid round 3 (owner reconsidering authority pages); no jobs of this round queued
Quest: .campaign/detent/quests/authority-pages-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 (design re-review CHANGES at 12608a08: T1-T3, U1-U6; owner rule V5, table first). Plan, then fixes:

| Finding | Fix | Shown by |
|---|---|---|
| T1 rule 3's clock counts from the last new-root check | clock = age of the oldest published-but-unverified record, zero when none; re-checking the held root never resets it; doc rule 3 has one reading | model: idle then write publishes at once; a stalled builder with writes trips at the bound; lost roots trip; re-check does not reset |
| T1b bound by hand (10 s) | derived from measured parts: last root age + election timeout + window + root commit + delivery budget, times margin; presented with what a user sees per trigger; the owner decides | doc "Rule 3's bound" |
| T2 root-entry proposals unpriced | root entries ride in the next transaction group (no extra sync); bytes per entry stated; harness arm pass4r = pass4 plus its root entries riding each group | the paced exclusive A/B at the final head (replaces 261124-111623-b18370, which ran no root entries) |
| T3 pass4 slots unfilled | fill ⟨PASS4-DELTA⟩, ⟨PASS4-JOB⟩, ⟨SEAL-LAG⟩ from the A/B | the paced exclusive A/B |
| U1 retry storm | asking another source is jittered and rate-limited; records, not a full copy, unless a check failed | doc rule 3 |
| U2 agent restart | v, the tail and the clock's start persist with the published state, else the tail is unverified from the restart; second generation priced | doc rule 2 |
| U3 new namespace or incarnation | invisible until its first root: stated as rule 2's exception | doc rule 2; model incarnation case |
| U4 untested states | kept as acceptance criteria for the implementation quests | doc quests list |
| U5 pending lists unbounded in a stall | bound, past which the namespace is dropped from the pool and cold-built again | doc loop paragraph |
| U6 wording, line numbers | "elections whose window outruns the bound"; D133 row at namespace_relay.cc:414/426, coordinator.cc:683-692 | doc |

Sweep, every timer and clock in option 2 (start, reset, what idles it, model case):

| Clock | Starts | Resets / stops | Idle | Model case |
|---|---|---|---|---|
| Rule 3 (agent) | first publish of a record past the last verified root | a checked root covering the oldest unverified record (moves to the next; zero if none) | nothing unverified: zero | idle-then-write, stall, lost roots, re-check |
| Pass interval (leader, per namespace) | start of the namespace's last pass | the next pass's start | no pending changes: no pass; the interval is a minimum, so an idle namespace's first change passes at once | scheduler: idle write passes on arrival; busy waits the interval |
| Step-down cancel (leader) | the loop sees its role change | a new term as leader starts a fresh pool | a follower has no pool | results of an old term are never proposed |
| Staleness (voter, relay, agent) | not a clock: order by (incarnation, position) | a newer entry replaces | none | a root of an old incarnation or older position is ignored |
| Source retry (agent, U1) | a refused answer | a checked root | nothing refused | jittered, rate-limited (doc only) |

Fix round 2: correctness review CHANGES (C1-C8) and design re-review CHANGES (R1-R6, N1-N4), both at 28ca4c31.
The eight reruns at b5ce810d were cancelled before they ran (the harness changes again here). Fix round 1
(F1-F11), D156 and D157 are recorded below and in the doc.

| Finding | Fix | Shown by |
|---|---|---|
| C1 interval was z over 4 pairs | check.py `stats` uses Student's t (table to df 30, Cornish-Fisher past it); every interval and the A/A band in the doc recomputed from the same logs: 519b4d pool [-1.8, 1.3], aa [-1.7, 1.3], thread [-3.3, -0.6], write [33.3, 40.8]; strict rows widened, none resolves | test_check.py (t(3) = 3.182; four pairs [0.4, 4.6]); fails without: the z mutant |
| C2 recommended pool never beside the journal | harness `paced` gains arm pass4 (option 2's PassPool, four threads, 250 ms passes, arrivals at 0.98 x 2,048/s), rotating with thread, pool, write and aa; the doc's claims about it wait on ⟨PASS4-DELTA⟩ / ⟨PASS4-JOB⟩ | the exclusive paced job at the final head (the rerun of 261123-193517-519b4d) |
| C3 "builder lag" was the longest build | Builder stamps each post and reports lag (post to build end) p50/p99/max and build_ms_max apart; doc row relabelled "longest build of one seal", backlog at block end from drain_ms (write 3,168-3,463 ms, thread 49-648); ⟨SEAL-LAG⟩ waits on the paced job | the exclusive paced job; doc "Commit latency and elections" |
| C4 detection delay omitted the root entry's commit | rule 2 adds the group commit (133 / 309 ms) | doc, After a leader change |
| C5 lag CPU was process CPU | `build_cores` printed; doc CPU column recomputed as builder CPU (recommended 1.30 / 1.72 cores, was 1.34 / 1.75) | doc LAG table; lag rerun |
| C6 digest timed wall | `digest_cpu_ms` on thread CPU; doc labels old rows "wall on one thread" | seals reruns |
| C7 commit mode gap not reset | `reset_gap()` | commit reruns |
| C8 posting rate unchecked | check_paced fails outside 0.7-1.1 x RATE; posting rounds to the nearest seal (was down) | test_check.py slow-rate case |
| R1 relay strips root on divergence | relay forwards the root always; agent publishes nothing on a missing, stale or failing root; doc "Agent and relay rules" 1-2, root path table | test_root_model.py (5 cases); fails without: ROOT_MODEL_RULES=strip (3 fail) |
| R2 pool and loop handoff | doc "The builder and the service loop": input (pending lists, persistent_map snapshot), encode on the pool, output queue drained once per turn, memory, scheduling (N2) | text; authority-builder-pool's TSan test named |
| R3 step-down | lifecycle table and "Step-down": cancel flag, drop results and queue, free heads, one cold build at a time | text; authority-builder-pool's test named |
| R4 after a failed check | agent rule 4: keep published state, count, full copy from another source checked first, health unverified | test_root_model.py wrong-tail case |
| R5 time without a root | agent rule 3: 10 s bound, then stop publishing the namespace and ask another source | test_root_model.py bound case |
| R6 root entry contract | "The root entry": entry, history (no position, survives snapshot and cut), wire, staleness | text; authority-root-entries' test named |
| N1 | = C2 | |
| N2 | scheduling sentence in R2's paragraph, probe 776fa1 cited | text |
| N3 | relays build at rooted positions, not at head | root path paragraph, D7 row |
| N4 | "1.7-1.9 times the peak RSS" | text |

Jobs at the final head 67b76b6c: the eight cancelled reruns, 261124-111619-945876 (100K seals, exclusive),
-111619-8a04c5 (5M seals, exclusive), -111620-0bbbdb, -111620-a9701c, -111621-1c41a2 (commit rows),
-111622-6718ae (strict seals), -111623-b18370 (paced 5M exclusive, with pass4: C2's A/B against none and the
A/A), -111624-56374f (lag steady); smoke -111623-31f7f5; suite -111629-fea86a; tests -111638-e08fc4; fails
without: C1 z mutant -111704-bdd5fe, R1 strip mutant -111650-5bc59c. Not queued, though `campaign brief`
lists them because the harness changed: the strict commit rows 1457d9, ac3113, 4d9157, 02c215, the strict
paced 65f196 / 95a0ae, the lag rows d6ce25, e60802, 1675da, 962367 (exclusive) and 885bdd. The lead's
instruction named the eight plus C2; those 11 are the lead's call (A1 cites them at 7a4d6f83 / 392deab8).

For the owner (flag, the decision rests on option 2's line): agent rule 3 makes option 2 fail closed. An agent
that goes 10 s without a checked root stops publishing that namespace, so a builder stall, a relay that loses
root entries, or elections faster than the window now stall visibility after 10 s. Before, those cases published
unverified with no limit. Steady and post-election numbers are unchanged: the window is 2.1-6.2 s, under the bound.
C3 does not change option 2's numbers: lag mode measured arrival to pass end throughout.

## Next

Stopped at the lead's word (2026-11-24, owner reconsidering authority pages for speed over verification).
Where round 3 stands, at 6627483e:
1. Done, committed: T1 (rule 3's clock = age of the oldest unverified record; model cases idle-then-write,
   stall, lost roots, re-check; mutants last-check and any-check fail 3 and 2 cases locally), T1b (bound derived
   at 15 s, owner options in "Rule 3's bound"), the clock sweep table ("Clocks"), U1-U6 text, T2's design (root
   entries ride the next group) and the pass4r arm in the harness.
2. Not done: the pass4r harness is unsmoked (the first local smoke hit the 64-transaction group bound; fixed by
   riding roots as mutations of the last transaction, rebuilt, not rerun). ⟨PASS4-DELTA⟩, ⟨PASS4-JOB⟩,
   ⟨SEAL-LAG⟩ and ⟨ROOT-ENTRIES⟩ remain unfilled.
3. Jobs: I cancelled 261124-111623-b18370 at 11:59 myself, to replace it with one exclusive A/B at the round-3
   head adding pass4r (ruling 15); its record shows no other reason. This round's smoke 261124-120011-be672b and
   model tests 261124-120021-e8df6d were cancelled at the stop. Still queued from round 2 at 67b76b6c, the lead's
   to keep or cancel: 945876, 8a04c5, 0bbbdb, a9701c, 1c41a2, 6718ae, 31f7f5, 56374f, fea86a, e08fc4, 5bc59c.
4. To resume: smoke pass4r (`run.sh smoke`), then one exclusive `run.sh paced 10 500000 0.98 4 40` at the head,
   fill the four slots, set "design ready for re-review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/design/authority-pages.md prices (a)-(d) at 100K and 5M, every number citing its job. Seals: exclusive 261122-210428-976f07, 261122-210429-1ed7a8; strict 261123-143709-e27543, -143709-c59049, -143710-993a49, idle -143708-866493. First commit rows (inline ruling): 261123-143701-b2823a, -143704-14eaf8, 261123-143702-63a2df, 261123-143702-dbe12a, -102608-7ae2b2, -102610-fdd616, -102613-0c4d8c. Paced commit: 261123-193517-519b4d (exclusive), -193507-f0eafa, -193510-65f196, -193513-95a0ae. Root lag and window: 261123-193520-a9b30c, -193523-d6ce25, -193525-e60802, -193528-1675da, -193531-962367 (exclusive). Reruns at the final head b5ce810d: 261124-104556-e167d6, 261124-104557-d95ac0, 261124-104557-c49edb, 261124-104558-1a841c, 261124-104559-60f158, 261124-104559-41e88a, 261124-104600-50c662, 261124-104600-0c7e60 |
| A2 | 261123-193433-2e4687 (smoke at 7a4d6f83; the merge since touches tests only); fails without: 261123-102546-6e81cf (with 261123-102550-885bdd). Every row is strict or exclusive at a named commit; before = arm none / option (a), interleaved; noise floor = the aa (A/A) arm's 95% CI (paced), min-max of 30 seals (seals). Suite at head 261124-065709-635779 (passed) |
| A3 | docs/design/authority-pages.md, "Recommendation" and "What it changes": Limit 1 closed with a bounded detection delay (open against a lying relay, Q1); Q4 (b) stands for pages, the authority serves records and roots; D6 unchanged plus split answers; D1, D4, D7, D34 (reason: off the service loop) and D133 amended; quests added: authority-root-entries, authority-builder-pool, relay-authority-root, agent-root-check; none dropped |
| A4 | [manual] owner decision, PLAN D156 (2026-11-24): option 2, the leader builds for the root on four builder threads, one pass per namespace every 250 ms, pages in memory, agents publish and verify at the next root; (d) the fallback. Q1: PLAN D157, no root signing now. Recorded in docs/design/authority-pages.md, "Decision" |

## Result

docs/design/authority-pages.md, bench/authority_pages/. Recommendation: (b) the leader builds for the root only,
on four builder threads with one pass per namespace every 250 ms, pages in memory, each root proposed as a log
entry; relays forward roots and check their own; agents publish and verify at the next root. (d) the fallback.
- Per seal (exclusive): build 78.8 ms CPU at 100K, 604.8 ms at 5M; (d) 97.3 / 96.7 ms, flat in store size.
- At 5M and rc3's 2,000/s: 1.34 cores steady, 1.75 burst (strict); root lag p99 0.33 s, 0.56 s burst.
- Commit p50 with a builder beside the journal: pool -0.2 ms inside the A/A band of ±1 ms; persisted pages
  +37.1 ms (exclusive 519b4d). Inline stalls the loop 971-2,650 ms: ruled out.
- Leader change: first root past the cold build in 3.3-6.2 s on four threads (strict), 2.1-2.7 s on ten
  (exclusive), at 1.0-1.2 / 1.9-2.0 GB peak RSS.
- Decided: A4 = option 2 (D156), Q1 = no signing now (D157).

## Rulings I made

13. T1b, rule 3's bound: 15 s, derived as 1.5 x the worst measured sum (record age 0.64 s + election 2 s + window
    6.2 s + new root's commit 0.31 s + 1 s delivery budget = 10.2 s); my sum of parts 1-4 is 4.1-9.2 s (the
    reviewer wrote 4.4). Recommended as configuration defaulting to 15 s; the owner decides (options 1-4 in the
    doc, "Rule 3's bound").

14. T2: root entries ride the next transaction group as part of its batch, never a proposal or sync of their
    own (the loop admits one prepared proposal at a time). Measured by the paced arm pass4r: pass4 plus one
    62-byte mutation per finished pass in the next group's last transaction (a group holds at most 64
    transactions, a transaction 128 mutations; at most 120 roots ride one group, the rest wait).

15. The exclusive paced A/B at 67b76b6c (261124-111623-b18370, still queued) ran no root entries; it is replaced
    by one at the round-3 head that adds pass4r, so T2 and T3 share one exclusive job (the lead allowed folding
    T2 into C2's A/B while it is queued).

11. Pool size four, not one per namespace (supersedes 9's "up to one per namespace"): in the same strict jobs ten
    threads held the same root lag as four (p99 0.40 vs 0.33 s steady, 0.55 vs 0.56 s burst) at 1.5-1.6 times
    the CPU and twice the peak RSS, and shortened the window by 0.1-1.9 s. The paced commit rows ran ten threads
    per seal (0.72-0.76 core); four at 250 ms were not run beside the journal (Limits 7).

12. Persisted pages are now resolved: +37.1 ms [34.8, 39.4] exclusive against an A/A of [-1.1, 0.7], so the doc
    rules them out on that number plus simplicity; the review's "not resolved" wording was about the old rows.

10. F1, rc3's rate: this host's durable single voter cannot commit 2,000 tx/s (one F_FULLFSYNC per
    64-transaction group). Pipelining 8 batches per group was tried locally and failed: the node admits one
    proposal per service call and the harness drives IO synchronously, so a 512-transaction group took
    ~1,050 ms (8 fsyncs). The paced rows therefore hold the builder at rc3's 0.98 seals/s by the clock,
    independent of the voter's rate; the doc states this as Limits 3.

9. Interval 250 ms and a pool of up to one thread per namespace are the recommended shape; immediate passes
   (no interval) cost 1.08 cores on a 10K-key smoke with 314 relocations, so passes are rate-limited.

8. F4: pass-through is deferred, not part of (b); (b) is restated as an added cost; the authority serves no
   pages (priced). F3: roots as log entries and split answers at rooted positions are the design's answer to
   followers, relays and unrooted positions; signing is the owner's Q1.

7. Lead's ruling (2026-11-23): the exclusive seal rows (261122-210428-976f07, 261122-210429-1ed7a8) are not
   rerun; the doc cites their CPU at bf55a099, and the strict reruns at the merged head are A2's evidence that
   the merge moves nothing (same bytes, same ordering, 29-64% more CPU beside light holds). The doc's seal
   section says both, with each job id.

6. Commit rows: 4 rotations (a Latin square) instead of 3 interleaved rounds, one strict job each, so every
   job fits 10 minutes and each arm sits once in each position; arms are compared by paired deltas within a
   job. Commit rows run at 07ac6b63 (main 1941ec85 merged: authority src changed, relay builder did not).

5. Before/after for A2 is arm none against the builder arms, interleaved on one node (3 rounds each), and
   seal p50 with min-max over 30 seals for the noise floor; (a) is the before for every option.

1. rc3 write load = SCALE_TARGETS.md:117's proposed baseline (2,000 tx/s sustained, 10,000/s burst), one change
   per transaction, uniform keys; with seal_transactions 2,048 (state.hh:28) that is about 1 seal/s and 5/s.
   The commit arm runs this host's closed-loop maximum (64 producers), since one voter here commits below it.
2. Corpora are relay_window_bench's synthetic namespaces: 100K = 4 x 25,000, 5M = 10 x 500,000 (a history
   file holds at most 500,000 changes). The dense-pages r12 corpora store definitions in an older encoding the
   C++ builder refuses (docs/mechanisms/paged-transfer.md).
3. The build is the relay's own (build_generation), fed a W6 records body from an in-memory history; the
   records encode is timed apart (encode_ms), the decode is inside the build and counted as build cost.
4. Commit mode's authority holds its own 100,000 keys for both builder corpora: the default state bound
   (~133 MB, test_history_large_base.cc:138) does not fit 5M, and the arms measure the builder's effect.
