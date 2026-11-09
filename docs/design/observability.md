> Status: DRAFT (2026-11-09) Ready for design review. Quest `observability-design` (campaign detent, D37). The application-facing API in D6 is a proposal: the `detent_*` C ABI is the owner's (PLAN.md, Reserved decisions).

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, counted inside the reader core at a measured cost of NUMBERS per warm lookup.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:194edd0` (main). The reader core is not on main yet: core citations are at reader S6, `Detent:b1d281c` (`quest/detent/reader-s6`).
- Depends on: reader.md D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork); the `detent_*` C ABI (D1 there). Benchmark: `docs/design/observability_bench.cc`, run by `observability-bench.sh` and summarized by `observability-summary.py`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering from its caller's default. The reader core counts nothing per lookup; the Client it replaces counts three process-wide outcomes with a shared `fetch_add` (`src/detent/client.cc:28-36`), which reader.md removed for cost (`docs/design/reader.md:39`).

The design counts in the core, after the answer, into a per-handle **usage table**: one slot per knob name, claimed on first sight, holding eight counters (the arm that answered, or the fallback taken), plus per-namespace view records written only on the refresh path. Counts are cumulative and never reset, so any number of consumers take deltas. Three consumers read the one table: the application through `detent_usage` (a pull), an on-resolve hook for applications that want each event, and the managed agent, which reads a copy of the table that the reader keeps in a shared page and serves a host-wide usage document. Values never enter the hot path: an arm and the generation's revision determine the value, so the exporter reads it from the published definition.

Measured on the S6 core (Intel i9-9980HK, Apple clang, `--threads 1,8`): RESULT SENTENCE.

## What a lookup can tell today

| Signal | Where it exists | Where it does not |
| --- | --- | --- |
| Knob read | The name passes `split` and `opened` (`core.cc:649-658`) | Nothing records it |
| Resolved value | `evaluate` returns a bool (`src/detent_local/evaluation.hh:14-39`); `detent_get` returns `default_int` (`core.cc:693`) | Not kept |
| Arm | Implicit in `evaluate`'s branches: switch override (forced bool or group rollout), then the global rollout, then the default; no hash answers the default (`evaluation.hh:30-38`) | Never returned; integers have no arms (spec `justknobs-implementation-spec.md:541`) |
| Fallback taken | Every status but `DETENT_OK` writes the caller's fallback (`core.cc:673,699`) | Not counted |
| View freshness | Each view entry keeps the sequence it was built from (`core.cc:116-120`); the handle counts publishes, refusals and attaches (`core.cc:356,478,485`) | Readable only through test hooks; no attach time; stale answers (`core.cc:532-535`) uncounted |
| Agent | `detent-agent --status` prints the pid, durable revision and knob count (`src/detent_agent/main.cc:125-147`); the monitor logs coordinator status every 20 ms (`src/detent_agent/managed_host.cc:28-100`) | No endpoint, no per-process view |
| Relay, authority | `/v1/health` JSON rebuilt every 100 ms (`src/detent_relay/host.cc:59-127,321-336`; `src/detent_relay/service.cc:405-413`); the authority's adds `kronuz::metrics` duration histograms (`src/detent_authority/service.cc:56-67`) | Nothing about readers |

## Decisions

### D1. What is observed, and its bounds

| Signal | Kept as | Per process | Per host (agent) | Per fleet |
| --- | --- | --- | --- | --- |
| Reads per knob, by arm | 8 cumulative counters per name slot (D3) | At most `K` slots per handle (default 4,096) plus one overflow slot | One row per distinct knob read on the host | One row per knob in the store (at most the 5M qualified) |
| Fallback taken | Counter cells `missing`, `wrong_type` per knob; every status per handle | 11 statuses × 2 kinds per handle | Summed | Summed |
| Generation each process reads | Per attached namespace: revision, sequence seen, attach time (D4) | One record per attached namespace | One per process × namespace | Revision lag per host |
| Age and refresh lag | Derived by the reader of the record: now − attach time; agent: its selection time − process attach time | | | |
| Refused publishes, stale answers, failed attaches | Per handle | 3 counters | Summed | |

Nothing is keyed by `hash_val`, `switch_val`, the caller's fallback value or the override group that matched (A5, D8).

### D2. Where it is counted, and what it costs

The count runs inside `lookup`'s answer, once the status and arm are known (`core.cc:660-672` and its twins in `detent_get` and `detent_read`), and before the pin is released, so a counter write and the pin share the lookup's time window but not its cache line. A name that fails validation counts only in the handle's status counters, so a caller spraying invalid names cannot fill the table.

COST TABLE AND DECISION.

### D3. The usage table

```
handle ── usage table: K + 1 slots
            slot i: name hash (8 B) | name (≤ 200 B, written once) | ROWS × 8 counters
            cells: default, default_no_hash, rollout_on, rollout_off,
                   override_forced, override_rollout, missing, wrong_type
```

1. A slot is claimed by compare-and-swap on the name's 64-bit hash, linear probing at most 32 slots; the claim writes the name before publishing the hash (release). A full table counts into the overflow slot and its `overflow` counter: bounded memory, no allocation, no lock.
2. A 64-bit hash collision between two names read by one handle merges their rows; with 4,096 names the chance is about 4.5 × 10⁻¹³. The snapshot reports the stored name, so a merge is visible as one name's counts being too large, never as a wrong name.
3. Counters are relaxed atomic adds and only grow. Readers take deltas between snapshots; nothing resets them, so two consumers never race over a reset.
4. `K` is fixed at `detent_open` (an option, Q1), so the table is allocated once and a warm lookup allocates nothing.

### D4. The arm, and values derived from it

`evaluate` returns `{bool value, arm}`; the arm is the branch it already takes (`evaluation.hh:30-38`), so the change adds a register write. Integers always answer `default`. The value a reader saw is a function of the knob, the arm and the generation revision: `default` and `default_no_hash` are the definition's `default_bool` or `default_int`, `override_forced` is the forced value, `rollout_on` is true and `rollout_off` false. The exporter reads it from the published definition, so no value, and no bucket, is recorded per lookup. `default_no_hash` is a separate arm because a caller that passes no hash on a ramped knob silently gets the default for every user.

### D5. Freshness, off the hot path

The holder of `refreshing` writes, per attached namespace, the revision, the sequence seen and the attach time (`CLOCK_MONOTONIC`, beside the refresh path's existing clock reads, `core.cc:359,375`), into the view it publishes. A snapshot also loads the namespace's current sequence from the mapped `change` file without refreshing (`detent_revision` refreshes, `core.cc:733-748`), so `current - seen` is the number of publishes a process has not yet looked up. On the same host `CLOCK_MONOTONIC` is one clock, so the agent compares a process's attach time with the time it renamed `selected`: that difference is the refresh lag, and it needs no change to the `change` file.

### D6. The application's view: snapshot and hook (proposed; Q1)

```c
typedef struct { size_t size; detent_str name; uint64_t cells[8]; } detent_knob_usage;
typedef struct { size_t size; detent_str name_space; uint64_t revision, seen, current;
                 uint64_t attached_ns; } detent_view_usage;
typedef struct { size_t size; detent_str name; detent_status status; uint32_t arm;
                 detent_value value; uint64_t revision; } detent_resolution;
typedef void (*detent_hook)(void *context, const detent_resolution *);

detent_status detent_usage(detent_reader *, void (*knob)(void *, const detent_knob_usage *),
                           void (*view)(void *, const detent_view_usage *), void *context);
/* detent_open_options grows (by its size field): usage_slots, hook, hook_context */
```

1. `detent_usage` walks the table and the installed view with relaxed loads; it takes no lock and runs beside lookups.
2. The hook is fixed at open, so a lookup reads it from the handle with no atomic. It runs on the caller's thread after the pin is released: a slow hook delays its caller, never a publish (reader.md Limit 3). It may look up knobs; it must not close the handle.
3. The built-in table counts every lookup whether a hook is set or not; the hook adds to it and cannot suppress it. Sampling is the hook's business.
4. C++, Python and Rust wrap both as they wrap lookups (client-local-source.md D1): `Reader::usage()` returning plain structs, a hook as a callable. A Python hook takes the GIL the lookup already holds.

### D7. Leaving the process and the host

```
app process: usage table ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file, D3 layout) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the table's counters live in it. The directory is the agent's (owner `publisher_uid`, mode 1733: others create, only the agent lists, nobody deletes another's file). The page costs what an in-memory table costs on the warm path (D2's mapped row); its creation is one file per handle, at open.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own counts, which it could do anyway.
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** A child must not count into its parent's page: the child handler replaces the page's range with private anonymous memory (one `mmap`, no file I/O, matching reader.md D10's handler rules), and the child's next refresh creates its own page and copies its names.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (hand-built, `host.cc:59-127`): per knob the counters by arm and the derived value; per process the namespaces, revisions, lag and age. Label-free totals (reads by kind and status, refused publishes, stale answers, processes) are spec §12's metrics (`justknobs-implementation-spec.md:690-738`).
6. **Fleet.** Per-knob reads and the count of hosts reading each knob are what answers "which knobs are dead". Aggregated by knob, the authority-side state is bounded by the store's knob count, not by hosts. It needs an upstream message from agent to relay to authority, a wire change, so it is Q4.

### D8. Privacy and limits

| Data | Exported as | Why |
| --- | --- | --- |
| `hash_val`, `switch_val` | Never stored | User or tenant identifiers (spec :580, :708) |
| Override group that matched | Never stored; only `override_*` arms | The group key equals a caller's switch value |
| Caller's fallback value | Never stored; only that it was taken | Caller-supplied, unbounded |
| Knob name | In the usage document and the snapshot, never a metric label | Spec §12 forbids name labels (:708) and allows a protected diagnostics endpoint (:712) |
| Resolved value | Derived from the published definition, as the value (Q5) | Already on every agent's disk |

Unbounded sets are capped where they enter: `K` names per handle, then the overflow slot; names at most 200 bytes (`core.cc:265-278`); the host document at most 65,536 knobs, ordered by reads, with a `truncated` count; fleet rows at most the store's knobs.

### D9. Kronuz/metrics

The agent keeps host aggregates and its scan and attach durations in `kronuz::metrics::Counter` and `DurationHistogram` (single owner, `metrics.hh` at the pin `metrics:9db73de`), as the authority does. The read path uses neither `AtomicCounter` nor `ContendedCounter`: D2 measures both. No new dependency, so `docs/SHARED_LIBRARY_DECISIONS.md` gains nothing. An exporter for `kronuz::metrics` snapshots is not proposed: the JSON writers stay where they are (`service.cc:56-67`, `host.cc:59-127`).

## Checks

| Criterion | Check |
| --- | --- |
| A1 What is observed, bounds | D1 table; reviewer reads |
| A2 Hot-path cost | `observability-bench.sh run confirm` (exclusive), job JOBID; D2 table |
| A3 Export path | D7; reviewer reads |
| A4 Hook | D6; `hook_off` and `hook_on` rows of D2 |
| A5 Privacy and caps | D8 |
| A6 Build plan | "Build plan" below |
| A7 Jobs | Build JOBID, smoke JOBID, screen JOBID, confirm JOBID (cited in D2) |

## Limits

1. **Silent:** a 64-bit name-hash collision merges two knobs' counts (D3 item 2).
2. **Silent:** past `K` names a handle counts into one overflow slot; a process with more live knobs than `K` loses per-knob detail for the rest (it sees `overflow` grow).
3. **Silent:** a process that never looks up never refreshes, so its view record is old but its `current − seen` says so only when someone reads it.
4. The per-knob counters cost memory per handle (D2); with the hook alone and `usage_slots = 0` the table is off.
5. Linux writeback of a dirty shared file page write-protects it again, so the first write after each writeback takes a minor fault on the lookup's thread; the page belongs on tmpfs (`/run`). D2's soak measures macOS only.
6. The agent's view lags by its scan interval; the application's snapshot does not.
7. A process can forge its page; host and fleet counts are as honest as the processes on the host.

## Alternatives considered

**Count in the application only (no agent path).** Every service would have to wire `detent_usage` into its own metrics stack before an operator sees anything; the owner's direction is counting in the client, published through a metrics engine. Kept as one of D7's consumers, not the only one.

**A shared `fetch_add` per lookup** (today's Client). D2 measures it under 8 threads.

**Per-thread counters.** No contention, but a thread registry per handle (threads come and go in Python and Rust pools), and `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5).

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are eight cells, and the value is recoverable from the definition.

**Knob-name labels in Prometheus.** Spec §12 forbids it, and per-knob series across 250k agents do not fit a time-series store.

## Open questions for the owner

**Q1. The application API (reserved).** (a) D6: `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. (b) Snapshot only. (c) Hook only, and the core keeps no table. *Recommendation: (a).* The table serves the agent with no application code; the hook serves applications that want events.

**Q2. Prepared knob handles.** (a) Keep names per call; the core hashes the name per lookup (D2's cost). (b) Add `detent_knob` handles resolved once (name validated, slot found), passed to lookups. *Recommendation: (a)* unless D2's hash share is large; (b) changes every binding.

**Q3. Host export.** (a) `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file in a configured directory (the node exporter's textfile collector reads it). (b) An HTTP `/metrics` listener in the agent. (c) OpenTelemetry. *Recommendation: (a):* no listener, port or credential on every host.

**Q4. Fleet aggregation.** (a) Agents send per-knob summaries upstream on their relay sessions; relays sum; the authority serves per-knob readers. (b) None; fleet tools collect host documents. *Recommendation: (a), after namespace delivery S4-S10*, which changes the same sessions.

**Q5. Values in the usage document.** (a) The value. (b) A digest of it. *Recommendation: (a)*: every agent already holds the definition.

**Q6. The page by default.** (a) On whenever the agent's store has `usage/` (agent setting). (b) Only with an open option. *Recommendation: (a)*, so operators get host data without changing applications.

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| OB1 Usage table and arms in the core | `src/detent_local/evaluation.hh`, `src/detent_reader/core.cc`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/` | Counts exact under 16 threads (TSan); T1's rows within D2's measured cost; zero allocation and syscall tests (reader.md T1) unchanged; fork child counts apart | high (hot path) | reader S6 landed; Q1, Q2 |
| OB2 View records and `detent_usage` | `core.cc`, `reader.h`, tests | Lag and age correct across publishes (T3's helper) | medium | OB1; reader S5 (landed) |
| OB3 Hook | `core.cc`, `reader.h`, `reader.hh` | Hook after unpin (park-hook test: a stalled hook never refuses a publish); `hook_off` cost | medium | OB1, Q1 |
| OB4 Usage page and agent collector | `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz (truncation, oversize, bad names) never crashes the agent; dead-process retention; Linux tmpfs fault count 0 over 10 min (VM) | medium | OB1, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | reader S8, S9 (`reader-s7-s9`) |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred | high | Q4; namespace S4-S10 |

reader S7 (`detent get` on the core) needs nothing from this design; `detent get` opens a handle with `usage_slots = 0`.
