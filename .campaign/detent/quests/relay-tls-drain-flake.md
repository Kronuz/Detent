# relay-tls-drain-flake: relay tests fail together on SSL UNEXPECTED_MESSAGE and an undrained fleet under heavy load

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status relay-tls-drain-flake`.

## Outcome

Seen once in job 261119-181942-21336c (loop-idle-waits head f133251c, macOS arm64, load average ~60): test_relay_mirror_crash timed out at 900 s and test_relay_isolated_process and test_system_fleet_raw failed in the same iteration, all with `ssl.SSLError: [SSL: UNEXPECTED_MESSAGE]` and "fleet admitted resources did not drain to retained slots". Not reproduced in 12 runs each at that head and at main afb5f449. When this quest lands, the cause is named (a test harness race, a relay TLS defect, or a load artifact with its trigger shown) and fixed, or shown to be the harness and bounded.

## Acceptance criteria

- A1 The failure is reproduced (loops under induced load, or a targeted fault) and its cause named from the reproduction's logs: jobs.
- A2 A product cause is fixed with a test that fails without it; a harness cause is fixed so the reproduction passes 50 loaded runs: jobs.
- A3 The suite and CI pass: jobs.

## Contracts

None, if the cause is in the tests; a relay or fleet code cause widens the file set and the review (the lead).

## Decisions

No retry or longer timeout that hides the error: the lead.

## May land early

The reproduction.
