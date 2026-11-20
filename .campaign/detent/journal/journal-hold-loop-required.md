# journal-hold-loop-required: journal

State: ready for review
Quest: .campaign/detent/quests/journal-hold-loop-required.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done in journal (branch pushed): journal:32172e7 test only (`hold_loop`, run by
`wake_refused`), journal:1222e3b the change (Hold's loop required, `std::logic_error` "a Hold needs
a loop"), mutant `hold-any-loop`, CHANGELOG, turn-seal.md. Jobs queued, tagged to A1/A2.

## Next

1. Review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pass at head 1c62ec; fails-without at test-only journal:32172e7 d62978 and at mutant hold-any-loop 49f27c |
| A2 | suite 3ff92a, TSan 971376, consensus@adb918f aa3407 pass. Detent 12b22a fails at CMake configure (journal names completion::task, Detent pins lack it); same command at journal main bb114f1 (d1e01c) fails identically at configure: a pin gap pin-wave fixes, Detent is unchanged by this quest. CI run 44692769460 at journal:1222e3b: success |
| A3 | [artifact] journal `CHANGELOG.md` (2026-11-19 Changed) and `docs/mechanisms/turn-seal.md` (Asking for a turn, wake_refused row) at journal:1222e3b |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- CMakeLists.txt is outside the file set, so `hold_loop` runs inside the registered `wake_refused`
  case instead of a new ctest entry.
- The compile-fail check is a runtime `check(!std::is_constructible_v<Hold, TurnDriver&, Holder>)`,
  so a missing change fails the test rather than breaking the file's build.
- Detent pins consensus a197efba, which has no Hold; its job proves the journal headers compile
  there (detent-authority). The Hold caller in consensus is checked at consensus origin/main.
