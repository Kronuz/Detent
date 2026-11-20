# loop-idle-waits review: correctness

Commit: Detent:d7de180bc026 (Detent:cc942c46 plus a merge of main bringing rename-detentd)
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 3, from a fresh context, reviewing fix round 2. The F7 fix is right: `owner_absence` catches
every case the last review named, and it never marks a return that main's rule would not. F8 and F9
are closed. One new blocking defect comes from the merge, not from the fix: the A1/A3 bench still
builds and runs `detentd`, which no longer exists at this commit, so A1 and A3 cannot be re-proved
at the head (F10).

## Evidence I relied on

All at Detent:cc942c46, whose quest code equals d7de180b's except the rename (`git diff cc942c46
d7de180b -- src include` is the detentd move and its include paths only).

- 261119-103519-3b65cd, exit 8: absence-merged (the round-2 rule) fails test_owner_absence (log:
  test 163 failed). The other five A2 mutants (644510, c4f26f, 3937ff, 5a6c35, 241774) exit 8.
- 261119-103506-5b36b1 (suite) and 261119-103617-426e37 (wake and absence tests, 20 repeats), exit 0.
- 261119-103541-50a978, -42bdbc, -da90b6, -4f2621: test_relay_service a5 under TSan, 20 runs,
  exit 0. That is the end-to-end witness of the absence wiring (F7's failure was this test).
- 261119-103510-a7a809 and 261119-103520-8e4934 (A1), 261119-103510-0db86e (A3), exit 0.

## What I checked myself

Script: `.scratch/detent/review-loop-idle-waits-correctness/r3_absence_variants.sh`. It compiles
test_owner_absence.cc verbatim against the real `owner_absence` and nine wrong variants, and it
checks the real rule exhaustively (slept 0-40 ms, next up to 60 ms, planned 0-70 ms) against main's
whole gap. Job 261119-125800-b7d5e4 was queued (load 65 on 16 CPUs) when I wrote this. It is the
confirming job, and the conclusions below do not rest on it.

### Q1: the rule (owner_loop.hh:63-67, wired at service.cc:2120-2127, 3290-3292)

`absence = (slept - pass) + max(0, next - max(planned, slept))`. Given pass <= slept <= next (both
are `Clock::now()`, a steady_clock, service.cc:42), `gap - absence = min(next, max(planned, slept)) - slept`.
That is exactly the part of the planned sleep slept through, so 0 <= absence <= gap.

1. Stopped mid-pass: the stop is in `slept - pass`. It is caught.
2. Stopped during the sleep: the lateness is the stop minus whatever planned sleep was left. It is
   caught.
3. A stall split across the pass and the sleep: the parts are summed (60 + 60 = 120 ms). It is
   caught. The last review's "either part alone" would have missed this case, so the sum is stricter.
4. A long planned sleep: it is not counted (R4).
5. Never more lenient than main? Not literally. It is more lenient by the planned sleep slept
   through. With peers, `wake_by` caps that at 1 ms (service.cc:1533-1535), and main's own gap
   included the same 1 ms poll. Without peers the window can be up to 250 ms, but a lone voter has
   no peer frames waiting in its sockets, and that is what `returned` guards against. It never marks
   a return that main would not (absence <= gap).
6. Yields (3248, 3270, 3276) leave `slept_from` at min, so the whole gap counts, as on main. The
   first pass measures from `last_pass` (2095), about 0.

### Q2: coverage

By hand, each variant is killed. The probe confirms it.

1. Counting the planned sleep, or measuring the pass to the deadline: (c) gives 251 ms, want 2 ms.
2. Dropping the pass part: (a) gives 0.1 ms, want 2500.1 ms.
3. Dropping the lateness: (b).
4. The merged rule: (a). Job 3b65cd proves this one.
5. Measuring lateness from the deadline only: (g) gives 16 ms, want 11 ms.
6. Taking the max instead of the sum: (f).
7. Dropping the clamp: (d) goes negative.

"Wrong clock" cannot be written in the function: the signature fixes steady_clock. It can be
written in the wiring, though. For example, `slept_until = Clock::now()` after the `co_await`
zeroes the lateness. test_owner_absence does not see the wiring, and only a5 does, by timing (F11).

### Q3: F8 and F9

1. F8: `Join` is declared after `owner`, `coordinator` and `driver`
   (test_coordinator_teardown.cc:68-72), so a throwing `require` joins the pool before those three
   are destroyed. A second `join()` after line 106's is what the pool's own destructor already does.
   It is closed.
2. F9: every A1 result line records `interval_ms`. a7a809 ran at 60000. 8e4934 ran at 250 with
   `IDLE_INTERVAL_MS=250`, which is configuration.cc:80's default. The journal's pending row names
   both. The row should carry the 250 ms numbers too: detentd 68-70 interrupt wakeups/s and 20 owner
   turns/s, against 10 and 4 at 60000, with main at about 850 (nit F12).

### Q4: round-1 semantics

Fix round 2 touches only the absence lines in service.cc, a pure function in owner_loop.hh, the
teardown test and the bench. `wake.begin_turn()`, the post-`co_await` `scheduled_wakeup` clamp,
`rescan = true`, every `raise()` site and the coordinator's tracked executors (the guard) are
unchanged, and the merge does not touch them. No lost wake is reintroduced, and the round-1 guard
analysis stands.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F10 | yes | defect | After the merge, the A1/A3 bench still names the renamed daemon. compare.sh builds target `detentd`, which no longer exists (the target is now `detent-authority`, src/CMakeLists.txt:118), and pair.py runs `build/src/detentd`. A1 and A3 reruns at d7de180b fail in the build, at head and at the main baseline alike, so these criteria cannot have evidence at the landing commit. It also breaks rename-detentd's A1 rule (`git grep detentd` finds only dated records) once this lands. | bench/idle-wakeups/compare.sh:3,16; pair.py:1,57,126,208-219; idle_loops.py:1,46,62,71; paths.py:3,5 (`git grep -i detentd d7de180b`) | Rename to `detent-authority` in compare.sh and pair.py, and in the labels and docstrings. Test: `campaign job rerun` a7a809, 8e4934 and 0db86e at the fixed commit, all exit 0. |
| F11 | no | gap | test_owner_absence pins the rule but not its wiring in service.cc. A wiring slip, such as `slept_until` taken after the sleep or `slept_from` not reset, survives the unit test. It shows only in test_relay_service a5, by timing. | service.cc:2120-2127, 3290-3292 | Optional: a wiring mutant run against a5 in Release, where a stop usually lands in the 1 ms sleep. |
| F12 | no | nit | The A1 Evidence row should carry both intervals' numbers, and the 250 ms row should read detentd's 68 interrupt wakeups/s against its real deadlines (relay syncs at 4/s). | 8e4934 log | State them in the row. |
| F1 | no | defect | Carried, unchanged: a lone voter's first election can wait for the 250 ms health sleep. | service.cc:3279-3283 | As in round 1. |
| F2 | no | defect | Carried, unchanged: a snapshot install is not counted as relay turn progress (up to 100 ms). | host.cc:428-432 | As in round 1. |
| F3 | no | gap | Carried: no relay test runs host.cc's sleeping turn; each lost relay wake is bounded by the 100 ms health turn. | relay_service_fixture.hh:129-132 | As in round 1. |
| F4 | no | gap | Carried: read test_authority_idle_wake in CI at the landing commit. | CI | As before. |
| F6 | no | nit | Carried: read "no wakeups" from `interrupt_wakeups_per_s`. | idle_loops.py | As in round 1. |
| F7 | no | defect | Closed: `owner_absence` catches a stop mid-pass, in the sleep, and split across both, and never exceeds main's gap. | 3b65cd; Q1 above | None. |
| F8 | no | nit | Closed. | test_coordinator_teardown.cc:68-72 | None. |
| F9 | no | gap | Closed: 8e4934 runs at the 250 ms default, and the interval is recorded. | 8e4934 | None. |

## Not run

- 261119-125800-b7d5e4 (the variant and property probe) was queued when I wrote this. Confirm it
  exits 0.
- The suite at d7de180b (261119-125133-46bdc3, queued) and CI at d7de180b (running).
- A Release a5 run against a wiring mutant (F11).
- The seven formerly hung tests under load (261119-102322-12a601, from round 2): I did not reread it.
