# journal-host-ranges-race: journal

State: working
Quest: .campaign/detent/quests/journal-host-ranges-race.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (journal:f3fc19e): `ranges_` is one table per loop, owned by the loop's thread.
Jobs submitted for A1 (TSan at head; --fails-without at mutant journal:d545123,
`refs/mutants/journal-host-ranges-race/shared-table`), A3 (exclusive bench, three arms) and A4
(campaign verify: suite, tsan, ci).

## Next

1. When the jobs finish: read A3's numbers (loop vs base vs mutex at 1K and 1M random ids), add
   them to docs/mechanisms/host-ranges.md, paste `campaign brief`'s table here, set State to ready
   for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs |
| A2 | [artifact] `docs/mechanisms/host-ranges.md`; the rule at `ranges_`'s declaration and `group_io`'s comment in `include/journal/host.h` (journal:f3fc19e) |
| A3 | pending job |
| A4 | pending jobs |

## Result

`Host::ranges_` (include/journal/host.h) is a `std::vector<host_detail::Ranges>`, one multimap
and sweep threshold per loop. `group_io(g)` and `publish(g)` touch only table `loop(g)`, on that
loop's thread, which already owns g's table shard: no lock, so no hold can stall another loop's
commit. `publish` returns at once while the vector is empty (the open's commits, before any view
exists). Mechanism: docs/mechanisms/host-ranges.md. Test: `host_test loop_views`.

Open for the lead: bench/group_io_bench.cc (outside this file set, not in CMake) injects into
`ranges_` as one multimap and no longer compiles at head; it needs `ranges[0].table.emplace(`.
The A3 job builds a sed-patched copy. docs/perf/group-io.md (outside the file set) still
describes `ranges_` as one multimap with `sweep_at_`.

## Rulings I made

1. Per-loop ownership over a mutex: a mutex over one shared table holds for a full sweep, about
   89 ms at 1M random ids (job 261118-145320-05f3b3's per-call `erase_if` column), stalling every
   other loop's commit. A3 measures the mutex arm too.
2. The rule constrains `group_io` to the group's loop thread, stated at the method. Any other
   thread already raced with the loops' commits, so no valid use changes; Host's API is unchanged.
3. The A1 mutant is the shared table without a lock (every group to table 0), kept off the branch
   under `refs/mutants/journal-host-ranges-race/shared-table`.
