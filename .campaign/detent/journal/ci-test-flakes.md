# ci-test-flakes: journal

State: waiting on jobs 261117-234948-483694 -234955-a588f5 -235001-67ee1b (A1 lostwrite mutant,
service-stale pair) and probes -235008-5cd352 -235015-1d92d0; forced/lostwrite 234942-985c20 passed 3/3.
Full `campaign job rerun` held until the probe: a catch-up timeout fix that edits test_multivoter
would invalidate the whole set again.
Quest: .campaign/detent/quests/ci-test-flakes.md (the lead's; never edit it)

## Doing

Session 4 (2026-11-17). Merged origin/main (c6718bce): every afef9684 job now ran other code, so the
whole set is resubmitted at Detent:6a39c344. Session 3's set at afef9684 had three problems:
1. mutant/dns-open 261117-123009-ebb1c4 survived (0 of 3 failed). The forcing SIGSTOPped the leader
   before `open_on_leader()`, whose `health()` probe swallows the stalled node's timeout and returns
   no leader, so no request reached the stalled node and `settled()`'s transport catch never ran. The
   forcing now stalls the leader the probe chose, just before the /v2/open is sent (6a39c344).
2. Native suite 261117-123504-e38040, exit 8: `test_transport_peers.cc:168 (committed && leader)` and
   poll_publisher timeout. Neither binary builds from a file this branch changes (diff vs main: test
   scripts, test_service.cc, test_repair.cc, test_paged_publication_artifacts.cc, ci.yml). The same
   peers abort hit namespace-s6-hardening's native check 261117-075327-c3d9ec at Detent:bfcb4159, no
   ci-test-flakes code in it. Attribution jobs at main's merge-base 2504f7d7 and at the head are
   queued (Jobs). Both files are outside this quest's set.
3. catchup x52 261117-123158-27e45f hit the 30-minute cap (the x51 job took 27.7 min). Resubmitted as
   five jobs of 38-42 runs (200 in all), about 22 min each.
Forcings and runners now live in tests/detent_system/forcing (6a39c344), no longer in ~/.scratch.

Session 4, results of the 6a39c344 set (49 jobs): every forced passed, every mutant failed (dns-open
5c8827 now 3 of 3, scrub 2 of 3, others all). Soaks: scrub 0/200, dns 0/200, relay 0/200, failover 0/200,
paged 0/200, paged TSan 0/100, all-down TSan 0/200; head native suite b4e4d7 284/284 passed.
Three failures at the fix, none the fixed flake:
- service x200 ea3ac4, 1 of 200: `test_service.cc:85 (rejected && !exists(directory))`, not cause 3's
  admission_full. The pid-named state directory of an aborted run (the slot's 15 mutant runs left
  `authority-service-<pid>`) met a reused pid. Cause 9; fix 0f19e635 clears the pid's paths first.
- catchup x40 0d9cee, 1 of 40: line 314 "unconsumed-response transaction did not commit", 13.9 s in;
  not cause 6 (line 304, socket timeout). The test required the unread write to commit within 5 s; a
  leader deposed before it replicates drops it. Cause 10; fix 0f19e635: outcome unknown unless the
  reply says 200, settled by the same-sequence retry that follows.
- catchup x41 eb7651, 2 of 41: exit 124 at ctest's 180 s TIMEOUT, both after the snapshot phase print
  (concurrent runs 24 and 26). The test runs 121-154 s in the suite alone, slowest 125-161 s in the
  other soaks: a budget at 67-89% of its limit, which load crosses. Probes queued for where it goes.
Catchup at the fix so far: 198 runs, 1 lost-write failure, 2 timeouts.
Attribution at main 2504f7d7: native suite f9ab01 exit 8, failed test_authority_dns_voters_unresolvable
and test_authority_snapshot_catchup (line 304: cause 6, the flakes this quest fixes); transport_peers
dc4a00 6 of 40 failed at line 168, vs 0 of 40 at the head 11a288 (peers-retry-flake, main 4cdc080f,
merged in c6718bce, fixes it); poll_publisher 0 of 40 at main 20d577 and head 17f7fe (slowest 79 s,
70 s, against its 90 s TIMEOUT: e38040's timeout was load). Both pre-existing, neither this branch's.
8e1a6b and 2a13e3 were my malformed commands (no test name), not evidence.

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
   Forced: the leader the open's probe chose SIGSTOPped 3 s as the open is sent.
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
9. service, stale state: an aborted run leaves `.scratch/authority-service-<pid>`; a later run with the
   same pid fails the invalid-options check (line 85). Fix: clear the pid's paths first. Forced:
   service-stale-forced.patch creates the directory first.
10. lost write: the unread /v2/transactions to a leader deposed before replicating never commits;
   the test needed it within 5 s. Fix: outcome unknown unless the reply is 200; the retry settles it.
   Forced: lostwrite-forced.patch stalls the leader 4 s (elections 1-2 s) before it reads the write.
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

0. Read probes 5cd352 (x12 at 4-way) and 1d92d0 (x3 alone): which phase grows under load toward the
   180 s TIMEOUT. Fix it in the test at its cause if the test spends it (no wider timeout), else rescope
   to the lead with the numbers. Then `campaign job rerun` the whole set at the new head, with
   lostwrite and service-stale pairs added to A1.
1. Read the jobs below; every forced passes and every mutant fails; soaks 0 failures.
2. Attribute the suite's two failures from the attribution jobs; if pre-existing, report to the lead
   (outside the file set) and rule A3 on the suite's other tests.
3. Then set State "ready for review".

## Jobs (at 6a39c344; commands use tests/detent_system/forcing)

A1 forced (pass) / mutant (fails without): scrub 261117-172346-239143 / -172350-3354ab; dns-port
-172357-dddbbe / -172403-478d87; dns-open -172408-aacc6b / -172412-5c8827; multivoter -172417-aca3ee /
-172422-14bedb; service -172426-99ff6f / -172431-73ba90; paged (TSan) -172436-b6afaa / -172441-20a671
(also A4).
A2 (4 at once): scrub-worker x48/49/51/52 261117-172447-ec24c1 -172453-f64146 -172459-6304f2
-172506-0bc8c1 (also A6); catchup x38-42 -172722-08474d -172727-28a6f7 -172731-0d9cee -172735-eb7651
-172739-1ea4a3; dns -172513-4792c4 -172519-f1cc31 -172524-630370 -172530-c3bf87; relay -172536-507832
-172543-d09106 -172550-5382e5 -172557-24fd53; failover -172603-a35135 -172611-7e520e -172621-ed0fbb
-172629-83cb02; paged x200 -172636-3cf325; service x200 -172644-ea3ac4.
A4: paged TSan x100 261117-172650-0ed378. A5: all-down TSan -172655-f3b09e -172701-043ccc -172706-92b832
-172712-588ac9. A3: pins 261117-172742-fac058, native suite 261117-172717-b4e4d7.
Done by 17:45: forced scrub, dns-port, paged pass; scrub-worker x48 0 failures. 261117-172918-8e1a6b and
-172929-2a13e3 failed on a malformed command (no test name), not evidence; -172923-7663a9 and
-172935-591184 cancelled for the same reason.
Attribution (problem 2): native suite at main 2504f7d7 261117-172940-f9ab01; x40 at 4-way, main / head:
transport_peers 261117-173010-dc4a00 / -173015-11a288, poll_publisher -173020-20d577 / -173024-17f7fe.

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
7. Forcings and their runners live in tests/detent_system/forcing, the only directory in this quest's
   file set; the repository's convention for patches is tests/<area>/mutants/, which would need a
   wider set. The lead may move them.
8. Causes 9 and 10 are fixed in the tests: neither is the product breaking a promise (pid
   reuse of a crashed run's directory; Raft drops an unreplicated write of a deposed leader).