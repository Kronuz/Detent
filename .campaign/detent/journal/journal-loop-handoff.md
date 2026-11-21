# journal-loop-handoff: journal

State: waiting on jobs 261120-230045-01aa40 261120-230047-4a4672 261120-230048-06c33d 261120-230050-c45323 261120-230051-2f2530 and CI at journal:4748481
Quest: .campaign/detent/quests/journal-loop-handoff.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review correctness CHANGES at 87d4382 (reviews/journal-loop-handoff-correctness.md).
- F1 (blocking): journal:4748481 adds host_test `handoff_scope` (2-loop Node: a thread adopts
  loop 1 and makes no call; the first thread's loop-1 view aborts, its loop-0 view passes). No
  code change: no test showed a defect. Jobs: passing Debug 261120-230045-01aa40 (loop_handoff,
  handoff_scope, handoff_close, full host_test); fails-without handoff_scope at mutants clear
  261120-230047-4a4672 (journal:51728741), every-loop 261120-230048-06c33d (journal:1757375c),
  loop-zero 261120-230050-c45323 (journal:4c29a944); handoff_close at no-handoff
  261120-230051-2f2530 (journal:ca6eaa71). Mutant refs: refs/mutants/journal-loop-handoff/*.
- F2 (optional): not adopted; handoff_scope covers both loop mutants, and CI TSan runs the tests'
  handoffs. Left to the lead.
- F3 (nit): fixed in 4748481: status checks expect `stored`; the never-failing check is gone.

## Next

1. Read the five jobs (mutants must fail on the test, not the build) and CI at 4748481; then
   ready for review. Land after journal-idle-groups, merging journal main first.

msg-178 (D57): A3 cites ci:macos-arm64 at journal:87d4382; nothing else changed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-181331-7a0bfd; fails without: 261120-172830-714b37, 261120-181334-f52e5a |
| A2 | 261120-181331-7a0bfd; fails without: 261120-172830-714b37, 261120-181334-f52e5a |
| A3 | 261120-224307-bc6241, `ci:macos-arm64` (job 136696384237), run 44854682137 at journal:87d4382, all CI jobs green (D57/msg-178) |

## Result

journal:ca95301 and 87d4382 add `Host::adopt_loop(loop)` (include/journal/host.h): the calling thread
becomes the loop's range-table owner and the debug check in owned_ranges follows it. Release
builds keep no owner; the call bounds-checks the loop (`std::out_of_range`). Tests:
test/host_test.cc `loop_handoff` (A1) and `handoff_close` (A2, strata's t6w), abort checks in
forked children. Docs: docs/mechanisms/host-ranges.md (rule 6, "The handoff"), CHANGELOG.md.
strata can drop the t6w workaround (strata:6421d4b3) by calling adopt_loop under its turn mutex.

## Rulings I made

- The handoff is unconditional and unchecked: the caller's own serialization orders it after
  the old owner's last call; the host cannot see that lock, so it checks nothing (msg-168 option a).
- `adopt_loop` bounds-checks with `ranges_.at` in every build, so release and debug refuse the
  same bad loop.
- The abort checks fork a child (stderr silenced) and expect SIGABRT; under NDEBUG they are
  skipped with a printed note, and the handoff paths still run.
