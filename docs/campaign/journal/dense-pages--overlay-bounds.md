# dense-pages--overlay-bounds: journal

State: working
Quest: docs/campaign/quests/dense-pages.md

## Doing

Bound inline subtree counting with a fixed 203-frame stack, 202-byte key depth,
page-header node visit budget and checked uint64 sums. Every View inline child
must follow its parent's end. Mutation walks reject accumulated prefix overflow.

## Next

1. Queue Debug, separate ASan/UBSan, and parent-code regression jobs at the code commit.
2. Report job ids to the game master; evidence remains pending until jobs finish.

## Blocked on

Nothing.

## Evidence

Focused regression checks unchanged prior bodies for every valid fixture row,
zero hashing on verified hits, and refusal of self/back/interior inline slots,
forward alias visit-budget exhaustion, excessive key depth, and key-count overflow.
CTest gives the regression a 15-second deadline. Existing dense delta assembly
runs alongside it to check canonical deltas and relocation values.

## Rulings I made

Counting traverses only the selected inline subtree and external summaries; it
neither rehashes verified hits nor verifies the full page for each node. Repeated
forward aliases consume the page's declared node budget on every visit.
