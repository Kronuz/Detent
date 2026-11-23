# journal-host-ranges-race review: correctness

Commit: journal:0202ebc059adec7a4ad5fd9f7e74e28b8f82063f
Verdict: APPROVE
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- `261118-185127-cf5001`: `journal_host_test loop_views` under TSan (RelWithDebInfo, so NDEBUG:
  the TSan result is TSan's, not the assert's) at 0202ebc, exit 0.
- `261118-185129-a1418f`: the shared-table mutant (journal:7cf0101, a one-line change of
  `owned_ranges` to `ranges_[0]`, assert left in but compiled out) under TSan, exit 134. I read
  its log: 63 race reports, among them `publish` (host.h:514, from `create_group` -> `update` ->
  `commit`) on one thread against `group_io` (host.h:325) on the other, plus `group_io` against
  `group_io`. So the test drives exactly the A1 pair, not only two inserts.
- `261118-185131-950d48`: the same mutant in a debug build aborts on the `owned_ranges` assert
  (host.h:524), so the debug check catches a shared table on its own.
- `261118-185126-82265f` (debug suite, asserts live: no CMAKE_BUILD_TYPE) and
  `261118-185126-2eeb43` (TSan suite; journal_host 9.68 s against its 30 s timeout) at 0202ebc.
- `261118-185140-2b12ac`, `261118-185141-16e1a8`: journal_host 5.9-7.5 s, loop_views 2.0-2.8 s,
  debug and TSan, on a host at load 15-35.

## What I checked myself

Reading, all at 0202ebc (`include/journal/host.h`, `include/journal/journal.h`,
`include/journal/store.h`, `include/journal/artifact.h`; consensus at 84608cc):

1. Ownership. Every access to a table goes through `owned_ranges(g)` = `ranges_[loop(g)]`
   (host.h:520-527): `group_io` (host.h:305) and `publish` (host.h:514). The only other touches are
   `ranges_.resize` at the end of `open` (host.h:601), the unlocked `ranges_.empty()` read in
   `publish` (the vector header, immutable after the constructor), the destructor, and the
   test/bench `Steal`. `commit(slots)` publishes only ids in `shards_[slot]`, and
   `loop(g) = loop(slot(g))`, so a loop's commit reaches only its own table; the only multi-slot
   commits (`mark_lost`, re-partition) run in the constructor while `ranges_` is empty. Callers:
   `group_io` from `Journal(Host&, g)`; `publish` from `update`, run by `create_group`,
   `reopen_group`, `close_group` and `next_artifact`. `next_artifact` runs from
   `ArtifactBuildState`'s constructor (artifact.h:139), built synchronously by `prepare_artifact` /
   `begin_artifact_preparation` (journal.h:170-175, store.h:450,475) on the caller, the worker's
   loop; the async artifact operation's completions never name an artifact. `Hold`'s destructor
   (any thread) reaches only `unhold`, which does not touch `ranges_`. Views hold their `Range`
   by `shared_ptr` and read it under `Range::mutex` (host.h:330), never the table, so a scan on a
   completion worker or a view outliving a close is safe. No path found where another thread
   touches a table.
2. Lifecycle. `loops_` changes only in `open`/`repartition` (host.h:587, 657), before `ranges_` is
   sized, so a group never changes tables within a Host's life; a new Host starts with empty
   tables. Close: `update` publishes the closed entry to held ranges, then erases the closed
   entry from the shard; a later `group_io` sees `resident == nullptr` and skips every range
   with `incarnation != 0`, so it gets a fresh unpublished `Range` (host.h:313-327). Same code as
   main with the table index added; `closed_view` and `closed_reopen` pass in 82265f.
3. The check. `owner` is assigned and asserted only under `#ifndef NDEBUG`; the `owner` member is
   present in both modes, so the struct layout does not differ between TUs. With NDEBUG,
   `owned_ranges` is an index. It checks "one thread per table for the host's life", which is
   the property that rules out the race, whichever thread comes first.
4. Tests. `loop_views` (host_test.cc:385-427): two threads released together; each creates its 20
   groups in its first 20 iterations and keeps taking views until both have finished creating
   (`i < viewed || done < 2`), so each thread's `publish` overlaps the other's `group_io`. 2,000
   views per loop with every fourth held, so each table sweeps. The value check (every view taken
   before its group's create lists the first artifact) fails if `publish` misses the view's table.

Probe job (consumer, debug, asserts live):
`261118-222012-a53140`: consensus 84608cc in Debug against journal 0202ebc
(`FETCHCONTENT_SOURCE_DIR_JOURNAL`), running the tests that use `journal::Host` (driver,
shared_*, wave_open, waiter, blocking_entry, simulation, transport_groups/rpc/peers). Script:
`.scratch/detent/review-journal-host-ranges-race-correctness/consensus-debug.sh` (`build`, then
`test`). Still queued when I wrote this; see F1.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | No consumer has run with the new debug check. Consensus pins journal 4272229, so nothing changes until a bump. Statically, consensus calls `group_io` and the transitions only on a loop's single `io.run()` thread (groups_test posts `Driver::open` to its loop). The debug check is stricter than the race needs: a consumer that runs one loop on several threads, serialized, would trip it in debug, as ruling 4 notes. | probe `261118-222012-a53140` (pending); groups_test.cc:106-108, 917-925 | Read a53140 before the consensus bump. If it trips, give that bump quest the finding. |
| F2 | no | nit | host-ranges.md "The callers" says consensus `Driver::open` is on the loop because "`on_loop()` asserts it". `on_loop()` checks only that any current task runs on the Driver's scheduler (`!task::current() \|\| ...`), so a plain call from a thread running no task passes. The guarantee comes from the callers posting to the loop, and in debug from `owned_ranges`. | consensus driver.h:514-516; host-ranges.md "The callers" | Say that callers post `open` to the loop, and that `owned_ranges` is what checks the thread. |
| F3 | no | nit | `create_group`, `reopen_group`, `close_group` and `next_artifact` now abort in debug off their group's loop thread, through `publish`, but their declarations do not say "on the group's loop" the way `group_io` and `hold` do. The rule is stated only at `ranges_` and in the mechanism note. | host.h:390-430 | Add "On the group's loop" to the transitions' comment block. |
| F4 | no | nit | The journal is stale. State still reads "waiting on jobs", and Next says A3's numbers (base 766 / mutex 695-770 / per-loop 781-786 ns at 1M random) go into docs/perf/group-io.md and host-ranges.md, which 0202ebc does not have. | journal "Doing"/"Next"; job 340039 | Record them or drop the step. |
| F5 | no | nit | `#include <thread>` sits between `<mutex>` and `<optional>`, out of the block's alphabetical order. | host.h:45 | Move it. |

## Not run

- consensus, strata or Xapiand against this journal head, beyond probe a53140 (pending at
  writing).
- No platform but macOS. No GCC TSan. CI at 0202ebc is "not run at head" per `campaign status`;
  A4 needs it before landing.
- The A3 performance claim, which is outside this lens. I only read 340039's table.
