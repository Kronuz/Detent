# consensus-replica-delete: A replica that leaves a group removes its state, so it can rejoin fresh

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-replica-delete`.

## Outcome

Xapiand's placement drains and replaces replicas (msg-187). journal gains group delete in xapiand:journal-group-delete; this quest gives consensus's Driver the matching step: a replica removed from a group's membership closes, deletes its log and image through the store, and a later add of the same node starts it as a fresh learner. A crash mid-delete leaves the old replica or none.

## Acceptance criteria

- A1 A node removed from a group and added back rejoins as a fresh learner and catches up by snapshot or log: jobs
- A2 A crash at every disk operation of the delete recovers the old replica or none, never a half: a faultfs job
- A3 A mutant that skips the delete fails A1: a --fails-without job
- A4 [artifact] docs/DRIVER.md states the leave and rejoin contract
- A5 CI passes

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

- 2026-11-21, Xapiand lead (proposed, final when xapiand:journal-group-delete fixes it): `journal::Host::delete_group(std::uint64_t group)`, synchronous like create/reopen/close_group; refused with a named error unless status(group) is closed and holding(group) is false; durable order: a table record marks it deleted, then segments, manifest and artifacts go; a power cut leaves the whole old group or none, and open() finishes a recorded delete; a later create_group(group) starts empty and other groups keep every acknowledged record. Consensus side: a replica removed from membership closes its group, calls delete_group, and rejoins as a fresh learner.
- 2026-11-22, Xapiand lead (msg-246, fixed at journal:d89528f, under review): `void journal::Host::delete_group(std::uint64_t group)` on the group's own loop right after close_group, no handoff; std::logic_error unless closed, std::system_error(EWOULDBLOCK) while a pin is held; the entry commits as state `deleted` (4) before segments and manifest go, open() finishes a recorded delete. The entry keeps its incarnation and base as a floor, so a recreated group's first record is TurnDriver::retired(group)+1, not 1; consensus must not assume a fresh group's log starts at 1 (Journal::create_shared handles it for Store users). Doc: journal docs/mechanisms/host-delete.md.
