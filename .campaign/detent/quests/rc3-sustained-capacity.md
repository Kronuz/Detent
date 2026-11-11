# rc3-sustained-capacity: RC-3: sustained capacity and the published operating envelope

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rc3-sustained-capacity`. Planned from `docs/RELEASE_PLAN.md` RC-3 and its row "Sustained capacity", the release blockers in `docs/handoffs/README.md` (SC-1 to SC-4), and the lead's plan (M4 row);
the criteria are the plan's own, not new ones.

## Outcome

Detent publishes an operating envelope measured on the actual topology and host, with refusals preserved.

## Acceptance criteria

- A1 Ingestion calibration of the 500k-key producer, SC-4's waits attributed (RC-3; `docs/handoffs/ingestion-waits.md`): exclusive jobs
- A2 The raw and zstd 500, 2k and 10k-session ladder (RC-3): exclusive jobs
- A3 Per-hop traffic and retry amplification (RC-3): jobs
- A4 A mixed-pressure soak at the supported operating point (RC-3): a soak job
- A5 Sustained 5M qualification (RC-3): exclusive jobs
- A6 The exploratory 10M trial if resources permit; refusals and interrupted runs kept as results; it does not raise the supported ceiling (RC-3): jobs
- A7 SC-2 and SC-3 segment storage and compaction costs resolved at release scale (`docs/handoffs/segment-storage.md`, `docs/reviews/scaling-2026-10-23.md`): jobs
- A8 SC-1: the relay snapshot lifecycle's sustained capacity envelope (`relay-snapshot-lifecycle` left it open): jobs
- A9 [artifact] The operating envelope published, with its topology, host and measured limits (RC-3 closure): `docs/CAPACITY_QUALIFICATION.md`

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. No AWS (D16): local VMs and the laptop under `quiet exclusive`; A6 depends on available resources.
2. The lead splits A1 to A8 into quests when M4 starts; A9 is the gate.

## May land early

None until the lead splits it.

## Inputs to weigh at split/start

- Xapiand triage #27 (msg-13): no spin-locked hand-off queues in the authority append path; the ingestion calibration measures the write path with write-heavy rows and a profile.
