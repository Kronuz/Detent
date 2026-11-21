# journal-loop-handoff: journal

State: ready for review
Quest: .campaign/detent/quests/journal-loop-handoff.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing. Debug host_test 261120-181331-7a0bfd passed at journal:87d4382 (loop_handoff 7/0,
handoff_close 4/0, full host_test 1028 checks/0 failures). CI run 44854682137 at journal:87d4382
green on every job (macos-arm64, macos-x86_64, linux tsan, asan-ubsan, x86_64 clang and gcc,
arm64-gcc, freebsd). History: at ca95301 the third-thread probe failed because a new thread took
the exited adopter's std::thread::id (probe 261120-181237-2da023, 20/20 reuse); 87d4382 probes
from two live threads.

## Next

1. Review; land after journal-idle-groups, merging journal main first (host.h, host_test.cc).

msg-178 (D57): A3 cites ci:macos-arm64 at journal:87d4382; nothing else changed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-181331-7a0bfd; fails without: 261120-172830-714b37, 261120-181334-f52e5a |
| A2 | 261120-181331-7a0bfd; fails without: 261120-172830-714b37, 261120-181334-f52e5a |
| A3 | `ci:macos-arm64` (job 136696384237), run 44854682137 at journal:87d4382, all CI jobs green (D57/msg-178) |

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
