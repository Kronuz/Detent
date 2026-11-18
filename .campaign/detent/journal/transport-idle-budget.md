# transport-idle-budget: journal

State: waiting on jobs 261118-091335-f80be0 261118-091337-344b1b (A1 reruns at 42019eb; lead waives review: test-only diff)
Quest: .campaign/detent/quests/transport-idle-budget.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:42019eb, pushed. Suite e60cb9 passes 65/65 and CI run 44343810415 is green. The lead
reran the A1 probes at the head (f80be0 replaces da90ae, 344b1b replaces acdca6); they are running.

## Next

1. Lead: once f80be0 and 344b1b finish, A1's generated row reads green. A2's job 7593b8 is not
   counted by `campaign brief` (see Evidence); the bench is identical at 33f8540 and 42019eb.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | manual (contributor, checked by the lead): 261118-091335-f80be0 and 261118-091337-344b1b at 42019eb (lead's reruns, running). Cause, from the same probes at main consensus:5923b6c (261118-073615-da90ae, 3 runs per build): the loops=4 slope sits at the 48 KB cap (49,152 B), reading 49,107-49,109 B in Release and 49,154-49,159 B with no build type. Release passes and no build type fails, which is the f0eff6 (Release) vs 1fb596/e7e9cf (no build type) difference. Both link OpenSSL 4.1.0. The slope carried each loop's OpenSSL thread-local DRBGs: connections sit on loop_of_node(peer), so at L=4 the number of loops running TLS grows with P. 261118-074432-acdca6 at d2eeb0d (DRBGs drawn per loop) passes 3/3 in both builds. |
| A2 | 261118-091544-07e982 at 42019eb (rerun of 261118-075718-7593b8) (--for A2 --fails-without), the +4,096 B `Connection` mutant. It ran at 33f8540, whose bench and code are identical to 42019eb (the diff is docs/perf only); a resubmit at 42019eb was "reused" as this job, and `campaign brief` still lists A2 without evidence. Both builds reached the test. L=4 slopes are 50,829 B (Release) and 50,874 B (no build type), both FAIL 48 KB; with no build type L=1 also fails (49,271 B). |
| A3 | 261118-080908-e60cb9 at 42019eb: suite 65/65. CI run 44343810415 at 42019eb: success. |
| msg (lead, docs scope) | Docs written only to docs/perf/transport-idle-cost.md. |
| msg (lead, finish) | A2, A3 and write-up done as above; fits at head in 261118-075717-797949. |

## Result

consensus:33f8540 (bench/transport_idle.cc): before the first count, the bench draws OpenSSL's
public and private DRBGs on every loop and prints them per loop (9,408-9,412 B), set apart like the
timer wheels. Before, the loops that ran TLS grew with P at L=4, so their DRBGs landed in the fit's
slope. L=4 slope at main consensus:5923b6c was 49,107-49,109 B (Release) and 49,154-49,159 B (no
build type) against the 49,152 B cap: Release passed by about 45 B, and no build type failed by
2-7 B. That is the f0eff6 vs 1fb596 difference (job 261118-073615-da90ae). At head, job
261118-075717-797949 reads L=4 slopes of 46,681-46,686 B (Release) and 46,736-46,737 B (no build
type), and L=1 slopes of 44,978 B and 45,128 B (unchanged); every host is within 10%, at 94-102%.
The remaining ~1.7 KB L=4 vs L=1 gap per end is C++ heap (Asio's per-thread handler memory). Write-up:
docs/perf/transport-idle-cost.md ("Each loop's thread state, set apart"). Open: in Release only
the L=4 fit catches a +4 KB end, because the L=1 slack is 4,174 B.

## Rulings I made

- Docs go under docs/perf/transport-idle* only (lead, mid-task).
- Per-thread OpenSSL state is a per-loop cost, set apart and printed per loop like the timer
  wheels (consensus:33f8540), not counted per host: counting it per host (d2eeb0d) charges all 4
  loops' DRBGs, about 30 KB, and puts L=4 hosts 11-26% over 52 KB + 2P x 44 KB, failing the full
  (non --quick) mode's 10% check that passed at 108-109% on main.
