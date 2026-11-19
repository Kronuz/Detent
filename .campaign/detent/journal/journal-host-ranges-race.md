# journal-host-ranges-race: journal

State: waiting on jobs 261118-185126-82265f 261118-185126-2eeb43 261118-185127-cf5001 261118-185129-a1418f 261118-185131-950d48 261118-185216-340039 261118-185140-2b12ac 261118-185141-16e1a8 261118-185153-bed12c 261118-185154-eeb612 261118-185155-60fcd2 261118-185156-edea88
Quest: .campaign/detent/quests/journal-host-ranges-race.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head journal:0202ebc, pushed. At journal:0fc8c14: A1 passed (TSan 0b1b26; mutants 84fa95 and
3b8390 died with 134), A3 passed (1M random: base 766 ns, mutex 695, per-loop 786), CI green, but
suite e51757 and TSan fdbe99 failed: journal_host hit ctest's 30 s timeout on a loaded host.
Hypothesis: loop_views' 200 create_group calls each commit a shard on PosixIO, a full flush on
macOS (CMakeLists notes it for turn_log). journal:0202ebc cuts them to 20 per loop; both threads
keep taking views until both finish creating, so the overlap stays.

Timing (debug and TSan, whole journal_host_test x5, then loop_views x5,
`.scratch/detent/journal-host-ranges-race/host-time.sh`): main 6a37f19 bed12c / eeb612,
0fc8c14 60fcd2 / edea88, 0202ebc 2b12ac / 16e1a8. Moved to 0202ebc: suite 82265f, tsan 2eeb43,
A1 TSan cf5001, A1 mutants at journal:7cf0101 a1418f (TSan) and 950d48 (debug assert), A3
340039 (A3 df8396 "ran other code": test-only change).

## Next

1. When the jobs finish: check the timings show 0fc8c14's loop_views as the cost and 0202ebc's
   journal_host under 10 s under TSan (3x headroom on 30 s); if not, shrink further. Then read A3's numbers (loop vs base vs mutex at 1K and 1M random ids), add
   them to docs/perf/group-io.md and docs/mechanisms/host-ranges.md, paste `campaign brief`'s
   table here, set State to ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | `docs/mechanisms/host-ranges.md`; the rule is at `ranges_`'s declaration, `group_io`'s comment and `owned_ranges` in `include/journal/host.h` |
| A3 | pending job |
| A4 | pending jobs |

## Result

`Host::ranges_` (include/journal/host.h) is a `std::vector<host_detail::Ranges>`, one multimap
and sweep threshold per loop. `group_io(g)` and `publish(g)` touch only table `loop(g)`, on that
loop's thread, which already owns g's table shard: no lock, so no hold can stall another loop's
commit. `publish` returns at once while the vector is empty (the open's commits, before any view
exists). Mechanism: docs/mechanisms/host-ranges.md. Test: `host_test loop_views`.

Callers (proof that group_io runs on the group's loop, docs/mechanisms/host-ranges.md "The
callers"): one production caller, `Journal(Host&, group)`; consensus builds Stores only in
`Driver::open`, which asserts `on_loop()` and refuses `host_.loop(id) != loop_`. Detent, strata,
Xapiand, cluster and storage include no journal Host (rg over their checkouts). Debug builds
assert it (`owned_ranges`); the suite runs debug.

## Rulings I made

1. Per-loop ownership over a mutex: a mutex over one shared table holds for a full sweep, about
   89 ms at 1M random ids (job 261118-145320-05f3b3's per-call `erase_if` column), stalling every
   other loop's commit. A3 measures the mutex arm too.
2. The rule constrains `group_io` to the group's loop thread, stated at the method. Any other
   thread already raced with the loops' commits, so no valid use changes; Host's API is unchanged.
3. The A1 mutant is the shared table without a lock (every group to table 0), kept off the branch
   under `refs/mutants/journal-host-ranges-race/shared-table`.
4. The debug check binds a loop's table to the first thread that touches it after the open, for
   the host's life (journal has no loop-thread identity to compare against). A consumer that
   moves a loop between threads would trip it in debug; none does today.
5. The tables are sized last in open(), so the constructor's commits publish nothing and bind no
   thread.
