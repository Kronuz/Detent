# seeding-waits: Seeding waits attribution

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status seeding-waits`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/seeding-waits.md`.

## Outcome

An evidence note answers why the 100K mixed seed takes 104 s: serialized file and directory sync completion occupies most measured seed time. No implementation is proposed.

## Acceptance criteria

- A1 [artifact] The 100K seed's time is attributed: serialized file and directory sync completion occupies most of it: `docs/status/seeding-waits.md`
- A2 [artifact] Both 10K current-source runs apply 80 transactions and verify the full 10,000-record state: `docs/status/seeding-waits.md`
- A3 [artifact] No durability or library default changes on this evidence alone: `docs/status/seeding-waits.md`

## Contracts

None.

## Decisions

1. Exact attribution needs instrumented sync syscalls and request stages in a 10K diagnostic run; grouped syncs (namespace S1/S2) followed.

## May land early

None.
