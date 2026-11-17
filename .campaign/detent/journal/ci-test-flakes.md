# ci-test-flakes: journal

State: waiting on jobs (see Jobs below)
Quest: .campaign/detent/quests/ci-test-flakes.md (the lead's; never edit it)

## Doing

Four flakes fixed in tests (causes below), each with a forced-window branch (fix + forcing, must pass) and
a mutant branch (forcing without the fix, `--fails-without`). CI upload of system-run member output done
(A5 second half). Writer failover (2), managed relay (1) and all-down (A5) have no member logs; soaks at the
fix keep each failed run's stage6 directory in the job's `out/stage6/` and print member tails.

Branches (local only): `forced/cif-{paged-fix,service,multivoter,dns}`, `mutant/cif-{paged,service,
multivoter,dns}`, `probe/cif-service`. Scripts: ~/.scratch/ci-test-flakes/scripts (tsan_loop.sh,
loop.sh, soak.py, sys_soak.sh, sys_soak_tsan.sh, probe_service.sh).

## Causes

5. paged artifacts (A4): `PagedDeltaPlanBuilder::take()` resets `work_`; the Work owner and the input
   delta's RetiredInput retire on the RetirementRuntime thread and free account bytes after `take()`
   returns, so a late retirement reopens headroom after the test's `admit(limit - used)` and `populate`
   succeeds. Probe 261116-195652-960b07 (deps-workpool-repin): sleep50 10/10 fail, fix 0/100, fix+sleep50
   0/10; main ce7faa41 TSan 7/100 (261116-195707-ca2128). Fix: wait until the plan is the domain's only
   resident (`eventually(residents == 1)`) before admitting. Product correct.
3. authority service: a read task with keys goes to the owner's `retired_tasks`, which hold its
   Admission (producer count, request bytes) until retirement drains under a per-turn budget; the
   settled reply's retention is not the last reference. The reader producer sits at its cap of 16 after
   the first 16 readers, so `start_read` for the 16 terminal readers threw admission_full from
   `create_extension` in the test thread (uncaught: Async's constructor). Fix: wait until /v1/health
   `requests == 0`. Product behavior is by design (retired_task_bytes charges the admission); no contract
   change.
6. snapshot catch-up: `successful()` let a client socket timeout (2 s) escape; with one voter killed a
   loaded host holds a commit longer (service timeout 5 s). Fix: transport error = outcome unknown, retry
   with the same producer sequence (the authority reconciles), as writers.py does; the checkpoint poll
   treats a transport error as not yet published. Forced: SIGSTOP the surviving follower for 3 s at
   sequence 50.
4. dns voters: ports reserved then released before the daemons start; another process can bind one in
   between ("bind: Address already in use"). test_multivoter already retried on fresh ports; that check
   and retry move to `bind_collisions()` / `with_fresh_ports()` in test_multivoter.py, used by both.
   Forced: bind+listen node 2's HTTP port on 0.0.0.0 before the first startup.
1, 2, A5 all-down: pending soak data.

## Next

1. Read the jobs (below). For 1, 2 and all-down: read kept runs' member tails, name the cause, force it.
2. Rerun A3 at the final head if more commits land.
3. A5 CI artifact path: needs a CI run with a failing system test after push (lead's): artifact
   `failure-<matrix>` holds `.scratch/stage6/<run>/events.jsonl`.

## Jobs

A1 forced/mutant: paged 261116-212936-ca55cf (mutant), 261116-212939-5508b7 (forced); service
261116-213650-e5d13f (mutant), 261116-213654-5b27ac (forced), probe 261116-213722-86689f; dns
261116-213935-7c388e (mutant), 261116-213938-68c298 (forced); multivoter 261116-213942-5baa52 (mutant),
261116-213945-9a4bcc (forced).
A4: 261116-212942-bf6c81 (TSan x100). A2: paged 261116-212945-cb8567, service 261116-213659-34bc61, dns
261116-214054-2671e3, catchup 261116-214106-ac7091 -214109-472075 -214111-b3b0d5 -214114-8cf29d; relay
and failover 261116-2143{23,26,31,34,39,41,46,48} (alternating relay/failover). A5 all-down TSan:
261116-214329-ce1a38, -214336-69e202, -214344-ba85f1, -214351-63accf. A3: suite 261116-214438-45b59f,
pins 261116-214406-e1d914.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs above |
| A2 | pending jobs above |
| A3 | pending 261116-214438-45b59f |
| A4 | pending 261116-212942-bf6c81, mutant 261116-212936-ca55cf |
| A5 | CI: `.github/workflows/ci.yml` failure_artifacts gains `.scratch/stage6/*/*.jsonl`, `*/*.json`, `*/*/*.json`; drivers remove a passing run's directory. Soak pending |

## Result

<pending>

## Rulings I made

1. Flakes 3-6 are test bugs: each test assumed a release or a free resource the product does not
   promise at that instant (asynchronous retirement, transport timeouts, released ports).
2. Forced reproductions live on local `forced/` and `mutant/` branches, not in the tests.
3. System test drivers remove their stage6 run on success so CI's upload (and local disk) holds failed
   runs only.
