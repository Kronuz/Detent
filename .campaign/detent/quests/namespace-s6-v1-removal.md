# namespace-s6-v1-removal: S6 v1 removal: one cursor, no one-shot or poll mode, agent on v2 sessions

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-v1-removal`.

## Outcome

The agent and every relay consume namespace-s6's v2 sessions, together in one landing (the relay's upstream is the same `Coordinator` the agent runs): one coordinator, one W5 session and one `AgentStore` cycle per response, with per-namespace state as records (design C1); one table entry and generation per followed namespace (D8, not D88's scope-named entry); bounded descriptors and mappings (C2); readers that attach one namespace at a time (C3); readiness and failure scoped per namespace (C4). The agent client's v1 request, one-shot receive and poll mode are removed; the authority's v1 endpoints stay, unused, until namespace-s6-authority-delete (P4 of D94).

## Acceptance criteria

- A1 First task, before freezing limits: at 1, 1,000 and 4,096 namespaces (100 knobs each, plus the 5M namespace in one arm), jobs measure agent descriptors and mappings when ready and during a cycle that republishes every namespace, startup to ready against the 0.3 s restart gate, table encode/decode and SHA-256 per cycle and per reader re-read (against reader T2's 1 ms first-read gate), and RSS and ledger use per idle and per active namespace; the journal states the bounds they set, descriptors O(1) plus attached namespaces among them (design F3, F11): strict or exclusive jobs and a results doc
- A2 Through authority, relay, relay, agent, every followed namespace's cursor and state commitment at the agent equals the authority's at that W1 cursor, including a W7 re-creation and a drop through the chain (design F8): the system suites
- A3 The read path finds its namespace's entry by name; a reader of B keeps its generation and lease through 1,000 publications of A and moves only when B moves; collection waits for held leases (design F4): tests
- A4 The agent is ready when every followed namespace is published or known absent (with `all`: the directory reached and every live granted name published); a namespace failing verification is refetched as a full copy while the others serve (D10); only a refused table fences the store (design F5): tests
- A5 The 5M capacity preset bootstraps and catches up on v2 within its bounds, or an over-bound answer splits across nexts with each next carrying its own cursors (design F7): the capacity system job
- A6 The agent client's v1 request, one-shot receive and poll mode are gone: a grep job and the suites

## Contracts

Consumes namespace-s8-wiring (relays serve W5/W6 over HTTP), namespace-s6-hardening (ledger, staged sink, drop and unfreezable signals) and namespace-local-cursor (the one cursor, layout 2). Provides v2-only consumers, so namespace-s6-authority-delete can remove v1.

## Decisions

D93, D94 (lead, from the design review `reviews/namespace-s6-v1-removal-design.md`): D8 with conditions C1-C4; the old one-landing plan split into P1-P5, this quest is P4, high risk, durability and correctness reviews.

## May land early

Nothing: it switches every consumer's wire.
