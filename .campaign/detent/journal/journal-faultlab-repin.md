# journal-faultlab-repin: journal

State: ready for review
Quest: .campaign/detent/quests/journal-faultlab-repin.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: journal:9c7e31c pins faultlab 5d4df9b; jobs and CI remain.

## Next

1. Land on green suite and CI (the lead).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261123-185919-e4cfcc (suite at journal:9c7e31c, crash and power-cut tests included) |
| A2 | `ci:ci` |

## Result

journal pins faultlab at 5d4df9b (stale at_mark fix), as consensus does. No code or test changed.

## Rulings I made

None.
