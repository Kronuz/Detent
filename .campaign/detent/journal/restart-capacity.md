# restart-capacity: journal

State: waiting on jobs 261121-212128-2b5201 261121-212132-5fb9e1 261121-212136-f963d5 261121-212139-b4d7b2
Quest: .campaign/detent/quests/restart-capacity.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round at Detent:2f64e0fd. test_authority_node and _completion crashed (SEGV, test_node.cc:392,
`retried[0].results[0]` on an empty vector) in every unit job since the quest's first, at
Detent:e3f48e0b (261121-135043-e4073f) and Detent:849cea6a (261121-160253-0a4d67); CI showed the
same on all four Linux jobs. Neither the main merge (website and campaign files only, no dependency
pin change) nor fence_reason (service.cc, not linked by the node test) caused it: it came in with
the test itself, Detent:4a703ede. Its `lead()` serviced the recovered voter at a fixed tick 0, so
the election never timed out; the voter stayed a follower and the retry came back NotLeader with no
results. A second fault in the same commit's changed-limits loop: it stopped as the worker fenced,
a turn before the Fenced action reaches the host, so the asserted reason was empty. Fixed
(Detent:2f64e0fd): the voter comes up like the other restart tests (pump, start, pump, asserted
Leader); the refusal loop runs until the reason arrives. Both modes pass under a local ASan Debug
build. Mutants rebased onto it: a1 Detent:eedaff36, a2 Detent:7bc53720.

## Next

1. Read the jobs: each mutant must fail at its assertion in `restart_raise` or test_state, not at
   build; the unit and system jobs must pass.
2. Paste `campaign brief`'s table below; set State to ready for review.

## Blocked on

Nothing. The Docs check-docs failure (stale `image_bytes`, `state_bytes` exceptions) is fixed
after the lead added `tests/docs/docs_exceptions.txt` to the file set; check_docs.py reports 0
stale locally.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1 | unit 261121-212128-2b5201; mutant 261121-212136-f963d5 (`mutant/restart-capacity-a1`, Detent:eedaff36) --fails-without: 3 of 3 abort |
| A2 | unit 261121-212128-2b5201; mutant 261121-212139-b4d7b2 (`mutant/restart-capacity-a2`, Detent:7bc53720) --fails-without: test_state.cc:1046 restart_raise asserts |
| A3 | 261121-212128-2b5201, 261121-212132-5fb9e1 |
| A4 | system 261121-212132-5fb9e1 |
| A5 | [artifact] `docs/mechanisms/authority-capacity-raise.md` "Every use of `State::limits_`" |
| A6 | pending: CI at Detent:2f64e0fd's journal head (suite, ASan/UBSan, TSan); Docs (check-docs, stale exceptions removed) |

Failed, superseded (not evidence): 261121-135043-e4073f and 261121-160253-0a4d67 (node SEGV, fixed
at Detent:2f64e0fd); mutants 261121-160304-c2e302, 261121-160309-c65da6, 261121-134017-55739f and
261121-134021-7685c2 ran with the node test already crashing, so they do not show their assertion;
system 261121-160259-a5c5c9 passed at Detent:849cea6a (replaced by 261121-212132-5fb9e1).

## Result

Each DTABAT02 group, DTAADM01 entry and DTAIMG03 image is read under `admit_limits`: keys,
state_bytes and image_bytes may have risen, the other seven equal; anything else refuses naming
the field. Groups decide under their carried limits (`BatchDecoder::limits()` into the
`ValidatedCommand`), administrative entries under `recorded_limits(payload)`; recovered state runs
and captures under the configured limits. No format, wire or consensus change. How it works and
the site classification: `docs/mechanisms/authority-capacity-raise.md`. Operator text:
`docs/CAPACITY_RUNTIME.md`, `website/.../reference/configuration.md` "Raising authority capacity".
Open: the in-force layout policy is not re-checked under raised limits (mechanism doc, "What it
does not do").

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
4. Not fixed: the in-force layout policy under raised state_bytes (documented). `Node::checked`
   still checks the configured layout.
5. Mutants are commits on local branches `mutant/restart-capacity-a1` and `-a2`, one assertion
   each, run with `--fails-without`; rebased onto Detent:84cf2357 (a1 9a433e5b, a2 7a5c6f54).
6. A fenced startup names the worker's fence reason (`fence_reason`, service.cc), for all three
   refusals (storage initialization, recovery application, committed profile); the prefixes stay.
   The failed job 261121-135043-e4073f (Detent:e3f48e0b) is the run without it.
7. The node test's segfault was the quest's own test (Detent:4a703ede), not the merge or
   fence_reason: fixed in the test (Detent:2f64e0fd); no product code changed.

Cancelled, superseded (not evidence): 261121-134013-9953d2, 261121-134216-f7b3a0, 261121-134612-884a01.

- Lead ruling (2026-11-21) on ruling 1: contract rule 1 in docs/ONLINE_CAPACITY_ARCHITECTURE.md governs. Only keys, state_bytes and image_bytes rise, and only upward; the other seven stay equal. The outcome's "keys and the other nine" names the ten limits each entry carries, not ten raisable ones.
