# logger-wheel-slim: Measure a slim deferred-line wheel record in logger

Risk: low (measurement). Repo `../logger`. Needs logger-wheel-ab. The lead writes this quest;
contributors read it and never edit it. Machine state: `campaign status logger-wheel-slim`.

## Outcome

logger-wheel-ab's arm (C) lost on deferred lines mostly by size: its record grew 184 to 264
bytes (+80: the entry, an owner pointer, the sort key, a `shared_ptr` self-reference), which
crossed macOS's 256-byte nano allocator and failed the heap gate (logger:c93ace2, "Verdict").
Arm (D) keeps (C)'s shape and order guarantees with the record at most 22 bytes over (A)'s, so
the verdict says whether logger can drop its deferred-line heap for the wheel (one timer
mechanism, D72) or keeps it on numbers.

## Acceptance criteria

- A1 [artifact] (D)'s design before any measured row: which of (C)'s 80 bytes it removes and how
  (for example the entry replacing (A)'s heap fields, container-of instead of an owner pointer,
  the record's existing ownership instead of a self-reference), with `sizeof(Logging)` for A, C
  and D: `docs/perf/wheel-ab.md`: "Arm D".
- A2 Correctness as logger-wheel-ab's A2 for (D) (every line once, none early, cleared lines never
  written, 0 per-thread and due-order inversions, TSan), then patterns 2 and 3 at P = 1, 4, 14,
  `exclusive`, (A) vs (D), by the committed Rule and the paired statistic of D77.
- A3 [artifact] The verdict per pattern and P, and one line for the lead: (D) no worse than (A) on
  every deferred row, or which row it loses and by how much: `docs/perf/wheel-ab.md`: "Verdict D".

## Contracts

Consumes `wheel:timer_wheel` at the landed wheel-lib commit. Provides nothing.

## Decisions

D72 (one mechanism if proven no worse), D77. A loss here goes to the owner with the numbers:
keep logger's heap (two mechanisms) or accept the cost.

## May land early

A1.
