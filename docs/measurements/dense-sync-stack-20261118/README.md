> Status: CURRENT (2026-11-18) The kept 5M authority fixture and how it was seeded. Maintainer: Detent campaign.

# The 5M fixture

`/Users/kronuz/code/Kronuz/Detent/.scratch/capacity-trials/5000000-flat-r1` holds three authority voters (`state-0`..`state-2`) seeded with 5,000,000 records through the flat writers. It sits in the home checkout's `.scratch`, outside every job checkout, so job cleanup does not remove it. Build it once; arms read it and never rebuild it.

- Reading: `coldboot.py` clones each `state-N` with `cp -cR` into the arm's output, so an arm never writes the fixture. An arm that would write must copy it first.
- Seeding: `seed.py TREE BUILD SCENARIO CAPACITY OUTPUT`, one `light` job (the 2026-10-24 capacity run took 5,484 s); scenario `.scratch/capacity-trials/5000000.json`, capacity `tests/detent_system/presets/capacity-5000000.json`.
- `seeded.json` in the fixture names the record count, revision, seed seconds and the seed tree's commit.

Each fixture's `seeded.json` gives its records and revision; the seed seconds are the seed job's.

| Path | Size | Records, revision | Seed seconds | Seed commit | Seed job |
| --- | ---: | --- | ---: | --- | --- |
| `.scratch/capacity-trials/5000000-flat-r1` (Dense, S2, stacked, A/A) | 579 MiB | 5,000,000, 39064 | 4,578 | Detent:9852a3c | 261118-064312-81a63c |
| `.scratch/capacity-trials/5000000-flat-main` (main) | pending | pending | pending | Detent:ea1fddc | 261118-070640-2fb122 |
| `.scratch/capacity-trials/100000-main` (main, 100K) | 10 MiB | 100,000, 784 | 75 | Detent:ea1fddc | 261118-070638-b711be |
| `.scratch/capacity-trials/10000-main` (main, smoke) | 1 MiB | 10,000, 80 | 8 | Detent:ea1fddc | 261118-070638-b711be |

Main runs on its own fixtures of the same scenarios. Its detentd cannot read the Oct-23 or `5000000-flat-r1` state: a state seeded before 2026-11-12 fails to replay ("authority recovery application fenced", service.cc:1983), while a fresh 10K fixture seeded at main boots main (probe 261118-064844-08680a) and the old one boots at Detent:d04794d and its parent-of-53abd637 tree (261118-064904-6c2712, 261118-064907-e7158a). The 53abd637 and 711a114e probes failed to build, so the first incompatible commit is not named. Seeding: `seed-fixture.sh SCENARIO=FIXTURE`; main's arms: `submit-main.sh RECORDS ROUND SEED_JOB...`.
