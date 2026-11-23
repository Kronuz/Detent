# loop-idle-waits: detentd and detent-relay: replace 1 ms idle polls with wakes on work

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status loop-idle-waits`.

## Outcome

detent-relay's host turn (src/detent_relay/host.cc:398, 407) and detentd's authority service (src/detent_authority/service.cc:2030, 2046, 3182, 3317) re-arm a 1 ms timer when a turn makes no progress, so an idle server wakes 1,000 times a second. After this quest an idle loop sleeps until work arrives (a post on enqueue, or a wheel wait at the next real deadline), per the server-composition note's F6 (Xapiand review, 2026-11-19). A mixed shape stays only with a D62 cost case.

## Acceptance criteria

- A1 An idle detent-authority and an idle detent-relay make no loop wakeups over 10 s beyond their real deadlines (counted wakeups, before and after at named commits): jobs.
- A2 A test posts work to an idle loop and sees it served within one turn, with no timer involved; it fails at a mutant that drops the wake (--fails-without): jobs.
- A3 Throughput and p99 of the relay and authority benches stay within 5% of main (strict, before and after): jobs.
- A4 The suite, TSan and CI pass: jobs.

## Notes

High risk: concurrency (a lost wake hangs a server). Filed from the Xapiand lead's server-composition review F6 (msg-92).
Widened 2026-11-19 by the untracked-commitment audit (R5): detent-agent's delivery coordinator polls a worker's `ready` flag every 1 ms (`src/detent_delivery/coordinator.cc:253, 316, 446`; reviews/namespace-s7-store-durability.md:50 F8); A1 and A2 cover it too.
