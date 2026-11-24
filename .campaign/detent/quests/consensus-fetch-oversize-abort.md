# consensus-fetch-oversize-abort: rpc_host_fetch on macOS: an oversize fetch ends connection_aborted, not message_size, and takes a concurrent fetch with it

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state is
in `state.json`: `campaign status consensus-fetch-oversize-abort`.

## Outcome

`consensus_transport_rpc_host_fetch` fails on CI's macos-x86_64 on main (run 45366654906 at
consensus:512e3ef) and on quest branches that change no transport code (run 45407143806 at
consensus:8f21d10, twice). The failing line: "48 MiB over 32 MiB Software caused connection abort,
five 4 MiB at once: 0 ok, 4 refused FAILED" (rpc_host_test.cc:880-902): the oversize fetch ends
`connection_aborted` instead of `message_size`, and one of five concurrent 4 MiB fetches ends with
neither data nor `busy`. Find the cause (the server's oversize path closing the link while other
calls are in flight, a race in how the refusal is sent, or the test's expectation) and fix the
cause: an oversize request is refused with `message_size` and never costs another call on the link.

## Acceptance criteria

- A1 The cause, named with a trace or probe that reproduces it (a forced ordering or a stress job);
  a test that fails without the fix, by value: `--fails-without`.
- A2 rpc_host_fetch passes 200 runs in a row on this host as a light job, beside 4 burners.
- A3 [artifact] The mechanism doc for the oversize refusal says what a caller sees and that other
  calls on the link are unaffected.
- A4 The suite passes and `ci:ci` is green at the landed commit, macos-x86_64 included.

## Contracts

The transport's error for an oversize request is `message_size`; other calls on the link continue.

## Decisions

Lead, 2026-11-24: its own quest, first in line: it keeps CI red on consensus main and blocks
consensus-driver-pipelined-sim's A3. It takes precedence over consensus-fetch-alongside-loop-stall,
which shares src/transport/**.

## May land early

Not applicable.

## Tasks

None.
