# groups-upload-stall: journal

State: working
Quest: .campaign/detent/quests/groups-upload-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Getting a macos-x86_64 `upload` failure with socket samples (they exist from consensus:5923b6c).

## Next

1. Read probe run 44319694813 and main run 44318999208 (5923b6c) for an `upload` failure.

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
  .scratch/probe/ci.yml in the code worktree. Dispatch 1: 44319694813 (3 runner jobs of ~6 allowed).

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
