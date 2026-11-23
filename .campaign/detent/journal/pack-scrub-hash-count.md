# pack-scrub-hash-count: journal

State: waiting on jobs 261119-144306-5c63f3 261119-144312-8d0f2c 261119-144316-8e7e71
Quest: .campaign/detent/quests/pack-scrub-hash-count.md (the lead's; never edit it)

## Doing

Fix is pack_scrub.cc:a800bbfb (count_hash(Pack, 0, count) per slice, (Pack, 1, 0) per digest). Test
counts_what_it_hashes in test_pack_scrub.cc checks exact bytes and calls. Jobs queued: A1 plain test,
A1 mutant (sed removes the count lines; test must fail), A2 suite.

## Next

1. On job completion: check results, fill Evidence, set ready for review.

## Blocked on

Nothing. (Quest says "after namespace-s6-v1-removal"; the lead sequences landing.)

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | plain 261119-144306-5c63f3; mutant --fails-without 261119-144312-8d0f2c |
| A2 | suite 261119-144316-8e7e71 |

## Result

PackScrub now counts hashing as Pack hashes: bytes per slice read, one call per digest taken (match,
mismatch alike; unreadable, missing and rebase-skipped packs count nothing), same as the graph validator
(calls at digest). Hence delivery HashStats.checked covers scrubs. No docs change.

## Rulings I made

1. Kind is Pack (checked counters), as the other pack hashes in the validators.
2. Bytes are counted per slice and the call at the digest, so a pass cut short still counts what it hashed
   (matches bytes_hashed()).
