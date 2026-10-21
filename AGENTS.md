# AGENTS.md: working in Detent

## Shared library decisions

strata and Xapiand share Kronuz libraries with Detent (`consensus`, `journal`, `completion`,
`reactor`). Decisions they make that touch those libraries are recorded, with their reasons and
how to object, in `docs/SHARED_LIBRARY_DECISIONS.md`. Read it before changing or pinning any of
those libraries, and record agreement or objections there.

## Sharing this machine

Other agents may run on the same machine, from other sessions and projects, and benchmarks may
run on it too; a compile during a measurement corrupts the measurement. Where the `quiet`
command is installed (`command -v quiet`), use it to coordinate; where it is not, work as usual.

Before compiling, running test suites, or measuring anything, run `quiet check`:

- Exit 0: the machine is free.
- Exit 1: a strict hold or insufficient disk. Read and edit while waiting; do not compile, test or measure. `quiet list` shows current holds.
- Exit 2: light holds, high load or low disk. A light `quiet run` queues for its slot.

Wrap builds and test suites in a light hold, run them at nice 19, and use at most four parallel jobs:

```sh
QUIET_LEVEL=light quiet run detent-build 15 "Detent build" -- nice -n 19 cmake --build build -j4
```

Wrap benchmarks, profiles and long fuzz runs in a strict hold. The wrapper waits for other holds and for host load to settle, then runs alone:

```sh
quiet run detent-bench 90 "Detent: what it measures" -- <command>
```

Use realistic deadlines: a hold expires when its process exits or its deadline passes. Do not rely on a pre-build check alone; the light hold keeps an already-running build visible to a later benchmark.
