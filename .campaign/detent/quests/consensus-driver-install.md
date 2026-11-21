# consensus-driver-install: A Driver group installs snapshots: transfer frames, validation, and the image at activation

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-driver-install`.

## Outcome

A group run by a consensus Driver installs snapshots end to end (msg-179, option (a); Xapiand's group-heal, then cluster, then release, needs it). Today a Driver group runs no SnapshotChannel, nothing reads Worker::source_chunk or frames RFTXFR01 for it, the host transport carries no transfer frames to or from it, the receiver has no reserve/offer path or application validation hook, and ActivateInstall carries a token and a boundary but not the image (consensus b8de3b8, main 8801b99). After this quest the Driver keeps owning its Workers and exposes one per-group install surface: transfer frames in and out over the host transport, a validation notice the application answers with validated / validation_failed, and the image readable at ActivateInstall. strata stages the whole transfer at validation, before consensus persists the install, so a crash leaves the old replica or the complete new one (strata:0e67dcb7, quest/xapiand/group-heal--install).

## Acceptance criteria

- A1 A Driver group behind a lagging follower installs a snapshot over the host transport and the follower then applies entries past the boundary: tests
- A2 The application's validation_failed rejects the install and the follower keeps its old state; validated installs it: tests
- A3 A crash at every persisted step of an install recovers either the old replica or the complete new one: a simulation job, with a --fails-without mutant
- A4 driver_simulation runs installs (lagging and restarted followers) under its seeds: jobs
- A5 [artifact] The contract, frames and calls: `docs/DRIVER.md` install section
- A6 TSan, ASan and CI pass

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
