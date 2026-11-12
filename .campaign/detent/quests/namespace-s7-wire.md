# namespace-s7-wire: Namespace S7 wiring: the agent publishes and reads through the agent store

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s7-wire`.

## Outcome

detent-agent publishes every generation through the namespace-s7 agent store (`include/detent_local/agent_store.hh`, D8): one directory, the table, derived generation and segment ids, verified reuse, tombstones. DiskStore, the coordinator, the segment builder and the readers read and write through it, and the agent's former private layout is gone (D43, no migration).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 A real agent run (authority, relay, agent) publishes and serves generations from the store; the readers' answers equal the authority's: the suite
- A2 A power cut at every storage primitive of the agent's publish path leaves a store that recovers to the last acknowledged generation or the one before, never a mix: the crash model job
- A3 The agent's old layout code is deleted; nothing outside agent_store writes the agent directory: the suite plus a grep job
- A5 Catch-up (ring entries, a trimmed block, a base) runs on an opened AgentStore recovered from a mid-publish cut and reaches the full copy's commitment (namespace-s7 durability F3): the suite
- A4 [artifact] How the agent's publish path uses the store: `docs/status/namespace-s7.md`: "## Wiring"

## Contracts

Consumes namespace-s7's `agent_store.hh` API. Provides the agent's on-disk layout for namespace-s8 (the relay mirror) and namespace-s9-s10.

## Decisions

D8, D43, D54, D55, D58.

## May land early

None: it changes the agent's on-disk layout.
