# consensus-dns-notes: journal

State: waiting on jobs 261119-165312-91a81d (A1 fails-without at 0bd4c17), 261119-165313-11543e (A1 at c411e50), 261119-165558-404d98 (suite, A3), 261119-165558-071155 (TSan, A3)
Quest: .campaign/detent/quests/consensus-dns-notes.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and docs done on quest/detent/consensus-dns-notes (pushed). Waiting on the jobs above.

## Next

1. Read the jobs (check the fails-without log shows the `start_fails` CHECK failing, not a build error); paste `campaign brief`'s table below.
2. When the lead adds CHANGELOG.md to the file set, add the entry under Blocked on, then A2 is complete.

## Blocked on

CHANGELOG.md (held by consensus-worker-release): lead, add it to my file set when worker-release lands. The entry, as a new section above `## 2026-11-18`:

```
## 2026-11-19

### Fixed

- A `LookupThreads::start` that throws while making a thread's list entry no longer leaves its lookup queued: the entry is made inside the try, so no resolver runs for a caller that saw the exception (`test/transport/resolve_test.cc`, case `start_fails`, fails start() at each of its allocations).

### Documented

- Exit waits for running lookups ([docs/mechanisms/peer-dns.md](docs/mechanisms/peer-dns.md)): the process's lookup threads close from an atexit handler, which waits for each running resolver to return, so a stuck `getaddrinfo` delays exit by up to the system resolver's timeout, and a resolver or delivery must not call `std::exit`. Fork is not supported: a forked child must exec or `_exit` before it uses a `Peers` or a `Lookup`.
```

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | jobs 261119-165312-91a81d (fails-without, test at 0bd4c17 with the emplace outside the try), 261119-165313-11543e (passes at c411e50) |
| A2 | [artifact] `docs/mechanisms/peer-dns.md` (exit wait: "A stuck getaddrinfo delays exit...", and the new fork paragraph, b3576ab); CHANGELOG.md entry text above, pending file-set |
| A3 | suite 261119-165558-404d98, TSan 261119-165558-071155 |

## Result

resolve.h: `LookupThreads::start` makes the `running` entry inside the try; the catch erases it only if made. Test `start_fails` (resolve_test.cc) replaces global operator new to fail the thread's nth allocation and sweeps n over every allocation start() makes: after a throw, no queued lookup, no thread, and the failed lookup's resolver never runs. peer-dns.md gains the fork note (the exit-wait text was already there). Commits: consensus:0bd4c17 (test), consensus:c411e50 (fix), consensus:b3576ab (docs).

## Rulings I made

- The fork note is by the code's construction (inherited `running`/`exited` entries, `busy`, mutex), not measured.
- The mutant is the test commit before the fix (--fails-without at 0bd4c17), not an edit to dns_mutants.sh (outside the file set).
