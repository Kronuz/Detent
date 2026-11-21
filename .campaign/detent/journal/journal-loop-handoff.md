# journal-loop-handoff: journal

State: waiting on jobs 261120-172817-4dd987 261120-172827-391258 261120-172830-714b37
Quest: .campaign/detent/quests/journal-loop-handoff.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs 261120-172817-4dd987 (Release suite, A3), 261120-172827-391258 (Debug host_test,
A1/A2), 261120-172830-714b37 (fails-without at mutant d2484f35, adopt_loop a no-op; ref
refs/mutants/journal-loop-handoff/no-handoff in the journal repo). Branch head journal:ca95301.

## Next

1. Read the three jobs' logs; the mutant job must fail on handoff_close's abort, not the build.
2. CI on the pushed branch; merge journal main once journal-idle-groups lands (host.h, host_test.cc).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending 261120-172827-391258, 261120-172830-714b37 |
| A2 | pending 261120-172827-391258, 261120-172830-714b37 |
| A3 | pending 261120-172817-4dd987, CI |

## Result

journal:ca95301 adds `Host::adopt_loop(loop)` (include/journal/host.h): the calling thread
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
