# ci-test-flakes: journal

State: waiting on jobs (all at Detent:afef9684, listed under Jobs)
Quest: .campaign/detent/quests/ci-test-flakes.md (the lead's; never edit it)

## Doing

Session 3 (2026-11-17). Merged origin/main (2504f7d7, clean). Read session 2's six jobs, fixed two new
causes (8: dns open timeout, 7: scrub-carry measurement), moved every forcing to a patch applied at the
head so A1 runs on the head's code, and resubmitted every row's evidence at afef9684.

Session 2's jobs: dns forced 261117-075836-62302f 0 of 3 failed, mutant 261117-075844-98c9ef 3 of 3
("node 2 exited at startup", bind: Address already in use); multivoter forced 261117-075852-a4ca60 0 of 3,
mutant 261117-075900-8b25f2 3 of 3 (TLS handshake timed out at the stalled leader). Both forcings now
force. dns x200 261117-075907-93d492: 8 of 200 failed, all printed ones a read timeout on the /v2/open
reply (cause 8). Scrub probe 261117-080150-81c8e3 (30 runs): cause 7 below; the lead's trigger hypothesis
is refuted (the finished pack carried is always 408,495 bytes).

Forcings are patches in ~/.scratch/ci-test-flakes/patches, applied by scripts/patched.sh in the job's
checkout at the head (the slot's next checkout --force drops them): `<cause>-forced.patch` (window, fix
kept) must pass, `<cause>-mutant.patch` (window, fix removed) must fail. The older forced/ and mutant/
branches are superseded. Scratch worktree for patch edits: ~/.scratch/ci-test-flakes/forcing.

## Causes

5. paged artifacts (A4): `PagedDeltaPlanBuilder::take()` resets `work_`; the Work owner and the input
   delta's RetiredInput retire on the RetirementRuntime thread and free account bytes after `take()`
   returns, so a late retirement reopens headroom after the test's `admit(limit - used)`. Fix: wait until
   the plan is the domain's only resident before admitting. Forced: 50 ms sleep after the admission.
3. authority service: a settled read's Admission stays held until the owner retires its task (per-turn
   budget); the reader producer is at its cap of 16, so the 16 terminal readers threw admission_full.
   Fix: wait until /v1/health `requests == 0`. Product by design; no contract change. Forced: the last
   reader's reply released 200 ms after the loop.
6. snapshot catch-up: a 2 s client socket timeout escaped `successful()`; with one voter killed a loaded
   host holds a commit longer. Fix: transport error = outcome unknown, retry with the same producer
   sequence. The retry now lives in `test_multivoter.settled()`. Forced: the leader SIGSTOPped 3 s at
   sequence 50.
4. dns voters, startup: ports reserved then released before the daemons start; another process can bind
   one. Fix: `bind_collisions()` / `with_fresh_ports()`. Forced: a socket bound on node 2's 127.0.0.1 port.
8. dns voters, first write (new): under 4 copies the leader's /v2/open reply outlasts the 2 s socket
   timeout (8 of 200, 261117-075907-93d492, test_dns_voters.py:183). Fix (e568a381, afef9684): `settled()`
   retries with the same nonce, asking each attempt which voter leads; the test checks generation 1.
   Forced: the leader SIGSTOPped 3 s as the open is sent.
7. scrub-worker (A6): the coordinator's scrub status keeps the old root's byte count while the scrub
   still hashes the old root after the selection moves (coordinator.cc scrub loop: `continue` while the
   selection differs from the scrub's root), until a later tick rebases. The test read "at the delta" at
   `recoverable == 2`, so old-root bytes hashed in that gap counted as after the swap. Probe 81c8e3: pack
   order is fixed (408,495, 1,048,511, 1,048,347 bytes); the first pack is always the one carried, so the
   check's slack is 408,495 - 200,000 = 208,495 bytes, about 1 s at the test's rate (old_bytes / 12).
   Run 29 shows the gap exactly: status 1,119,519, rebase 1,146,298, after 2,370,300 = 2,343,521 + 26,779.
   Failing a909be: after 2,560,872 = 2,343,521 + 217,351, a 217,351-byte gap. Fix (f9858e88): take the
   count at the status's first change after the selection, which is the count at the rebase (the rebase
   tick's step hashes 0 bytes: credit restarts). Forced: the coordinator keeps the old root for 250,000
   more bytes before rebasing.
1, 2, A5 all-down: no failure in 200 runs each at session 1's fix; resubmitted at the head.

## Candidate flakes (not fixed)

- `test_relay_isolated_process`, macos-x86_64, CI run 44122714524 attempt 1 (job 134181601557, passed on
  attempt 2): `test_managed_process.sh` line 159, 33.35 s: the empty-bundle agent never reported
  `durable revision=1` within 30 s. agent-empty.log is empty (not even "awaiting its first generation",
  so its coordinator never initialized or the process died); authority and relay drained cleanly; the
  retained relay-status.json is from readiness (sessions 0). Cause not clear from the fixture: it does not
  capture the agent's `--status` output or exit on that failure. The fixture is outside this quest's file
  set; a first step is to print both on failure. Logs: ~/.scratch/ci-test-flakes/ci-44122714524.

## Moved

- A7 (consensus `peers_test.cc:1196`) moved to quest peers-retry-flake (lead, 2026-11-17); the lead drops
  it from this quest. Session 2's cause analysis is in that quest's hands.

## Next

1. Read the jobs below; every forced passes and every mutant fails; soaks 0 failures.
2. Then set State "ready for review".

## Jobs (all at afef9684)

A1 forced (pass) / mutant (fails without): scrub 261117-122918-255da8 / -122940-785822; dns-port
-122947-5cd036 / -122956-1bafd6; dns-open -123003-eaa3f7 / -123009-ebb1c4; multivoter -123014-166bcb /
-123021-adcde1; service -123028-8e93b6 / -123035-8eee38; paged (TSan) -123041-89e38a / -123048-36df58.
A2 (4 at once): scrub-worker x48/49/51/52 261117-123108-b7afee -123114-33f21c -123121-45fad3
-123128-667c34 (also A6); catchup -123137-cc170b -123144-16f6e7 -123151-41b258 -123158-27e45f; dns
-123217-0a3036 -123224-0d0ffa -123233-6bcaed -123241-dcbf68; relay -123249-ac5226 -123256-467ef1
-123305-9ea67c -123312-cdcd46; failover -123319-2ce179 -123326-eae445 -123333-255579 -123339-b03646;
paged x200 -123346-58311d; service x200 -123353-4b22d1.
A4: paged TSan x100 261117-123401-a48568. A5: all-down TSan -123408-9f7a1e -123415-218d06 -123423-6308af
-123430-21eeff. A3: pins 261117-123441-a5f929, native suite 261117-123504-e38040.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: forced and mutant pairs above, at afef9684 |
| A2 | pending: soaks above, at afef9684 |
| A3 | pending: 261117-123441-a5f929 (pins), 261117-123504-e38040 (suite) |
| A4 | pending: 261117-123401-a48568 (TSan x100), mutant 261117-123048-36df58 |
| A5 | CI: `.github/workflows/ci.yml` failure_artifacts gains `.scratch/stage6/*/*.jsonl`, `*/*.json`, `*/*/*.json`; drivers remove a passing run's directory. Soak pending (above) |
| A6 | pending: cause 7; forced 261117-122918-255da8, mutant 261117-122940-785822, x200 (A2 scrub jobs) |
| A7 | moved to peers-retry-flake |

## Result

<pending>

## Rulings I made

1. Flakes 3-8 are test bugs: each test assumed a release, a free resource or a fresh measurement the
   product does not promise at that instant.
2. Forcings and mutants are patches applied in the job's checkout at the head, not branches, so A1's
   evidence runs the head's code.
3. System test drivers remove their stage6 run on success so CI's upload holds failed runs only.
4. test_dns_voters names its scratch directory by pid, as test_multivoter does.
5. One retry for a lost reply, `test_multivoter.settled()`, serves both multivoter and dns voters.
6. A6 is fixed in the measurement, not the trigger: the probe refutes the trigger hypothesis.
