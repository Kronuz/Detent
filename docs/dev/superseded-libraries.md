# Superseded Kronuz libraries

Which unarchived Kronuz C++ libraries something else supersedes, what supersedes them, who still
uses them, and what each user must change before the repository is archived (D73). A library is
archivable when its Users column is empty (D72's first condition); the other D72 conditions
(a separate replacement, proven better on every current use) are tracked by their own quests.

Swept 2026-11-11 against the main checkouts under `~/code/Kronuz/*`: stash
(`Kronuz/stash:d249352`), scheduler (`Kronuz/scheduler:73e582c`), cluster
(`Kronuz/cluster:141ebe0`), wheel (`Kronuz/wheel:4eff806`), Xapiand (`Kronuz/Xapiand:e8933b269`),
logger (`Kronuz/logger:b13724b`), prism (`Kronuz/prism:e0aea0e`), http-log
(`Kronuz/http-log:b652bb5`), Detent (`Kronuz/Detent:7db895a7`).

## Superseded

| Library | Successor | Evidence | Users | Migration still needed |
|---|---|---|---|---|
| stash | Kronuz/wheel | D70 (stash retired as a timer wheel; Linux VM: 3.6-30x slower on the loop, 12-25x slower cross-thread, `docs/design/wheel-evidence.md`); D72 (stash retires); `wheel/README.md`: "The successor to stash and the timer side of scheduler". The stash-niche quest (one more measurement on stash's own shape) can keep a narrow use before the archive. | See [stash users](#stash-users) | The users of scheduler, below, are stash's only users: they leave when logger and Xapiand leave scheduler. Then Detent drops its pin. |
| scheduler | Kronuz/wheel | D70 (scheduler moves onto the wheel); D72 (scheduler retires); `wheel/README.md` as above | See [scheduler users](#scheduler-users) | logger-on-wheel, Xapiand's moves, Detent's pins (D72's quest list), then the logger bump in prism and http-log. |
| cluster | Kronuz/consensus for the Raft half only (see the caveat) | `cluster/README.md:89-90` ("Durable consensus: ... Use consensus"); `cpp-libraries/AGENTS.md`, cluster entry ("The durable storage and consensus that grew here moved to completion, journal, consensus, and metrics"; "otherwise use `consensus`"); `cluster/AGENTS.md:69-71`. No PLAN decision. | None | Nothing: no repository pulls or includes it. |

cluster is a row at the owner's request (D73 says "likely"), and it is a partial supersession.
Its `cluster::Raft` is volatile (terms and votes live in memory) and `consensus` replaces it
wherever durability matters, which is every current consumer: Detent pins `consensus`, and its
`docs/status/post-w2.md` records the cluster pin as the one it did not move. Its `cluster::Bus`
(UDP-multicast typed frames scoped by a cluster token) and `LivenessTracker` have no successor
in any library; only Xapiand's discovery frames are wire-compatible with them, and Xapiand does
not pull the repository. The owner decides whether archiving cluster also retires the multicast
bus (see Proposed).

### stash users

Every row is a build dependency; stash's only first-party includer is scheduler.

| User | Where | Kind |
|---|---|---|
| scheduler | `CMakeLists.txt:14` (FetchContent `Kronuz/stash`, pin `b7d887678a21`), `scheduler.h:38` (`#include "stash.h"`) | direct |
| Detent | `cmake/dependencies.json:117` (pin `b7d887678a21`) | pin only: `cmake/DependencyPins.cmake` declares it first so logger's scheduler fetches this revision; no Detent source includes stash |
| Xapiand | `CMakeLists.txt:942-944` (puts the stash source dir on the include path, "scheduler.h includes stash.h"), `CMakeLists.txt:969` (`STASH_TRACE_HEADER="xapiand_stash_trace.h"`), `src/xapiand_stash_trace.h` | transitive via scheduler; no `#include "stash.h"` under `src/` |

### scheduler users

| User | Where | Kind |
|---|---|---|
| logger | `CMakeLists.txt:15` (FetchContent, pin `d57e8fc051097d`), `:48` and `:118` (`scheduler::scheduler` linked), `logger.h:50` (`#include "scheduler.h"`), `logger.h:558` (`Logging` is a `ScheduledTask<Scheduler<...>>`), `test/frozen/logger.h:42` | direct |
| Xapiand | `CMakeLists.txt:550` (FetchContent, pin `d57e8fc051097d`), `:553` (`SCHEDULER_TRACE_HEADER`), `src/xapiand_scheduler_trace.h`; `src/manager.h:40` (`debouncer.h`, the manager Debouncer), `src/xapiand_fsyncher.h:42` (`make_unique_debouncer`, the WAL fsync Debouncer), `src/debounce_or_run.h` (used by `src/database/wal.cc:44`) | direct |
| Detent | `cmake/dependencies.json:106` (pin `d57e8fc051097d`) | pin only, as for stash; Detent reaches scheduler through `detent_logger` (`cmake/dependencies.json:39`, `CMakeLists.txt:101`) |
| prism | `CMakeLists.txt:29` (logger pin `765528e977b5`; the comment at `:27` names scheduler) | transitive via logger |
| http-log | `CMakeLists.txt:23` (logger pin `f61d8df44ee5`) | transitive via logger |

Xapiand also pulls logger (`CMakeLists.txt:561`), which carries scheduler a second time through
the same pin. Detent's `logger::logger` links (`src/CMakeLists.txt:72`, `:86`, `:120`) reach
scheduler the same way.

Not users: documentation links in cpp-libraries (`README.md`, `AGENTS.md`, `docs/`), logger
(`README.md:80`, `ARCHITECTURE.md`), queue and varint (`AGENTS.md`), strata
(`docs/reviews/background-merges.md`), completion (`docs/MEASUREMENTS.md`), and Detent's
`benchmarks/library-adoption-*.json`, `docs/status/post-w2.md` and `.campaign/` records. They
name the repositories; none fetches or includes them. The cpp-libraries index (README table and
"What's here", AGENTS entries and "Pick by task" rows) lists all three and changes when the
lead archives them (D73).

### Migration order

1. wheel-lib, then task-wheel (D72).
2. logger-on-wheel: logger drops `scheduler` for `wheel` (`CMakeLists.txt`, `logger.h:50`, `:558`).
3. Xapiand's moves (its lead): the manager and WAL-fsync Debouncers, the two trace headers and
   the `CMakeLists.txt:542-553` and `:942-969` blocks.
4. prism and http-log bump their logger pin; Detent bumps `detent_logger` and removes the
   `scheduler` and `stash` entries (`cmake/dependencies.json:105-122`).
5. Re-run this sweep's greps; archive the repositories whose users are empty. A FetchContent pin
   on an archived repository keeps resolving (D73), so a missed pin breaks nothing at archive time.

## Proposed

Nothing here has a decision or a README statement; the owner decides.

1. **cluster's multicast bus.** If archiving cluster should retire `cluster::Bus` and
   `LivenessTracker` too, it needs a stated successor (none exists) or a statement that
   multicast discovery is out of scope; until then cluster is superseded for Raft only. Reason
   to retire: no repository pulls it, and Xapiand's discovery does not use it. Reason to keep:
   it is the only multicast bus and the one wire-compatible with Xapiand's discovery frames.
2. **fantasyname** (`Kronuz/fantasyname`, a fork of skeeto/fantasyname in JavaScript, TypeScript,
   C++, Elisp and Perl, last pushed 2026-06-29). Its C++ implementation lives on in the family
   as `namegen`, whose AGENTS entry calls `fantasyname::fantasyname` an alias "for the fork's
   CMakeLists". No repository fetches `Kronuz/fantasyname` (the only hit is a comment in `namegen/CMakeLists.txt:16`). It is not in the
   cpp-libraries index, so it is outside D73's scope as written; archiving it would also freeze
   the non-C++ implementations.

## Already archived

`server` (libev prototype; `cpp-libraries/README.md:214`, AGENTS entry `### server`). Nothing
pulls `Kronuz/server`: the two hits are comments in `http/CMakeLists.txt:40` and
`Xapiand/CMakeLists.txt:1194`, both saying it is no longer used.

## Checked and judged current

Method: every library's README, plus the cpp-libraries README and AGENTS.md, searched for
"supersed", "successor", "deprecated", "replaced by", "archived", "retired" and "use ... instead".
The only hits are the rows above, `server`, and the sentences that name Elysian (prospect,
strata: successor of Xapiand, not of a library). No decision other than D70-D73 supersedes a library.

- Foundations: char-classify, static-string, strings, split, repr, escape, stringified,
  strict-stox, utype, math, endian, iterators, lazy.
- Compile time, hashing, identifiers, containers: constexpr-phf, ctrie, fst, termdict, hashes,
  crc32c, enum-reflection, uinteger_t, base-x, cuuid, md5, sha256, random, bloom-filter,
  fusefilter, lru-cache, generation-cache (wholesale invalidation, not a duplicate of lru-cache),
  btree, flat-map, persistent-map.
- Concurrency, time, OS: task, threadpool, queue, atomic-shared-ptr, allocators, nanosleep, epoch,
  time-point, times, datetime, residency, io, fs, completion, system, wheel (work in progress; its
  first release lands with wheel-lib).
- Networking, serialization, storage: reactor, http, http-parser, http-log, radix-router,
  url-parser, compressors, varint, sortable, bitpack, docset, bkd, flume, storage, msgpack,
  journal, consensus, prism, pki.
- Domain: cartesian, htm, double-metaphone, soundex, string-similarity, namegen, unicode,
  snowball, analysis, boolean-parser, queryparser, automata (new 2026-11-10, no users yet),
  logger (current; it moves off scheduler, it is not superseded), term-color, traceback,
  located-exception, errno-names, metrics.
- Search and storage engines: prospect, strata, strata-oracle.

Not in the family and not judged: `Kronuz/stacktraces` (Python, last pushed 2020), forks and
mirrors of other projects (fmt, xapian, cpython and similar), the dotfiles and editor-theme
repositories, and `Xapiand`, `Detent` and `agent-campaign` themselves.

## Reproduce the users lists

From `~/code/Kronuz`, per main checkout (skip `*-wt` and build directories); `git grep` reads
tracked files only:

```sh
git grep -n -I -E 'Kronuz/(stash|scheduler|cluster)([^A-Za-z0-9_-]|$)|include[ ]*[<"](stash|scheduler|debouncer|bus|raft|liveness)\.(h|hh)[>"]' \
  -- '*CMakeLists.txt' '*.cmake' '*dependencies.json' '*.h' '*.hh' '*.cc' '*.cpp'
git grep -n -I -E 'Kronuz/logger|logger::logger' -- '*CMakeLists.txt' '*.cmake' 'cmake/dependencies.json'
git grep -n -I -E 'Kronuz/(fantasyname|automata)' -- '*CMakeLists.txt' '*.cmake' '*dependencies.json'
```

The first grep finds, outside the three libraries themselves, only logger, Xapiand and Detent;
the second adds prism and http-log as the transitive users. It finds no `Kronuz/cluster` user in
any repository.
