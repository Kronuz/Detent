# faultlab-design review: design (for Detent, D104)

Commit: cpp-libraries:a2c60c7193cb6a1637868dd674a082481084c216 (docs/design/faultlab.md, branch quest/xapiand/faultlab-design)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

A design with no code: no jobs. Every finding cites `file:line` at Detent:abb78f7 (home checkout),
completion:7731315, strata:491a1bd, journal:5a959a1 (quest/detent/journal-driver),
consensus quest/detent/consensus-fetch-macos, Xapiand:52c3e82 (harness/cluster).

## What I checked myself

Read, no probes needed: the two crash subjects and their recorders against F8/F10; journal-driver's
`PrefixIO` against F4; Detent's PeerMesh against Xapiand's proxy.py (F14); Detent's system and relay
histories, consensus-transport A6 and voter-read-freshness A2 against history.py (F12/F13); completion's
sync_calls_test against QD1's premise. The lead's "F3, an io is one directory" is the design's **F4**.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | gap | F10's recorder cannot serve Detent's two crash subjects, and step 7 cannot land as written. Both subjects take a concrete `PosixIO`, not `journal::IO`, and publish each file with a raw no-clobber `::linkat` on the native directory handle, plus a raw `::fstat` on it for the ownership check. `journal::IO` has no link primitive and faultfs no link op, so an IO-level shadow misses the commit step D6 names ("fetch, stage, link, the sync group") and diverges from the real directory; today's recorder only sees it because it rescans the real directory at each hook. | Detent mirror.hh:50, mirror.cc:45, :120, :249; agent_store.cc:84, :201, :251, :382; completion io.h:159-255 (no link); strata faultfs.hh:10 (create, rename, unlink only); test_relay_mirror_crash.cc:44-63 (scan) | Design: F8 adds `IO::link(source, destination)` (hard link, EEXIST on an existing name, never replaces; PosixIO `linkat`), and F7 adds faultfs's `link` as a fourth namespace op replayed in order, consuming no coins when absent so F3's seed table holds. F10 adds `recorder::verify()`: the shadow's current names and bytes equal `load(real)` after a run, so any IO bypass fails loudly. Detent (step 7, its own): subjects take `journal::IO`, `fstat` moves to `directory_trust()` (io.h:146-153 has foreign_owner and shared_writable). Tests: faultlab, a crash of link + unlink-temporary before `sync_directory` keeps only POSIX-reachable states; Detent, both crash tests end their recorded runs with `verify()`. |
| R2 | no | gap | QD3's recommendation drives placed stops "from outside by waiting for the event the process logs". A6's placed stop must land inside a drain at a code point; an outside signal sent on a log line races the drain, which completes in well under the driver's reaction time. peers_test places stalls in-process (a sleep posted on the loop, a side context frozen in the reader callback), and SIGSTOP of a single-process cluster would stop every node. | consensus peers_test.cc (fetch-macos) :1254-1262, :1290-1291, :1318-1320; consensus-transport.md A6 | Record the answer below: placed stops stay in the test at labelled code points; faultlab supplies only the `pause`/`resume` events (an `at=<label>` argument, measured `ms`) and the scenario seed. Also reconcile F15's table ("process faults (SIGSTOP, SIGKILL)") with QD3's "no signal code in faultlab": process signals are the harness's (test_relay_service.py:212-224 keeps them). |
| R3 | no | gap | F14 calls proxy.py a superset of PeerMesh, but `isolate` means two different faults. Detent's isolate closes every live connection and refuses new ones; Xapiand's holds bytes silently and delivers them on heal. Moving `mesh.isolate` unchanged turns Detent's partition and outage runs into a different fault. proxy.py's `Mesh.close` also discards forward-task failures that PeerMesh raises. | Detent network.py:100-104, :156-166; Xapiand proxy.py:87-101, :189-191 | F14 names both: `isolate` is silent hold, `isolate` + `reset` is Detent's today. Detent's step 7 picks one per run and records it. The moved proxy raises a forward failure on close, as PeerMesh does (Errors should never pass silently). |
| R4 | no | gap | F13's `load` sets aside only `scenario` and `fault` lines. Detent's system runs already write `events.jsonl` with `{"kind": ...}` records (metadata, actor_output, phase marks), which `load` rejects. | Detent runtime.py:118-132; history.py:122-126 | Either F12 reserves a key (for example, lines with `kind` are the harness's and set aside) or Detent writes F12 lines to their own file. Recommendation: the reserved key, which keeps one log per run. |
| R5 | no | nit | F6's `kill()` says current and durable state stay, but not whether staged ordering epochs stay. Detent's `after_kill` keeps them, so a cut after the restart still orders by them. | test_relay_mirror_crash.cc:263-281; faultlab.md:78-80 | State that ordering epochs survive `kill()`. |
| R6 | no | nit | F10's `recorder(real, filesystem& shadow)` roots the shadow at the device root. Under F4, a node with two directories needs `(filesystem&, directory)` like `io` does. | faultlab.md:146, :106 | Take the shadow as an `io`, or as a filesystem plus a directory. |
| R7 | no | nit | Step 6's journal count is at journal:8f3e410. journal-driver, which lands before step 7 (D104), adds host_test, driver_test and placement_test on `MemoryIO(disk, "host/")`. host_test's `RecordingIO` spies on names and fails by name, which faultfs's nth-op rules do not express, so it stays a test decorator. | journal:5a959a1 test/host_test.cc:52-77, :99; driver_test.cc:58 | Recount step 6 at the journal-driver landing. |

## Answers QD1-QD3, F3

**QD1. Yes, an IO-level recorder is enough, once R1 is fixed.** Recommendation: (a) recovery on
faultfs, with (b) `materialize` only for `mirror_matches` and anything that reads the directory
directly. What PosixIO turns the contract into is already pinned per primitive and per platform
(completion sync_calls_test.cc:131-151, :193-197, :254-299: each group sync, ordering point and
directory sync, with its barrier or full flush). Detent's recorders re-derive that mapping twice,
each with its own `#ifdef __APPLE__` rule (test_relay_mirror_crash.cc:74-119). The premise breaks
only where a program bypasses the contract, which is exactly the `linkat`/`fstat` in R1. Hence
`verify()`: it keeps (a) honest whenever new code adds a raw syscall. Recovery on faultfs also
drops the per-state `materialize` to a real directory, which is where the 900 s ctest budgets go
(tests/detent_relay/CMakeLists.txt:10, tests/detent_agent/CMakeLists.txt:24). That saving pays for QD2.

**QD2. Yes: (b), the four fixed picks at every cut plus N seeds.** The fixed four keep today's
deterministic coverage. The seeds add torn sectors and partial epochs, which Detent's model cannot
produce. Three conditions:

1. Seeds at the outer cuts only, not on the nested restart-and-cut recordings, deduplicated by
   state fingerprint as `g_outcomes` already does. Each seed comes from the F11 base seed and the
   cut index, so a failure prints its replay line.
2. N set by measurement after the move, inside today's 900 s timeout. The recorder now cuts after
   every primitive, not only at fsyncs and hooks, so cut counts grow before seeds multiply them.
3. One migration check before Detent deletes `after_power_cut`: for each recorded run, the four
   policies' states equal the old model's four, as a job. faultlab's own F5 property (every policy
   state is some seed's state) does not prove F5's claimed mapping onto Entries x Data.

**QD3. The process-level histories fit; A2 needs one named check; A6 does not use the checker.**

- **Relay partition and outage runs: (a).** Writers' receipts carry a unique (producer, sequence)
  per key (writers.py:118-127), which suits a write value. Durable agents' local reads during the
  outage are `prefix` reads with `node` set. Faults map to `isolate`/`heal` (see R3) and
  `kill`/`restart`.
- **"A relay never serves history older than it served": (b), a named check.** The `prefix`
  check is monotonic per (node, launch) and resets at a restart (history.py:240-246). A durable
  relay must not revert across restarts (the mirror test enforces `sequence >= served`,
  test_relay_mirror_crash.cc:206). Logging a constant `launch` would hide it. The check is
  explicit: `monotonic` across launches for nodes the log marks durable.
- **voter-read-freshness A2: (b), a named `seal` check.** A voter answers at a seal, not at the
  latest commit, so `linearizable` over writes would flag legitimate seal lag. The check: a
  history read returns a seal no older than any seal that a completed read or acknowledgement had
  observed before this read began. It is the read half of linearizability over a monotone
  system-written counter, about 20 lines in history.py, sharing `load` and the fault-interval report.
- **consensus-transport A6:** its verdicts are timing assertions inside the test (late timers,
  same lanes, drain), not register histories. It uses F11's scenario line and F12's
  `pause`/`resume` events for replay and reports, with no checker. Can placed stops be driven from
  outside? Whole-process pauses at a test phase can (test_relay_service.py:212-219, voters stopped
  with SIGSTOP). Stops at a code point cannot (R2). They stay in-process at labelled hooks, and
  faultlab carries no signal code.

**F3 (the design's F4). No conflict with D105. A prefixed view is a valid io over one directory;
the design should say so in one sentence.** journal-driver's `PrefixIO` (journal:5a959a1
host.h:214-245) is a decorator over any `journal::IO`. It forwards every call with a name prefix,
and its `sync_directory` and ordering points sync the one host directory. Over faultfs it composes
unchanged: `PrefixIO(io(fs, "host"), log_prefix(g, l), ...)`. F4 replaces something else:
MemoryIO's prefix, which models separate *directories* sharing one disk, where `sync_directory`
covers only that prefix (memory_io.h:121-122). Costs:

- **faultlab:** one sentence in F4, saying a name-prefixed view over one io is the caller's
  decorator, valid over faultfs, and not a directory. No code. faultfs's single-component names
  hold, since prefixed names carry no `/`.
- **journal and journal-driver:** none. `MemoryIO(disk, "host/")` in its tests becomes
  `io(fs, "host")` after `make_directory("host")`, with `PrefixIO` on top as today. D105's "IO gains
  no directory API" also holds: `make_directory` lives on faultfs's `filesystem`, test-only, not on
  `io.h`.
- **The hazard the sentence prevents:** at step 6 or 7, mapping each loop's view to its own faultfs
  directory would narrow `sync_directory` to the loop and diverge from D105's on-disk layout.

## Also checked (agreed shape)

- **One Python checker over one event log:** it fits Detent, given R4's reserved key and the two
  named checks above. C++ tests calling `python3 -I` as a test step works on Detent's macOS and
  Linux hosts.
- **In-process parts:** header-only and depending on completion's `io.h` alone, so portable to
  macOS dev hosts (faultfs today is header-only on io.h).
- **netlab and disklab:** truly optional for Detent. PeerMesh needs only isolate, heal and reset,
  all on the proxy tier, and F14 raises `Unsupported` rather than skip.

## Not run

No jobs. F5's mapping and the runtime of seeded matrices were not measured: QD2's conditions 2
and 3 are Detent's migration checks. F9 is the owner's and not reviewed. Xapiand-, strata- and
storage-side steps are outside this lens.
