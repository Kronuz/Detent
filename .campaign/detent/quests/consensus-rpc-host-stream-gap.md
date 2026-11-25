# consensus-rpc-host-stream-gap: rpc_host_stream: heartbeats keep every gap under 1,000 ms on slow macOS runners

Risk: normal (transport lanes, a test). The lead writes this quest; contributors read it and never
edit it. Its machine state is in `state.json`: `campaign status consensus-rpc-host-stream-gap`.

## Outcome

consensus CI run 45299903223 (macos-x86_64, consensus-fetch-alongside-flake at consensus:4dbaee0,
whose diff touches only groups_test.cc): `consensus_transport_rpc_host_stream` failed with
"stream: 32 MiB in 1593 ms (21.1 MB/s) ... heartbeats' longest gap 1170 ms" against the 1,000 ms
election timeout. Control-lane heartbeats must not wait behind a Bulk stream. Find whether the gap
is the product (Control frames queued behind Bulk on one connection or loop: a defect that costs
an election on a slow host) or the test (heartbeats posted from a starved thread), and fix that
cause. Related: rpc_host stalled-peer flaked once on the same runner class.

## Acceptance criteria

- A1 A reproduction: the stream case on a slowed loop or CPU-limited run shows a gap over 1,000 ms
  at main, with the cause named by measurement (where the heartbeat waited): a job.
- A2 The fix: the same reproduction keeps every gap under 1,000 ms; a `--fails-without` job at
  main; the test's own bound is unchanged.
- A3 50 runs of rpc_host_stream beside 4 CPU burners, none failing: strict jobs.
- A4 The suite passes and `ci:ci` is green at the landed commit, macos-x86_64 included.

## Contracts

None, unless the cause is lane scheduling; then the Control lane's priority over Bulk is stated in
the transport mechanism doc.

## Decisions

Lead, 2026-11-23: its own quest; one failure so far, not in fetch-alongside's diff.

## May land early

Not applicable.

## Tasks

None.
