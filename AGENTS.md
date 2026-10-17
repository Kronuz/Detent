# AGENTS.md: working in Detent

## Sharing this machine

Other agents may run on the same machine, from other sessions and projects, and benchmarks may
run on it too; a compile during a measurement corrupts the measurement. Where the `quiet`
command is installed (`command -v quiet`), use it to coordinate; where it is not, work as usual.

Before compiling, running test suites, or measuring anything, run `quiet check`:

- Exit 0: the machine is free.
- Exit 2: a light hold. Build and test at `nice -n 19` with at most 4 parallel jobs.
- Exit 1: a strict hold. Do not compile, test, or measure; edit and read instead, and run
  `quiet wait --light` before resuming. `quiet list` shows who holds the machine and until
  when.

Wrap each benchmark, profile, or long fuzz run so others wait for it:

```sh
quiet run detent-bench 90 "Detent: what it measures" -- <command>
QUIET_LEVEL=light quiet run ...   # single-threaded or informational runs
```

A hold ends with its process or at its deadline, so a crashed run blocks no one for long.
