# managed-relay-ipv6-flake: journal

State: waiting on jobs 261121-225424-d93fe2 261121-225431-dfdab9
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

1. When 261121-225424-d93fe2 (unloaded x20) and 261121-225431-dfdab9 (4 copies x8 + 4 burners, after the first) finish, read which fail and their kept .scratch/stage6 events; name the cause; fix; --fails-without job.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>

## Jobs

- 261121-142317-81f60a and 261121-185318-61a635 timed out (the burner `wait` hang above, not the test); 261121-142320-2b70fc and 261121-185320-67e210 skipped. Resubmitted with the fixed script: 261121-225424-d93fe2 (x20 unloaded, 45 min) and 261121-225431-dfdab9 (4 copies x8 + 4 burners, after the first, 45 min), both at the quest's 6c3950842adc, pre-fix (no fix exists yet).
- 261121-131440-d68a15 failed (stale SDK/libc++ include error on a reused .scratch/build); 261121-131903-946009 and 261121-131904-868125 were skipped. Resubmitted as 261121-133855-33a40b and 261121-133912-8a5eb2, each configuring a fresh build in $JOB_OUT the way CI does (llvm, CMAKE_OSX_SYSROOT).
- 261121-133855-33a40b failed too: my script's -DCMAKE_OSX_SYSROOT put the SDK's usr/include before libc++'s (-isystem), breaking <cwchar>. Dropped the sysroot and explicit compilers; resubmitted as 261121-142317-81f60a and 261121-142320-2b70fc, default toolchain, fresh build in $JOB_OUT.
