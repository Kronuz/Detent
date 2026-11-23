# managed-relay-ipv6-flake: journal

State: ready for review (proposal: close as not reproducible; A1-A3 cannot be met)
Quest: .campaign/detent/quests/managed-relay-ipv6-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Reading the three failed CI runs' artifacts (downloaded to /Users/kronuz/code/Kronuz/Detent/.scratch/managed-relay-ipv6-flake/<run>/). Findings so far:
- All three: `managed agent 0 did not converge` after the 30 s selection_timeout (managed.py wait_selected); the same CI run also failed test_system_managed_poll (44869473548) or test_system_managed_relay (44877822656), so it is load on the macos-x86_64 runner (4 vCPU, ctest --parallel 2), not IPv6.
- events.jsonl: both relays get the first bundle, agents publish the seed, then ~6.5 s into the 12 s write phase every relay request to the leader hits `delivery network deadline` (client timeout_ms 2000), then two `not_leader` (the followers), repeating every ~2.2 s until the test ends, long after the writers stop. Leader health: 16 writer requests in flight (reserved_body_bytes 4 MiB), publisher_admitted_to_ready max reaches 2.0 s, distribution_export_disconnected climbs.
- Open question: why the relay never recovers once writers stop. Needs the local reproduction.
Stress jobs 261121-142317-81f60a and 261121-185318-61a635 timed out (30, 90 min) with no result. Cause, found by running the script locally: `seq 1 0` on macOS counts down (prints 1 and 0), so BURNERS=0 started two endless spinner subshells and the bare `wait` never returned. The test itself passes in 35-41 s per run (local, 1 and 2 concurrent copies); 20 runs are about 13 min. Also fixed in stress.sh (/Users/kronuz/code/Kronuz/Detent/.scratch/managed-relay-ipv6-flake/stress.sh): burners and rounds loops are C-style, `wait` waits only on the copy loops, results and kept logs go to $JOB_OUT (the old mktemp in .scratch failed in a fresh checkout), each round runs under `timeout -k 10 240` and prints pass/FAILED with seconds, and the test command is the last `Test command:` line of `ctest -N -V` (the first is the fixture setup). Verified locally: 2 copies x2 rounds + 1 burner ends cleanly. The quest has no fix yet; these are the pre-fix runs.

## Next

1. The lead rules: close as not reproducible (recommended), or widen the repro.

## Blocked on

Nothing. Lead chose option 1.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | cannot be met: the failure does not reproduce, so no cause is proven. Evidence so far: the three CI runs' events.jsonl (journal Doing): `managed agent 0 did not converge` after 30 s selection_timeout, the same CI run also failing another managed test, every relay request to the leader hits `delivery network deadline` during the 12 s write phase; "load, not IPv6" is a hypothesis |
| A2 | cannot be met (nothing fails pre-fix): jobs 261121-225424-d93fe2 (0 of 20 failed) and 261121-225431-dfdab9 (0 of 32 failed) |
| A3 | cannot be met (no fix). The pre-fix repro CI run 45116414557 passed 10 repeats each of test_system_managed_relay and _relay_ipv6 on macos-x86_64 and linux-x86_64-gcc |
| A4 | the quest branch carries no code change over main; CI baseline on the repro tree is run 45116414824 (queued for runners when written; its source-checks, pin-check, linux jobs passed) |

## Result

Not reproducible. No code changed.

- Original failures: CI 44877100446 (warm-relay-agent-boot, 610565fc), 44877822656 (loop-idle-waits, 7d1fad45), 44869473548 (journal-idle-groups, c2e83169), all macos-x86_64: `managed agent 0 did not converge` after the 30 s selection_timeout; relay requests to the leader hit `delivery network deadline` during the write phase and never recover; each run also failed another managed test (load on the 4 vCPU runner with ctest --parallel 2).
- Now: 0 failures in 52 local runs (jobs 261121-225424-d93fe2, 20 serial, and 261121-225431-dfdab9, 4 copies x8 with 4 spinners; 30-62 s each) and 0 in 2 x 10 CI repeats (run 45116414557, macos-x86_64 and linux-x86_64-gcc, `ctest --repeat until-fail:10 --parallel 2 -R 'relay|relay_ipv6|poll'`, 21-28 s each on macOS). The test_system_managed_poll named in 44869473548 no longer exists.
- Plausible fixes, after the three failing trees (none is an ancestor of any of them): Detent:59206705 "Relay: visit every request each turn and continue only on progress" and Detent:563c5c31 "Relay: keep main's per-turn shares and sleep after an idle round" (relay turn scheduling; the symptom was relay requests stalling and not recovering), landed 21 Nov 02:33 and 05:03 PST. Detent:87f69a56 and Detent:0ab699fa (diagnostics: voters, agent convergence times) landed later and would name the cause if it recurs. Not proven: nothing was bisected.
- Cannot be met: A1 (no cause), A2 (no failing run), A3 (no fix). A4 is CI of an unchanged tree. Proposal: close as not reproducible; reopen if the failure recurs with the new convergence diagnostics.
- Left open: the repro workflow existed only on the deleted branch quest/detent/managed-relay-ipv6-flake-repro (never merged).

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>

## Jobs

- CI repro: run 45116414557 (Repro workflow) and 45116414824 (CI) on branch quest/detent/managed-relay-ipv6-flake-repro. The ctest arguments live in the shared Kronuz/cpp-libraries cmake.yml with no input for repeat, so the repro adds its own .github/workflows/repro.yml.
- 261121-142317-81f60a and 261121-185318-61a635 timed out (the burner `wait` hang above, not the test); 261121-142320-2b70fc and 261121-185320-67e210 skipped. Resubmitted with the fixed script: 261121-225424-d93fe2 (x20 unloaded, 45 min) and 261121-225431-dfdab9 (4 copies x8 + 4 burners, after the first, 45 min), both at the quest's 6c3950842adc, pre-fix (no fix exists yet).
- 261121-131440-d68a15 failed (stale SDK/libc++ include error on a reused .scratch/build); 261121-131903-946009 and 261121-131904-868125 were skipped. Resubmitted as 261121-133855-33a40b and 261121-133912-8a5eb2, each configuring a fresh build in $JOB_OUT the way CI does (llvm, CMAKE_OSX_SYSROOT).
- 261121-133855-33a40b failed too: my script's -DCMAKE_OSX_SYSROOT put the SDK's usr/include before libc++'s (-isystem), breaking <cwchar>. Dropped the sysroot and explicit compilers; resubmitted as 261121-142317-81f60a and 261121-142320-2b70fc, default toolchain, fresh build in $JOB_OUT.
