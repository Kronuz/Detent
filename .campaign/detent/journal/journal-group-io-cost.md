# journal-group-io-cost: journal

State: ready for review
Quest: .campaign/detent/quests/journal-group-io-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Bench written and committed in the code worktree (journal:dcbd791, bench/group_io_bench.cc); jobs queued.
Local 1K smoke (Apple clang, unversioned, not evidence): 6.4 ns per id, intercept 333 ns.

What group_io iterates (include/journal/host.h, Host::group_io): every call runs
`std::erase_if(ranges_, expired)` over `ranges_`, a std::multimap<group id, weak_ptr<Range>> holding one
entry per group view ever handed out and still alive (a view is a PrefixIO whose scan closure owns the
Range). The sweep drops entries whose view died, so the table does not grow without bound; it visits
every live entry on every call (a pointer chase per node into the weak_ptr control block), so a call is
O(live ids) plus O(log) for the emplace and the `resident` lookup. Why: ranges_ is keyed by id for
publish() (a table commit pushes the new incarnation/mark/high to every live view of the id through
equal_range); expiry is reaped lazily on group_io rather than on view destruction (the views die on any
thread; ranges_ is the loop's, unlocked).

## Next

1. Lead: review and land journal:878c43d (docs/perf/group-io.md); fix quest is journal-group-io-sweep.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261118-072911-17102d (exclusive; light dry run 261118-072910-b78abc) |
| A2 [artifact] | `docs/perf/group-io.md` at journal:878c43d (table, command, commit, fit): "The slope is 138.6 ns per id, dominated by the 1M point." |

## Result

group_io is linear in live ids and convex per id. Median ns/call, random ids: 1K 4,763; 10K 147,173;
100K 4,871,407; 1M 135,846,060 (ascending: 4,215; 67,680; 1,133,686; 23,822,221). Fit 138.6 ns per id,
r^2 0.9965. Per-id cost 4.8 -> 135.8 ns (random) because the multimap outgrows cache (about 176 B per id,
16 MiB L3); inferred from sizes, no miss counters. Doc: docs/perf/group-io.md. Fix: journal-group-io-sweep.

## Rulings I made

1. Setup injects the N live ranges into Host::ranges_ directly (explicit-instantiation access, no Host
   change) instead of calling group_io N times: group_io's own sweep makes building N ids quadratic
   (1M ids: about 5e11 node visits). A population built through group_io at 1K and 10K validates that
   the injected table times the same.
2. No table entries: live ids are views (ranges_ entries), which is what the sweep walks. MemoryIO has
   a few files only (layout, one log), so its directory sync costs nothing. A resident-id control
   (create_group once, then group_io on that id) covers publish().
3. The exclusive job runs only the bench (--build compiles under light); the cap for exclusive is 10 min.
