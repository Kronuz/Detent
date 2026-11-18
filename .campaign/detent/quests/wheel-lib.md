# wheel-lib: Kronuz/wheel, the one timer wheel library

Risk: high (concurrency, a new public library). The lead writes this quest; contributors read it
and never edit it. Machine state: `campaign status wheel-lib`.

## Outcome

Kronuz/wheel ships `wheel.hh` (D70, D72): candidate (f) of `docs/design/wheel-evidence.md`, a
single-owner hierarchical wheel with intrusive 32-byte entries, upper levels for far deadlines, and
a 64-stripe intrusive inbox for arms from other threads; std only, no Kronuz dependency. It follows
the cpp-libraries conventions (README, ARCHITECTURE, AGENTS, CI via cpp-libraries' cmake workflow,
example, fuzz target). Its first code is task-wheel's "// Timers" block at task:5a1e516, moved out.

## Acceptance criteria

- A1 Re-arm gap closed (D70): on macOS, an `exclusive` A/B on the wheel-evidence harness (A5's
  >= 100 ms samples, R = 10) puts the library's single-loop score within 1.10x of (a) at n = 1,000
  and n = 1,000,000, and its cross-thread score at P = 1, 4, 14 within 1.10x of (f)'s rows; the A/A
  band is reported.
- A2 Correctness: matches a reference model under random arm, cancel and re-arm, including from
  inside a fire (itself included); deadlines beyond any level never fire early and cost O(1) per arm
  and at most one move per level; cross-thread arms of unarmed entries from 1, 4 and 14 threads all
  fire once; owner-only cancel and re-arm asserted in Debug; tests in the suite, with mutants a test
  kills (no cascade, no owner assertion).
- A3 No allocation per arm, re-arm, cancel or fire: a counting-allocator test.
- A4 TSan and ASan+UBSan pass (CI, or a job).
- A5 A libFuzzer target drives random operation sequences against the reference model.
- A6 [artifact] `docs/timer-wheel.md`: the shape, the two threading contracts (other threads arm
  only an unarmed entry; only the owner cancels or re-arms), far deadlines, the A1 numbers.
- A7 [manual] The Xapiand lead reviews the diff.

## Contracts

Provides `wheel`. task (task-wheel), logger (logger-on-wheel) and Xapiand consume it.

## Decisions

- D70 (f) is the one wheel, re-arm gap first; D72 its own library, stash and scheduler retire.

## May land early

Nothing: the first push is the library's first release.
