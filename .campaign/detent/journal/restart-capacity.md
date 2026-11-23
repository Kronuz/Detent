# restart-capacity: journal

State: ready for review (jobs 261122-212055-71549d 261122-212058-43a074 261122-212102-17f15d 261122-212105-5555a9 261122-212108-72f300 261122-212119-72d34c; CI at the pushed head)
Quest: .campaign/detent/quests/restart-capacity.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 for the correctness review (Detent:23001977, CHANGES) at Detent:c68022e8: R1-R6.

## Next

1. Read the jobs: unit passes; each `--fails-without` mutant fails at its assertion, not at
   build; the reviewer's probe at the fix shows m2 and m3 caught.
2. Read CI at the pushed head (macos-arm64 is the gate suite; ASan/UBSan, TSan, Docs for A6).
3. `campaign accept restart-capacity correctness --confirm R1=... R2=... R3=...` is the lead's.

## Blocked on

Nothing. The Docs check-docs failure (stale `image_bytes`, `state_bytes` exceptions) is fixed
after the lead added `tests/docs/docs_exceptions.txt` to the file set; check_docs.py reports 0
stale locally.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | unit 261122-212055-71549d (Detent:c68022e8); mutants --fails-without 261121-212136-f963d5 (keys), 261122-212102-17f15d (R2, state_bytes) |
| A2 | unit 261122-212055-71549d; mutants --fails-without 261121-212139-b4d7b2 (image), 261122-212058-43a074 (R1, administrative), 261122-212105-5555a9 (R3, fetch refusal) |
| A3 | unit 261122-212055-71549d; system 261122-212119-72d34c |
| A4 | system 261122-212119-72d34c (rerun of 261121-212132-5fb9e1 at Detent:c68022e8) |
| A5 | [artifact] `docs/mechanisms/authority-capacity-raise.md` "Every use of `State::limits_`" |
| A6 | `ci:macos-arm64`, `ci:linux-asan-ubsan`, `ci:linux-tsan` at the pushed head; the Docs workflow is read by the land gate |

### Fixes since the review

| Finding | Fix | Confirming job |
|---|---|---|
| R1 | test_node.cc `restart_raise_layout`: a `SetLayout` that got `Conflict` under old `image_bytes` stays in the log tail and replays with no pending policy after the raise; proposed again it applies | 261122-212058-43a074 (m2 on Detent:33b7a8be, --fails-without); probe rerun 261122-212108-72f300; unit 261122-212055-71549d |
| R2 | test_state.cc `restart_raise`: a group failing `Capacity` on `state_bytes` 512 replays the same under 4096; a new group then applies | 261122-212102-17f15d (m3 on Detent:3fd00cd8, --fails-without); probe 261122-212108-72f300 |
| R3 | node.cc `check_group_policy` refuses the in-force layout above a peer fetch, naming `image_bytes`; test_node.cc `restart_raise_layout` last phase; mechanism doc section, CAPACITY_RUNTIME.md, configuration.md | 261122-212105-5555a9 (check disabled on Detent:5195c190, --fails-without); unit 261122-212055-71549d |
| R4 | configuration.md: managed agent paragraph moved above `## Raising authority capacity` | [artifact] file |
| R5 | CAPACITY_RUNTIME.md: groups and administrative entries decide; an image is admitted | [artifact] file |
| R6 | mechanism doc Peers: "an image the receiver admits" | [artifact] file |

Failed, superseded (not evidence): 261121-135043-e4073f and 261121-160253-0a4d67 (node SEGV, fixed
at Detent:2f64e0fd); mutants 261121-160304-c2e302, 261121-160309-c65da6, 261121-134017-55739f and
261121-134021-7685c2 ran with the node test already crashing; unit 261121-212128-2b5201 and system
261121-160259-a5c5c9 and 261121-212132-5fb9e1 ran earlier code.

## Result

Each DTABAT02 group, DTAADM01 entry and DTAIMG03 image is read under `admit_limits`: keys,
state_bytes and image_bytes may have risen, the other seven equal; anything else refuses naming
the field. Groups decide under their carried limits (`BatchDecoder::limits()` into the
`ValidatedCommand`), administrative entries under `recorded_limits(payload)`; recovered state runs
and captures under the configured limits. No format, wire or consensus change. How it works and
the site classification: `docs/mechanisms/authority-capacity-raise.md`. Operator text:
`docs/CAPACITY_RUNTIME.md`, `website/.../reference/configuration.md` "Raising authority capacity".
A voter under whose limits the group's layout policy would write history files above a peer fetch
refuses after recovery and after each administrative entry (`check_group_policy`, node.cc), naming
`image_bytes` (mechanism doc, "The layout in force under raised limits").

## Rulings I made

1. Raisable set is contract rule 1's three (keys, state_bytes, image_bytes), not all ten: the
   outcome's "keys and the other nine" reads as the ten carried limits. The other seven bound what
   relays, agents and history and wire decoders accept, which rule 1 keeps equal. Widening is one
   flag per field in `limit_fields` (state.cc). The lead confirmed it (below).
2. One table (`limit_fields`) drives write, read and admission; messages name the configuration
   key (`application_limits.<field>`).
3. Sync paths (`State::apply`, `apply_batch`, `administer`, `BatchApplication::advance`, the
   four-argument `AdministrativeApplication`) apply under the state's own limits; only the node's
   replay passes carried limits. No node.hh change (outside the file set).
4. Superseded by the lead's R3 ruling: the in-force layout is checked under the configured
   limits (ruling 8).
5. Mutants are commits on local branches `mutant/restart-capacity-a1` and `-a2`, one assertion
   each, run with `--fails-without`; rebased onto Detent:84cf2357 (a1 9a433e5b, a2 7a5c6f54).
6. A fenced startup names the worker's fence reason (`fence_reason`, service.cc), for all three
   refusals (storage initialization, recovery application, committed profile); the prefixes stay.
   The failed job 261121-135043-e4073f (Detent:e3f48e0b) is the run without it.
7. The node test's segfault was the quest's own test (Detent:4a703ede), not the merge or
   fence_reason: fixed in the test (Detent:2f64e0fd); no product code changed.

8. R3: the check is `check_group_policy` (was `check_retained_bounds`, same two call sites:
   image recovery and each applied administrative entry), on `file_bytes(configured, pending,
   policy)` or `file_bytes(configured, policy)` against `fetch_bytes(configured)`. It throws
   `runtime_error` like its sibling retained-bound refusal. The message names `image_bytes`
   because `fetch_bytes` is `max(image_bytes, default-policy files)`: only `image_bytes` lifts it.
   It cannot fire without a raise: an entry deciding under the configured limits already refuses
   such a policy (validate_administrative, SetLayout `Conflict`).
9. R1/R2/R3 mutants are commits on local branches `mutant/restart-capacity-r1` (m2, Detent:33b7a8be),
   `-r2` (m3, Detent:3fd00cd8), `-r3` (fetch check disabled, Detent:5195c190), each on Detent:c68022e8.

Cancelled, superseded (not evidence): 261121-134013-9953d2, 261121-134216-f7b3a0, 261121-134612-884a01.

- Lead ruling (2026-11-21) on ruling 1: contract rule 1 in docs/ONLINE_CAPACITY_ARCHITECTURE.md governs. Only keys, state_bytes and image_bytes rise, and only upward; the other seven stay equal. The outcome's "keys and the other nine" names the ten limits each entry carries, not ten raisable ones.
