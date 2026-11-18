# groups-upload-stall: journal

State: waiting on in-window probes (the lead dispatches at 17:30Z, 21:00Z, 02:00Z)
Quest: .campaign/detent/quests/groups-upload-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Getting a macos-x86_64 `upload` failure with socket samples (they exist from consensus:5923b6c).

## Next

1. The lead resumes me with an in-window failing run's id: read its samples, name the cause (A1).
   Or with three green in-window runs: set `upload`'s bound from the clean samples and name the
   runner window as the cause (A2's runner branch). Dispatch nothing myself.

## Blocked on

Nothing.

## Notes

- No macos-x86_64 `upload` failure carries samples yet: every run at or after the sampler passed
  `upload`. Failures without samples (CI logs in .scratch/ci/ of the code worktree): main
  44130104547 (0.0 MiB, 3 beats, gap 4044 ms), 44214025598 (6.0 MiB, gap 3748), 44216299768
  (18.0 MiB, gap 3012); consensus-close-fenced 44261476872 job 134655655042 (07:51Z, gap ~4 s) and
  44264616865 job 134665992662 (08:16Z, 2.0 MiB, gap 4019 ms, host 1's loops join 1.5 s after
  draining). Host stalls 83-111 ms, under the 250 ms retry bar.
- Probe: branch probe/groups-upload-stall (consensus:0156b46 = 5923b6c with ci.yml replaced; never
  lands; dispatch only). Three macos-15-intel shards, each 24 `upload` runs beside a looping
  connection_test, printing `netstat -s -p tcp` deltas and load around each failure. Script
  .scratch/probe/ci.yml in the code worktree. Dispatch 1: 44319694813, 72 of 72 passed (14:02-14:12Z).
  Main at 5923b6c (44318999208) passed too. Dispatch 2: 44324074784 (14:25Z).
- msg (lead, after dispatch 1): could transport-feed-scan itself have removed the stall? No:
  1. Credit already went first before it: 3ba44aa's feed() sends `lane.grants` before control and
     data, as 5923b6c's repay() does; both wait only for the connection's queue to empty.
  2. Control frames stalled with the bulk (3-5 beats in 4 s), and control needs no credit: the whole
     connection stopped, which no credit or scan order explains. Six groups make the scan cost moot.
  3. The tabulation (every macos-x86_64 job since 11-17, logs in .scratch/ci/all/, table.txt):
     ring-feed code failed too, 4c7666a (05:13Z, run 44242758901) and 5eacc91 (05:41Z,
     44245984014), both after 02800e5 and 01e2c06. Pre-feed-scan code passed 12 runs after its last
     failure (c5d15d9, 08:09Z): c20f3fd through abc01bb, 08:26-13:53Z. The 7 failures fall in
     16:49Z-08:09Z by time, not by code version.
  So no probe at 13b32d3 or 303cad1 would fail on the feed: dispatched probe 2 instead.
- Probe 2 (44324074784): 72 of 72 passed. Since 08:09Z: 144 of 144 probe uploads, plus every CI run.
- Time clustering (toward A2's runner branch), macos-x86_64 `upload` per CI job since 11-17
  (.scratch/ci/all/table.txt in the code worktree; feed = whether the commit has 02800e5):

  | Window (UTC) | Failed | Passed | Feed of the failures |
  |---|---|---|---|
  | 11-17 13:06-16:48 (test exists from 13:06) | 0 | 4 | - |
  | 11-17 16:49 - 11-18 08:09 | 7 (44130104547, 44214025598, 44216299768, 44242758901, 44245984014, 44261476872, 44264616865) | 28 | 5 scan, 2 rings |
  | 11-18 08:10-14:25 | 0 | 18 CI jobs + 144 probe uploads | - |

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |

## Result

<pending>

## Rulings I made

- Probe shape: one dispatch = 3 runner jobs x 24 uploads, so "about 6 runs" is two dispatches.
