# consensus-peers-wake: Peers tells its host when owner work is waiting

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-peers-wake`.

## Outcome

multi-voter-idle-wake (Detent, D124) found that no consensus commit lets a host wake when Peers holds owner work: on a Control frame the Peers reader keeps its input and waits on its own `admission_wake` until the owner calls `admit()`, and `PeerHost` exposes only `worker()` and `channel()` (journal B1, Detent:36c65166). consensus:0acfaff's wake belongs to the Driver; the authority runs a Worker over Peers. After this quest `PeerOptions` carries `wake`, a `std::function<void()>` that Peers calls on its executor whenever a frame or event leaves owner work (and never otherwise), so a host loop can sleep until it is called or a deadline passes.

## Acceptance criteria

- A1 A test: with `wake` set, every frame or event that leaves owner work calls it before the owner could observe that work, and an idle Peers with no traffic never calls it: a job
- A2 A mutant that skips the call on one owner-work path fails A1's test: a job (--fails-without)
- A3 TSan clean on the Peers tests: a job
- A4 [artifact] The wake contract (when it is called, on which executor, what the host may do in it): `docs/mechanisms/peers-wake.md`
- A5 The suite and CI pass

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
