# dense-pages--overlay-bounds: journal

State: complete — bounded overlay traversal verified and merged
Quest: docs/campaign/quests/dense-pages.md

## Doing

Bound inline subtree counting with a fixed 203-frame stack, 202-byte key depth,
page-header node visit budget and checked uint64 sums. Every View inline child
must follow its parent's end. Mutation walks reject accumulated prefix overflow.

## Next

1. Inspect the three queued results; fix any regression before review.
2. Parent-code kill job restores only the old overlay source in its own slot,
   then requires CTest failure within the bounded test deadline.

## Blocked on

Nothing.

## Evidence

Code: Detent:506c24646072bc790adb04e96859baa1ca1a181f. All jobs pin this SHA.

| Check | Job | State |
|---|---|---|
| Debug focused + delta assembly | 261106-114739-ba4d50 | Pending |
| Separate ASan/UBSan focused + delta assembly | 261106-114740-c11743 | Pending |
| Regression with unguarded parent overlay | 261106-114741-2f126a | Pending |

Focused regression checks unchanged prior bodies for every valid fixture row,
zero hashing on verified hits, and refusal of self/back/interior inline slots,
forward alias visit-budget exhaustion, excessive key depth, and key-count overflow.
CTest gives the regression a 15-second deadline. Existing dense delta assembly
runs alongside it to check canonical deltas and relocation values.

## Rulings I made

Counting traverses only the selected inline subtree and external summaries; it
neither rehashes verified hits nor verifies the full page for each node. Repeated
forward aliases consume the page's declared node budget on every visit.

## Results reconciled by the lead

Debug261106-114739-ba4d50 and separate ASan/UBSan261106-114740-c11743 both pass the focused test plus delta assembly. Parent-code261106-114741-2f126a returns exit0 only after the unguarded code fails the bounded regression. The fix is merged in Detent:a729b77. No jobs remain for this task.
