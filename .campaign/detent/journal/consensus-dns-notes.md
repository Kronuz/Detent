# consensus-dns-notes: journal

State: waiting on jobs 261119-185923-ea9df4 (A1 at head), 261119-185934-15dd4a (A1 fails-without: head's test over 0bd4c17's header), 261119-185944-c665b8 (suite, A3), 261119-185946-1b0e41 (TSan, A3)
Quest: .campaign/detent/quests/consensus-dns-notes.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code and docs done on quest/detent/consensus-dns-notes (pushed). Waiting on the jobs above.

## Next

0. msg (coordinator): suite 404d98 failed at peers_test.cc:855 `hangs({other, port})`, a socket probe (full accept queue) that runs before any resolver; compare the two x20 jobs' 'failures: N of 20' lines; same rate = pre-existing flake (report counts), higher at head = my change, fix with a test.

1. Read the jobs (check the fails-without log shows the `start_fails` CHECK failing, not a build error); paste `campaign brief`'s table below.
2. When the lead adds CHANGELOG.md to the file set, add the entry under Blocked on, then A2 is complete.

## Blocked on

CI at b3576ab failed linux-tsan at link: resolve_test.cc's global operator new/delete collided with the sanitizer runtime's. Fixed in the next commit: the hook and the `start_fails` check are compiled out under ASan/TSan (the SANITIZER_THREADS macro, which covers both); the case prints "skipped" there. The check still runs in the plain (and UBSan-only) builds: the suite's Release build and the A1 jobs, on macOS and Linux; the sanitizer builds skip it. Failed suite 404d98: the peers_test :855 one-off, superseded by 7032e8 (passed). Mutant jobs (A1 fails-without) now run the head's test over 0bd4c17's header (git checkout of the old header), since the fix and test are no longer separate commits at head.

Suite 404d98 failed once at peers_test.cc:855 `hangs({other, port})`: a one-off environmental failure of the black-hole setup probe, which runs before any resolver code. Peers x20: head 482e54 0 failures; base 36e4e4 1 failure, at :1163 (busy round-trip bound, peers-busy-roundtrip-bound's), not :855. Suite resubmitted: 7032e8.

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
| A3 | suite 261119-181142-7032e8 (404d98 hit the :855 one-off above), TSan 261119-165558-071155 |

## Result

resolve.h: `LookupThreads::start` makes the `running` entry inside the try; the catch erases it only if made. Test `start_fails` (resolve_test.cc) replaces global operator new to fail the thread's nth allocation and sweeps n over every allocation start() makes: after a throw, no queued lookup, no thread, and the failed lookup's resolver never runs. peer-dns.md gains the fork note (the exit-wait text was already there). Commits: consensus:0bd4c17 (test), consensus:c411e50 (fix), consensus:b3576ab (docs).

## Rulings I made

- The fork note is by the code's construction (inherited `running`/`exited` entries, `busy`, mutex), not measured.
- The mutant is the test commit before the fix (--fails-without at 0bd4c17), not an edit to dns_mutants.sh (outside the file set).
