# transport-idle-budget: journal

State: waiting on jobs 261118-075718-7593b8 261118-080908-e60cb9 (and CI run 44343810415)
Quest: .campaign/detent/quests/transport-idle-budget.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:42019eb (pushed). Waiting on the A2 mutant rerun 261118-075718-7593b8, the suite
261118-080908-e60cb9 (A3), and CI run 44343810415 (44341580130 at 33f8540 was cancelled by the
push).

## Next

1. When 7593b8, e60cb9 and CI 44343810415 are green: paste `campaign brief`'s table, set State
   "ready for review" (correctness lens; the diff is bench and docs only, so the lead may waive).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 [manual] | Cause: the loops=4 slope sits 2-8 B over the 48 KB cap (49,152 B), and the build type tips it. Job 261118-073615-da90ae at main consensus:5923b6c: Release passes 3/3 (the f0eff6 suite built Release), no build type fails 3/3 with slope 49,154 B (1fb596 and e7e9cf built without one). Both link OpenSSL 4.1.0. The slope carries each loop's OpenSSL thread-local DRBGs: connections sit on loop_of_node(peer), so at L=4 the loops running TLS grow with P. Job 261118-074432-acdca6 at consensus:d2eeb0d (DRBGs drawn per loop first): passes 3/3 in both builds, L=4 slope 46,735 B. |
| A2 | 261118-075718-7593b8 (--fails-without, rerun at 42019eb): +4,096 B member in `Connection`; "reached the test" in both builds; L=4 slope 50,829 B (Release) and 50,874 B (no build type), both FAIL 48 KB; L=1 also fails with no build type (49,271 B) but reads 49,121 B in Release, 31 B under. |
| A3 | Suite 261118-080908-e60cb9 at 42019eb (queued); CI run 44343810415 (in progress). |
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
