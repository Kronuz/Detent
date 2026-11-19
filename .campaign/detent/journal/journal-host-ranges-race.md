# journal-host-ranges-race: journal

State: waiting on jobs 261118-172110-e51757 261118-172110-fdbe99 261118-172112-0b1b26 261118-172115-84fa95 261118-172118-3b8390 261118-172120-df8396
Quest: .campaign/detent/quests/journal-host-ranges-race.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head journal:0fc8c14, pushed; GitHub CI running. journal:f3fc19e gives each loop its own table;
journal:0fc8c14 adds the debug owner-thread check (`owned_ranges`), fixes
bench/group_io_bench.cc to index loop 0's table, and updates docs/perf/group-io.md (msg from the
lead: bench and perf doc added to the file set). Mutant journal:1c65ec4
(`refs/mutants/journal-host-ranges-race/shared-table`): `owned_ranges` sends every group to
table 0. Jobs: suite 261118-172110-e51757, tsan 261118-172110-fdbe99, A1 TSan at head
261118-172112-0b1b26, A1 mutant TSan 261118-172115-84fa95 (--fails-without), A1 mutant debug
assert 261118-172118-3b8390 (--fails-without), A3 exclusive 261118-172120-df8396 (base, mutex,
per-loop; real bench at head). A3 scripts: `.scratch/detent/journal-host-ranges-race/a3-build.sh`
and `a3-run.sh` in the home. Jobs at f3fc19e and the old mutant were cancelled.

## Next

1. When the jobs finish: read A3's numbers (loop vs base vs mutex at 1K and 1M random ids), add
   them to docs/perf/group-io.md and docs/mechanisms/host-ranges.md, paste `campaign brief`'s
   table here, set State to ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | [artifact] `docs/mechanisms/host-ranges.md`; the rule at `ranges_`'s declaration, `group_io`'s comment and `owned_ranges` in `include/journal/host.h` (journal:0fc8c14) |
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
