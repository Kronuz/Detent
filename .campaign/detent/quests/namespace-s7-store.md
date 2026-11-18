# namespace-s7-store: detent-agent publishes and recovers through the agent store

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s7-store`.

## Outcome

detent-agent publishes every generation through the namespace-s7 agent store (D8): DiskStore publishes and recovers through AgentStore, the table replaces `selected`, readers read their table entry, AgentStore reclaims superseded generations, and the old layout is gone (D43, no migration). Until namespace-s6-cutover the one table entry is named after the configured scope, and the old Cursor maps to NamespaceCursor (D88).

## Acceptance criteria

- A1 A real agent run (authority, relay, agent) publishes and serves generations from the store; the readers' answers equal the authority's: the suite
- A2 A power cut at every storage primitive of the agent's publish path leaves a store that recovers to the last acknowledged generation or the one before, never a mix: the crash model job
- A3 The agent's old layout code is deleted; nothing outside agent_store writes the agent directory: the suite plus a grep job
- A4 AgentStore gains a `.lease` and `collect()`: superseded generations are reclaimed as DiskStore reclaims them today, never one a reader holds: a test that fails without it
- A5 Catch-up (ring entries, a trimmed block, a base) runs on an opened AgentStore recovered from a mid-publish cut and reaches the full copy's commitment (namespace-s7 durability F3): the suite
- A6 [artifact] How the agent's publish path uses the store: `docs/status/namespace-s7.md`: "## Wiring"

## Contracts

Consumes the agent store (`include/detent_local/agent_store.hh`, D8).

## Decisions

D8, D43, D88. The dropped namespace-s7-wire's survey: its journal at Detent:b765d598.

## May land early

Nothing.
