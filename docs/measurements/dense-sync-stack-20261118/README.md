> Status: CURRENT (2026-11-18) The kept 5M authority fixture and how it was seeded. Maintainer: Detent campaign.

# The 5M fixture

`/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials/5000000-flat-r1` holds three authority voters (`state-0`..`state-2`) seeded with 5,000,000 records through the flat writers. It sits in the home checkout's `.scratch`, outside every job checkout, so job cleanup does not remove it. Build it once; arms read it and never rebuild it.

- Reading: `coldboot.py` clones each `state-N` with `cp -cR` into the arm's output, so an arm never writes the fixture. An arm that would write must copy it first.
- Seeding: `seed.py TREE BUILD SCENARIO CAPACITY OUTPUT`, one `light` job (the 2026-10-24 capacity run took 5,484 s); scenario `.scratch/capacity-trials/5000000.json`, capacity `tests/detent_system/presets/capacity-5000000.json`.
- `seeded.json` in the fixture names the record count, revision, seed seconds and the seed tree's commit.

Size, seed commit and job id: see the journal's Result and the table below once the seed job has finished.

| Path | Size | Seed commit | Seed job |
| --- | --- | --- | --- |
| `.scratch/capacity-trials/5000000-flat-r1` | pending | pending | pending |
