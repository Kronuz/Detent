# journal-hold-loop-required: journal

State: waiting on jobs 261119-230956-1c62ec 261119-230958-bbc01a 261119-231013-d62978 261119-230959-49f27c 261119-231000-3ff92a 261119-231002-971376 261119-231003-aa3407 261119-231005-12b22a
Quest: .campaign/detent/quests/journal-hold-loop-required.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done in journal (branch pushed): journal:32172e7 test only (`hold_loop`, run by
`wake_refused`), journal:1222e3b the change (Hold's loop required, `std::logic_error` "a Hold needs
a loop"), mutant `hold-any-loop`, CHANGELOG, turn-seal.md. Jobs queued, tagged to A1/A2.

## Next

1. Read the jobs: A1 = 1c62ec passes at head; bbc01a/d62978 must fail at journal:32172e7
   (--fails-without); 49f27c must fail at the hold-any-loop mutant. A2 = 3ff92a (suite), 971376
   (TSan), aa3407 (consensus at origin/main adb918f, journal via FETCHCONTENT_SOURCE_DIR_JOURNAL),
   12b22a (Detent main, detent-authority).
2. Paste `campaign brief`'s table below; fill Result; ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | jobs above |
| A2 | jobs above |
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
