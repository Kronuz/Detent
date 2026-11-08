# Detent Dense pages: plan

## Goals

Complete and optimize Dense pages. One coherent implementation and one format replace version 2, with exact correctness and 100K/5M measurements. Acceptance is A1-A15 in the quest.

## Non-goals

Reader S6 completion, damaged-pack repair, grouped syncs, legacy cleanup, shared-library upgrades and general release qualification remain parked. No AWS or other billable resources.

## Constraints

Keep record full copies and pack checksums. Use current main APIs. Honor quiet holds and the Homebrew pin. Gate native correctness, relevant sanitizers, bounded fuzz/decoder proof and quantitative reader acceptance. Queue extended matrices and soaks later.

## Reserved decisions

The owner decides scope, public behavior removal, full-copy wire changes and any waiver of quantitative acceptance. The game master coordinates implementation, jobs and whole-feature review.

## Milestones

| Milestone | Quests | Validation |
|---|---|---|
| Dense pages | dense-pages | Native, sanitizer/decoder, 100K/5M model and reader acceptance jobs on the complete feature; repeat accepted results on main |

## Decisions

| ID | Date | Decision | By | Why |
|---|---|---|---|---|
| D1 | 2026-11-05 | Only Dense pages; implement before whole-feature reviews, then architect merge ruling | Owner | Previous broad campaign wasted the feature budget |
| D2 | 2026-11-05 | Adopt full-build quest plus reads, deltas and integration tasks | Game master | Components are one persistent-format change and cannot land independently |
| D3 | 2026-11-05 | Preserve conditional contract approval as scoped history | Game master | It authorizes building, not landing |
