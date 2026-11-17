# ci-test-flakes: journal

State: waiting on jobs 261117-075836-62302f 261117-075844-98c9ef 261117-075852-a4ca60 261117-075900-8b25f2 261117-075907-93d492 261117-080150-81c8e3
Quest: .campaign/detent/quests/ci-test-flakes.md (the lead's; never edit it)

## Doing

Session 2 (2026-11-17). A4 is landable now as an increment (below). Reworked the dns and multivoter
forcings (session 1's mutants passed: forcing ineffective), fixed the dns test's shared scratch directory
(the x200 soak's 194 failures), and probing A6's cause.

A4 early landing: `campaign land ci-test-flakes --upto 9b85fdf6 --reason "test-only: the paged pressure
admission waits for the builder's retirement; TSan x100 and the forced mutant pass at 9b85fdf6"`.
At 9b85fdf6: TSan x100 261116-212942-bf6c81 (0 of 100), x200 under load 261116-212945-cb8567, pins
261117-075503-907e48; the mutant (forced 50 ms window, no settle) 261116-212936-ca55cf fails 10 of 10 runs
(TSan); the forced window with the settle 261116-212939-5508b7 passes. Main ce7faa41 TSan 7 of 100
(261116-195707-ca2128, deps-workpool-repin's probe). Only the test file changes.

Session 1's results read: dns mutant 261116-213935-7c388e and multivoter mutant 261116-213942-5baa52
passed (no failure), so neither forcing forced anything. dns: the thief bound 0.0.0.0 and asio's acceptor
sets SO_REUSEADDR, so on macOS the daemon's 127.0.0.1 bind succeeded; now the thief binds 127.0.0.1.
multivoter: pausing the surviving follower does not hold a reply past 2 s (the leader answers non-200
first); now the leader itself stalls 3 s between `leader()` and the write, so the client's read times
out. dns x200 261116-214054-2671e3 failed 194 of 200: the ctest entry has a WORKING_DIRECTORY, so
concurrent copies shared `.scratch/dns-voters-names` (mkdir FileExistsError, a sibling's rmtree); fixed
in a0593a31 (per-pid directory). Suite 261116-214438-45b59f exit 125: the cif-suite slot's checkout was
broken (infrastructure); A3 is met by pins, the suite is CI's.

Branches (local only), at a0593a31: `forced/cif-dns` b4e2cc78, `mutant/cif-dns` 9dadff1f,
`forced/cif-multivoter` 9bd34c33, `mutant/cif-multivoter` fa2db7dc, `probe/cif-scrub` 9dded741. Older:
`forced/cif-{paged-fix,service}`, `mutant/cif-{paged,service}`, `probe/cif-service`. Scripts:
~/.scratch/ci-test-flakes/scripts. Scratch worktree for branch edits: ~/.scratch/ci-test-flakes/forcing.

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
7. scrub-worker (A6), hypothesis under probe 261117-080150-81c8e3: `PackScrub::rebase` keeps only packs
   the pass finished; the pack in progress restarts. The test requests the delta at `largest + 65536`
   hashed, which guarantees only 64 KiB of finished packs, but checks a carry above 200,000. Failing run
   (a909be): hashed 1,124,926 at the delta, 2,560,872 after = new root 2,752,016 - 191,144, i.e. one
   small finished pack carried and ~934 KiB of a large pack in progress redone. Fix in the test (the
   trigger, so the finished packs exceed the margin), once the probe confirms pack sizes and order.
1, 2, A5 all-down: 200 runs each passed at the fix (A2, A5 jobs); no failure to read, cause not named.

## Next

1. Read the six jobs above. Forced must pass and mutants fail; then dns x200 for A2.
2. A6: from the probe, name the cause, fix the trigger in test_repair.cc, force it (delta requested just
   after a small pack completes) with a mutant, then 200 repeats under load.
3. 1, 2 (relay, failover): no failure in 200 runs each; ask the lead whether A2 plus no reproduction closes
   them, or soak longer on CI's runner class.
4. A5 CI artifact path: needs a CI run with a failing system test after push (lead's).

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

1. A7 (consensus, not mine to edit). Cause, from CI run 44074119964 attempt 1 (677c864, macos-x86_64):
   `fetch_failures` sets `options[0].fetch_deadline = 500ms` so the frozen fetch fails fast, and that
   deadline is the whole reply's (`Peers::await_reply`), so the retried 8 MiB fetch must arrive in 500 ms
   (16 MB/s over TLS through 1 MiB budgets); a loaded x86 runner misses it and the fetch completes
   `timed_out` within the test's 3 s wait. The CHECK at 1196 prints no error code, so this is inferred,
   not observed. Options: (a) the retry fetches a 1 MiB file (the drain over the same lanes is still
   proved by the 8 MiB stalled reply); (b) force it with a reader delay over 500 ms on the retry to
   prove the timed_out path, then (a); (c) rule that consensus-transport's lane port (D106) replaces
   the bulk-lane fetch and drop it. Recommend (b) then (a), with the CHECK printing the error.
2. A4 early landing: the lead's call (command under Doing).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending jobs above |
| A2 | pending jobs above |
| A3 | pending 261116-214438-45b59f |
| A4 | 261116-212942-bf6c81 (TSan x100 at 9b85fdf6), mutant 261116-212936-ca55cf, pins 261117-075503-907e48 |
| A5 | CI: `.github/workflows/ci.yml` failure_artifacts gains `.scratch/stage6/*/*.jsonl`, `*/*.json`, `*/*/*.json`; drivers remove a passing run's directory. Soak pending |

## Result

<pending>

## Rulings I made

1. Flakes 3-6 are test bugs: each test assumed a release or a free resource the product does not
   promise at that instant (asynchronous retirement, transport timeouts, released ports).
2. Forced reproductions live on local `forced/` and `mutant/` branches, not in the tests.
3. System test drivers remove their stage6 run on success so CI's upload (and local disk) holds failed
   runs only.
4. test_dns_voters names its scratch directory by pid, as test_multivoter does: ctest runs it in a fixed
   WORKING_DIRECTORY, so concurrent copies collided.
