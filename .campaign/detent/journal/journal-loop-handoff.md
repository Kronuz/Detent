# journal-loop-handoff: journal

State: waiting on jobs 261120-181331-7a0bfd and CI run 44854682137 (ci:macos-arm64)
Quest: .campaign/detent/quests/journal-loop-handoff.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Debug job 261120-172827-391258 at journal:ca95301 failed "a third thread's view fails the debug
check": the test was wrong, not the check. The third thread took the exited adopter's
std::thread::id (probe 261120-181237-2da023: a thread started after a join reused the id 20/20 on
macOS). Fixed at journal:87d4382: two live threads probe at once, so one is not the owner;
host-ranges.md states the limit (the check can miss a stranger, never abort the owner).
Rerun: Debug 261120-181331-7a0bfd, fails-without 261120-181334-f52e5a at mutant journal:21af69b3
(refs/mutants/journal-loop-handoff/no-handoff), suite 261120-181337-a04542. Branch pushed.
Mutant 261120-181334-f52e5a passed: handoff_close aborted on the owner assert (Abort trap 6),
after the build. CI at ca95301 (run 44850405066) failed the same third-thread check on every
platform (tsan, asan-ubsan, macos, arm64-gcc), nothing else; CI at 87d4382 is run 44854682137.

## Next

1. Debug job 7a0bfd and ci:macos-arm64 (run 44854682137); asan-ubsan, linux-arm64-gcc, freebsd already green there.
2. Merge journal main once journal-idle-groups lands (host.h, host_test.cc).

msg-178 (D57): A3 cites ci:macos-arm64 at journal:87d4382; nothing else changed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261120-181334-f52e5a passed; pending 261120-181331-7a0bfd |
| A2 | 261120-181334-f52e5a passed; pending 261120-181331-7a0bfd |
| A3 | ci:macos-arm64, run 44854682137 at journal:87d4382 (pending; D57/msg-178 makes it the suite check, local suite a04542 cancelled) |

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
