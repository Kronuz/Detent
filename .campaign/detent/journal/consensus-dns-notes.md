# consensus-dns-notes: journal

State: ready to land (CI at a8e867b pending)
Quest: .campaign/detent/quests/consensus-dns-notes.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and docs done on quest/detent/consensus-dns-notes (pushed). Waiting on the jobs above.

## Next

1. Land once CI at the merged head is green.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | jobs 261119-165312-91a81d (fails-without, test at 0bd4c17 with the emplace outside the try), 261119-165313-11543e (passes at c411e50) |
| A2 | [artifact] `docs/mechanisms/peer-dns.md` (exit wait: "A stuck getaddrinfo delays exit...", and the new fork paragraph, b3576ab); CHANGELOG.md (2026-11-19: a Fixed line for the start fix, a Changed line for exit-wait and fork; a8e867b) |
| A3 | suite 261119-185944-c665b8 (404d98 hit the :855 one-off above), TSan 261119-185946-1b0e41 |

## Result

resolve.h: `LookupThreads::start` makes the `running` entry inside the try; the catch erases it only if made. Test `start_fails` (resolve_test.cc) replaces global operator new to fail the thread's nth allocation and sweeps n over every allocation start() makes: after a throw, no queued lookup, no thread, and the failed lookup's resolver never runs. peer-dns.md gains the fork note (the exit-wait text was already there); CHANGELOG.md entry (a8e867b). Commits: consensus:0bd4c17 (test), consensus:c411e50 (fix), consensus:b3576ab (docs).

## Rulings I made

- The fork note is by the code's construction (inherited `running`/`exited` entries, `busy`, mutex), not measured.
- The mutant is the test commit before the fix (--fails-without at 0bd4c17), not an edit to dns_mutants.sh (outside the file set).

- CI at b3576ab failed linux-tsan at link (the operator new hook collided with the sanitizer runtime): the hook and `start_fails` are compiled out under ASan/TSan; the check still runs in the plain build. Failed suite 404d98 was the peers_test :855 one-off, superseded by 7032e8.
