# restart-capacity: journal

State: waiting on jobs 261121-135043-e4073f 261121-134615-70d30f 261121-134017-55739f 261121-134021-7685c2
Quest: .campaign/detent/quests/restart-capacity.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code, tests and docs done (Detent:4a703ede..HEAD). Waiting on the unit, mutant, service, system
jobs and CI (pushed) at the quest head.

## Next

1. Read the jobs: each mutant must fail at its assertion in `restart_raise`, not at build.
2. Paste `campaign brief`'s table below; set State to ready for review.

## Blocked on

Nothing. One open question for the lead under Rulings (1): the quest outcome says "keys and the
other nine"; this follows contract rule 1 (only keys, state_bytes, image_bytes rise).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending: unit job 261121-135043-e4073f (at Detent:e3f48e0b); mutant job 261121-134017-55739f on `mutant/restart-capacity-a1` (Detent:fc91b0c9, commands under configured limits) --fails-without |
| A2 | pending: 261121-135043-e4073f; mutant job 261121-134021-7685c2 on `mutant/restart-capacity-a2` (Detent:53faec96, administrative entries under configured limits) --fails-without |
| A3 | pending: 261121-135043-e4073f, 261121-134615-70d30f |
| A4 | pending: system job 261121-134615-70d30f (`tests/detent_system/test_restart_capacity.sh`, at Detent:702fe5f9; inputs unchanged since) |
| A5 | [artifact] `docs/mechanisms/authority-capacity-raise.md` "Every use of `State::limits_`" |
| A6 | pending: CI at Detent:e3f48e0b (pushed; suite, ASan/UBSan, TSan) |

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
   flag per field in `limit_fields` (state.cc). Lead: confirm.
2. One table (`limit_fields`) drives write, read and admission; messages name the configuration
   key (`application_limits.<field>`).
3. Sync paths (`State::apply`, `apply_batch`, `administer`, `BatchApplication::advance`, the
   four-argument `AdministrativeApplication`) apply under the state's own limits; only the node's
   replay passes carried limits. No node.hh change (outside the file set).
4. Not fixed: the in-force layout policy under raised state_bytes (documented). `Node::checked`
   still checks the configured layout.
5. Mutants are commits on local branches `mutant/restart-capacity-a1` and `-a2`, one assertion
   each, run with `--fails-without`.

Cancelled, superseded (not evidence): 261121-134013-9953d2, 261121-134216-f7b3a0, 261121-134612-884a01.
