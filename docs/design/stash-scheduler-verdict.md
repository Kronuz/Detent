# Is there a reason to keep stash and scheduler?

**Verdict: ARCHIVE AFTER Xapiand drops its scheduler pin and http-log bumps its logger pin.**
No reason to keep either survives the evidence: nobody outside the Kronuz repositories uses
them, every capability with a user has a replacement, the one region where stash measures
faster has no user, and archiving keeps every published link working. Research for quest
`archive-stash-scheduler`, 2026-11-21, at stash:5288b9b, scheduler:6560a42, wheel:075a40a,
task origin/main, logger:18be8a3, Xapiand:e8986d831, http-log:5e3df31, prism:5487d43.

## 1. Users

Grep of every main checkout under `~/code/Kronuz` (the `superseded-libraries.md` recipe plus
`scheduler::scheduler`, `debounce_or_run.h` and `.gitmodules`; no submodule names either):

| User | Where | State |
|---|---|---|
| Xapiand | `CMakeLists.txt:546-555` (scheduler block, pin `d57e8fc` at `:552`), `:946-948` (stash include loop), `:969-973` (`STASH_TRACE_HEADER`); `src/xapiand_fsyncher.h:42,72` (the `FS--` debouncer), `src/database/wal.cc:44-45`, `src/debounce_or_run.h`; `src/manager.h:40` is a dead include (no `Debouncer` in `src/`; the autocommit is `control::Debounce`, `manager.cc:300`, D58) | **live**: the WAL fsync debouncer is the last code user |
| http-log | `CMakeLists.txt:23`, logger pin `f61d8df` (2026-11-03), which still fetches scheduler | **standalone builds only**: prism and Xapiand define `logger::logger` first, so the guard skips it |
| Detent | `docs/design/async-drivers-bench/CMakeLists.txt:22,41` | evidence harness for `docs/perf/wheel-vs-stash.md`; resolves after archive, not a blocker |
| logger, prism, Detent's build | logger dropped scheduler at logger:b121f2e (2026-11-12); prism pins logger:529ae76, Detent logger:6e7ab9e, Xapiand logger:455cad0: none fetch scheduler | gone |

Which Xapiand quest removes the pin: `journal-replaces-wal` (active, 1 of 7 criteria) deletes
`src/database/wal.*` and `src/xapiand_fsyncher.h`, the last code users, but its outcome does not
name the CMake blocks or trace headers. The step that names them, `fsyncher-on-task`
(`Xapiand/docs/design/lifecycle-adoption.md:216-228`, A4 "`CMakeLists.txt` names no scheduler or
stash"), is a DRAFT design step and not filed as a quest. So no filed quest owns the pin today.

GitHub (`gh api`, Kronuz token): stash and scheduler have 0 stars, 0 forks, 0 watchers; 1 and 3
page views in 14 days; 5,003 and 4,934 clones (2,807 and 2,788 unique), which match CI and
FetchContent traffic (wheel, used by CI the same way: 9,392). Issues: stash #1 and scheduler
#1-#2, all opened by Kronuz; no outside issue or PR. Code search for `Kronuz/stash`,
`Kronuz/scheduler.git` and `github.com/Kronuz/scheduler` returns only Kronuz repositories.

## 2. Coverage

stash (`docs/API.md`, `stash.h`):

| Capability | Replacement |
|---|---|
| Lock-free multi-producer insert by integer key | wheel `arm_remote`: one CAS on the thread's stripe, no allocation; 9-23x cheaper per arm (`wheel-evidence.md`) |
| Drain in key order up to a key (walk) | wheel `expire(now)`: never early, at most one tick late |
| Non-consuming scan (peep) | none; its one user, `SchedulerQueue`'s next wakeup (`scheduler.h:242`), is wheel `next()` |
| Any pointer-like value type, boxed per add | none; wheel holds intrusive 32-byte entries. No repository stores non-timer values in stash |
| Compile-time levels (`Div`/`Mod`), `past_horizon` | wheel's fixed levels reach `UINT64_MAX` ns; nothing to refuse |
| `no_memory` result | wheel never allocates |
| Small when empty | none: wheel allocates 65,552 B at construction, stash 9,248 (blog, "What stash still does better") |
| Memory bound under re-arm | wheel is better: 0 B heap per re-arm vs 27-101 B; stash's RSS grew 351 MB over 24 h x 1 M keys (`stash-niche.md:162`) |
| Cancel cost | stash's flag clear is 0.66 ns vs 3.7 ns, but the whole life is 3-26x cheaper on wheel |
| Fire lateness above ~110 M arms/s | none: stash 1.02 ms worst vs wheel 3.48-4.08 ms at 4-14 producers; no current user arms near that rate (logger, the busiest, is already on wheel) |
| Tracing hooks, C++17 | none; every consumer is C++20 and Xapiand's trace header leaves with the pin |

scheduler (`scheduler.h`, `debouncer.h`):

| Capability | Replacement |
|---|---|
| `Scheduler` (inline runner), `ScheduledTask::clear()` | a loop-owned wheel with the caller's cancel flag, as logger does (logger:b121f2e) |
| `ThreadedScheduler` onto a `WorkPool`, `post()` | wheel owner plus `workpool`; task's executors |
| `Debouncer`: quiet, busy, randomized min/max force, throttle | `task::debounce` (`task/task_timers.hh:208-330`) with the same five timings plus `fire_pending()` |
| `schedule_result` (`past_horizon`, `no_memory`, `stopping`) | wheel cannot refuse; `task::debounce`'s `stop` keeps pending keys |
| `callable_traits.hh` | no includer outside scheduler |

The only region where stash measured faster: `stash-niche.md:139-140`, 24 h horizon, 1,000
fire-and-forget keys, 1 and 4 producers, r = 2.30 and 1.73, at 7.4x the wheel's bytes per key,
and lost at 14 producers (r = 0.558). No user has that shape; the rule there was fixed before the
rows and stash kept no use. No new job is needed: the deciding measurements exist
(`wheel-evidence.md` jobs 261111-144149-2a64b9, -144151-4c40f1; `stash-niche.md` 261111-231503-9f6729
through -231505-db629f).

## 3. Standalone value

Real, and archiving keeps it. stash is a distinctive technique (append-only leaves, an announce
board for in-flight inserts, a Python model in `tools/model/`), with `ARCHITECTURE.md`,
`docs/API.md` and benches. Three published posts link the repositories (all return 200): The
Sparse Wheel (6 links, two pinned at stash:a42e173), Who Owns the Wheel (2) and The Bag of Holding
(1). An archived repository stays readable and clonable at the same URLs, so no link or pinned
code reference breaks, and FetchContent pins keep resolving (D73). Who Owns the Wheel says "as of
this update neither repository is archived" and lists the conditions; archiving completes that
post rather than contradicting it. As a maintained library neither has a reader: no star, fork
or outside clone signal, and scheduler's own open issue #1 shows a mutex plus `priority_queue`
beating its `add()` at one producer.

## 4. Cost of keeping

- One shape per mechanism: three timer mechanisms (stash, scheduler, wheel) where D70 and D72
  chose one; keeping either needs a measured gap, and section 2 has none with a user.
- 1,000-line rule (owner, 2026-11-21, `Xapiand/.campaign/xapiand/quests/split-check.md:8`):
  `stash.h` is 1,095 lines and no `split-stash` quest exists among the 27 split quests.
- CI: since 2026-10-22, 30 stash runs (548 wall minutes) and 66 scheduler runs (906 minutes, 8
  failed). scheduler's main is red today: run 44937845353 failed at Configure, a 403 cloning
  stash. The central nightly skips archived repositories by itself
  (`cpp-libraries/tools/nightly_dispatch.py:170`, `isArchived: false`).
- Pin drift: scheduler pins stash b7d8876, five commits behind its main; Xapiand pins scheduler
  d57e8fc, behind "Move to workpool" (scheduler:25c7a2c).

## What archiving requires, in order

1. Xapiand drops scheduler and stash: `CMakeLists.txt:546-555`, `:946-948` (the `stash` entry),
   `:969-973`, `src/xapiand_{scheduler,stash}_trace.h`, `src/debounce_or_run.h`,
   `src/manager.h:40`, and `docs/src/content/docs/architecture/dependencies.md:36-37,62`. The
   Xapiand lead files `fsyncher-on-task` (or widens `journal-replaces-wal`'s file set to these),
   so a filed quest owns A4; if the fsyncher survives, its A2 strict A/B is Who Owns the Wheel's
   condition 2.
2. http-log bumps logger from f61d8df to a revision at or after logger:b121f2e (prism's 529ae76).
3. Re-run the users grep (`superseded-libraries.md`, "Reproduce"): only the two repositories and
   Detent's evidence harness remain.
4. Commit an "Archived: superseded by wheel and `task::debounce`" note at the top of each README,
   and close scheduler issue #1 as superseded; an archived repository takes no commits or edits.
5. Owner approves; the lead runs `gh repo archive Kronuz/stash` and `Kronuz/scheduler`.
6. cpp-libraries README (rows at `:161`, `:171`, the list at `:227-228`) and AGENTS.md entries
   mark them archived; `superseded-libraries.md` updates pins, archive state and cluster (A2).
7. The owner updates Who Owns the Wheel's "neither repository is archived" line.
