> Status: DRAFT (2026-11-09) Ready for design review. Quest `observability-design` (campaign detent, D37). The application-facing API in D6 is a proposal: the `detent_*` C ABI is the owner's (PLAN.md, Reserved decisions).

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, counted inside the reader core by sampling one lookup in 64, at a measured +2.6 to +7.4 ns per warm lookup under 8 threads, inside the benchmark's noise.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:0bd2aba` (main). The reader core is not on main yet: core citations are at reader S6, `Detent:b1d281c` (`quest/detent/reader-s6`).
- Depends on: reader.md R1 (the caller passes each lookup's default; no defaults file) and R2 (no overrides in the reader), D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork); the `detent_*` C ABI (D1 there). It targets the Reader API only: `detent::Client`, defaults files and `parse_defaults` are decided for deletion (PLAN.md D13, D14; quest `legacy-h`), and nothing here reads from them. Benchmark: `docs/design/observability_bench.cc`, run by `observability-bench.sh` and summarized by `observability-summary.py`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering with its caller's default. The reader core counts nothing per lookup, and reader.md removed a shared per-lookup `fetch_add` for cost (`docs/design/reader.md:39`).

The design counts in the core, after the answer, into a per-handle **usage table**: one slot per knob name, claimed on first sight, holding eight counters (the arm that answered, or the reason the caller's default was returned), plus per-namespace view records written only on the refresh path. One lookup in 64 per stripe is counted, so the name hash and the table probe run off the common path. Counts are cumulative and never reset, so any number of consumers take deltas. Three consumers read the one table: the application through `detent_usage` (a pull), an on-resolve hook for applications that want every event exactly, and the managed agent, which reads a copy of the table that the reader keeps in a shared page and serves a host-wide usage document. Values never enter the hot path: an arm and the generation's revision determine the value, so the exporter reads it from the published definition.

Measured on the S6 core (Intel i9-9980HK, Apple clang, 100,000 knobs, exclusive job `261109-120858-28cd50`): sampled per-knob counting costs +2.6 to +7.4 ns per lookup at 8 threads (0.3% to 0.8%), inside every A/A band (6.5 to 24.6 ns). Exact per-knob counting costs +24 to +69 ns (3.0% to 7.3%) across three row layouts, more than the shared `fetch_add` reader.md removed (+8.5 to +32.3 ns here). At one thread the A/A bands (30 to 180 ns) are wider than every arm's effect, so one thread resolves none of them.

## What a lookup can tell today

| Signal | Where it exists | Where it does not |
| --- | --- | --- |
| Knob read | The name passes `split` and `opened` (`core.cc:649-658`) | Nothing records it |
| Resolved value | `evaluate` returns a bool (`src/detent_local/evaluation.hh:14-39`); `detent_get` returns the published integer (`core.cc:693`) | Not kept |
| Arm | Implicit in `evaluate`'s branches: the definition's switch rule (forced bool or group rollout), then the global rollout, then the published value; no hash answers the published value (`evaluation.hh:30-38`) | Never returned; integers have no arms (spec `justknobs-implementation-spec.md:541`) |
| Caller's default taken | Every status but `DETENT_OK` writes the caller's fallback (`core.cc:673,699`; R1) | Not counted |
| View freshness | Each view entry keeps the sequence it was built from (`core.cc:116-120`); the handle counts publishes, refusals and attaches (`core.cc:356,478,485`) | Readable only through test hooks; no attach time; stale answers (`core.cc:532-535`) uncounted |
| Agent | `detent-agent --status` prints the pid, durable revision and knob count (`src/detent_agent/main.cc:125-147`); the monitor logs coordinator status every 20 ms (`src/detent_agent/managed_host.cc:28-100`) | No endpoint, no per-process view |
| Relay, authority | `/v1/health` JSON rebuilt every 100 ms (`src/detent_relay/host.cc:59-127,321-336`; `src/detent_relay/service.cc:405-413`); the authority's adds `kronuz::metrics` duration histograms (`src/detent_authority/service.cc:56-67`) | Nothing about readers |

## Decisions

### D1. What is observed, and its bounds

| Signal | Kept as | Per process | Per host (agent) | Per fleet |
| --- | --- | --- | --- | --- |
| Reads per knob, by arm | 8 sampled counters per name slot (D2, D3) | At most `K` slots per handle (default 4,096) plus one overflow slot | One row per distinct knob read on the host | One row per knob in the store (at most the 5M qualified) |
| Caller's default taken | Cells `missing`, `wrong_type` per knob; every status per handle | 11 statuses × 2 kinds per handle | Summed | Summed |
| Generation each process reads | Per attached namespace: revision, sequence seen, attach time (D5) | One record per attached namespace | One per process × namespace | Revision lag per host |
| Age and refresh lag | Derived by the reader of the record: now − attach time; agent: its selection time − process attach time | | | |
| Refused publishes, stale answers, failed attaches | Per handle | 3 counters | Summed | |

Nothing is keyed by `hash_val`, `switch_val`, the caller's fallback value or the switch rule that matched (A5, D8). No counter, hook field or value source reads a `Client`, a defaults file or an override layer: the value is what the generation published, and the fallback is the caller's argument to the lookup (R1, R2).

### D2. Where it is counted, and what it costs

The count runs inside `lookup`'s answer, once the status and arm are known (`core.cc:660-672` and its twins in `detent_get` and `detent_read`), and before the pin is released. A name that fails validation counts only in the handle's status counters, so a caller spraying invalid names cannot fill the table.

**The pick: sample one lookup in 64 per stripe.** A countdown word per stripe is decremented with a plain relaxed load and store; a lost decrement between two threads on one stripe only moves a sample. At zero the lookup hashes the name, finds its slot (D3) and adds 1 to the arm's cell; consumers multiply by the period. The countdown reloads from a per-stripe xorshift state with a value in [1, 127] (mean 64), so a caller that cycles through 64 × k names does not sample the same names forever, as a fixed reload of 63 would: the bench's scattered set, 1,024 names in order, samples only 16 of them at one thread with the fixed reload, by construction. The bench measures the fixed reload; the random reload runs once per sample and is OB1's to measure.

Cost per warm lookup, delta of the case p50 against the `none` arm (the S6 lookup with no counting), exclusive job `261109-120858-28cd50`, 7 repetitions, 262,144 calls per thread, the hot set one name and the scattered set 1,024 names. The A/A band is the largest gap between `none` and an identical `none2` over the repetitions:

| Arm (8 threads) | What it keeps | int hot | int scattered | bool hot | bool scattered |
| --- | --- | ---: | ---: | ---: | ---: |
| `none` p50 / A/A band | Nothing | 767.5 / 6.5 | 946.5 / 11.4 | 788.0 / 22.0 | 961.2 / 24.6 |
| `knob_sampled` (**the pick**) | Per knob, 1 in 64 | +6.0 | +4.1 | +2.6 | +7.4 |
| `knob_rows8` | Per knob, exact, 8 rows by stripe | +43.1 | +69.2 | +42.4 | +54.8 |
| `knob_rows8_mapped` | The same in a shared file page (D7) | +34.9 | +66.8 | +35.3 | +41.5 |
| `knob_rows64` | Per knob, exact, 64 rows | +24.5 | +60.5 | +24.0 | +50.8 |
| `knob_shared` | Per knob, exact, one row | +54.0 | +42.3 | +51.0 | +51.5 |
| `knob_ptr_rows8` | Per knob keyed by name pointer (no hash) | +26.3 | +32.6 | +16.8 | +23.3 |
| `knob_probe` | Hash and slot probe, no write | +15.6 | +24.5 | +14.4 | +33.9 |
| `stripe_rmw` | Per handle by arm, exact | +3.0 | +16.3 | +1.3 | +6.1 |
| `tls` | Per thread, exact | +1.9 | +7.0 | +0.6 | +3.3 |
| `shared` | One `fetch_add` | +23.6 | +14.2 | +32.3 | +8.5 |
| `metrics_atomic` | `kronuz::metrics::AtomicCounter` | +59.0 | +65.7 | +62.2 | +62.1 |
| `metrics_contended` | `kronuz::metrics::ContendedCounter` | +51.4 | +55.4 | +49.8 | +46.2 |
| `hook_off` | Hook pointer loaded, null | +5.4 | +0.9 | +1.4 | +5.5 |
| `hook_on` | A no-op hook called | +9.2 | +4.5 | +5.9 | +2.8 |

1. **The pick is inside the noise.** `knob_sampled` sits inside every 8-thread A/A band: at most 0.8% of the bench's lookup, and at most 2.2% of reader.md D3's 268.5 ns integer baseline if it were added whole. The screen job `261109-120857-3fb43d` (3 repetitions) agrees: +2.6 to +11.3 ns, inside its bands of 14.5 to 61.4 ns.
2. **Exact per-knob counting costs 24 to 69 ns at 8 threads**, 9% to 26% of D3's integer baseline. Most of it is finding the slot: hash and probe alone (`knob_probe`) cost 14 to 34 ns, and keying by pointer (`knob_ptr_rows8`) still costs 17 to 33 ns. Spreading the cells over more rows helps hot names (rows64 +24 against rows8 +43) and not scattered ones (+51 to +61), where each lookup touches a different line.
3. **Exact per-handle counts are nearly free** (`stripe_rmw` +1 to +16 ns, `tls` +1 to +7 ns), but they say nothing per knob. The core needs neither: the sampled table sums to the handle's estimate, and its status counters run only on failure paths.
4. **One thread resolves nothing.** At one thread the A/A bands are 30.3, 180.2, 134.6 and 47.6 ns, `none` and `none2` differ by up to 96 ns, and every arm lies between −167 and +68 ns of `none`; `knob_sampled` is −34.5, −19.0, −4.3 and −166.7. The 8-thread rows decide.
5. **The mapped page costs what memory costs.** `knob_rows8_mapped` is 2 to 13 ns below `knob_rows8` in all four cases, never above it. The 60-second single-thread soak on the scattered integers: in memory p50 1,198.9 ns, worst 64-call batch 22,095 ns, 73 minor faults; mapped p50 1,192.0 ns, worst batch 4,921 ns, 73 minor faults. On macOS the shared page adds no faults; Linux is Limit 5.
6. **Every exact arm counted every call**, and no timed loop allocated (the summary's checks; `counted / calls` 1.0000 for each exact arm in every case).
7. **The baseline is the S6 core, not D3's pinned view.** The bench's `none` at one thread is 889.2 ns for a hot integer and 945.3 ns for a hot bool, against D3's 268.5 and 475 ns measured on the pre-core pinned view at 5M knobs. That gap is the S6 core's, for its T1 gate; the counting deltas above are differences within one build and do not depend on it.

The counting code in the bench runs after the C ABI call, in `observability_bench.cc`, and recomputes the stripe; inside the core the stripe is already in a register, so a sampled lookup there costs at most what the bench measures. The bench checks its knob set with `parse_defaults` (`detent/json_schema.hh`) before publishing it through the test generation writer; `parse_defaults` goes with `Client` (D13, D14), and setup does not touch the timed loop, so the numbers stand. OB1 builds its test generations with the generation writer the reader tests use (`include/detent_testing/generation_writer.hh` at S6) and drops that check.

### D3. The usage table

```
handle ── usage table: K + 1 slots
            slot i: name hash (8 B) | name (≤ 200 B, written once) | 8 sample counters
            cells: published, no_hash, ramp_on, ramp_off,
                   switch_on, switch_off, missing, wrong_type
```

1. A slot is claimed by compare-and-swap on the name's 64-bit hash, linear probing at most 32 slots; the claim writes the name before publishing the hash (release). A full table counts into the overflow slot and its `overflow` counter: bounded memory, no allocation, no lock.
2. A 64-bit hash collision between two names read by one handle merges their rows; with 4,096 names the chance is about 4.5 × 10⁻¹³. The snapshot reports the stored name, so a merge is visible as one name's counts being too large, never as a wrong name.
3. Counters are relaxed atomic adds and only grow. Readers take deltas between snapshots; nothing resets them, so two consumers never race over a reset.
4. `K` is fixed at `detent_open` (an option, Q1), so the table is allocated once and a warm lookup allocates nothing.
5. One row per slot: with sampling, a slot takes one write per 64 lookups of its name, so the extra rows that exact counting needed (D2 item 2) buy nothing.
6. Estimates: a knob read `n` times shows about `n / 64` samples, with a relative standard error near `√(63 / n)` (8% at 10,000 reads, 0.8% at 1M). A knob read `n` times goes unsampled with probability about `(63/64)ⁿ`: 37% at 64 reads, 1% at 292 (Limit 2).

### D4. The arm, and values derived from it

`evaluate` returns `{bool value, arm}`; the arm is the branch it already takes (`evaluation.hh:30-38`), so the change adds a register write. Integers always answer `published`. The value a reader saw is a function of the knob, the arm and the generation revision:

| Arm | Value | Source |
| --- | --- | --- |
| `published` | The value the authority published for the knob | The definition (`default_bool()` and `default_int()` are its accessors, `evaluation.hh:16`, `core.cc:693`) |
| `no_hash` | The published value, because the caller passed no hash on a ramped knob | The definition |
| `ramp_on`, `ramp_off` | True, false: the global rollout's bucket | The bucket |
| `switch_on`, `switch_off` | True, false: the definition's switch rule, forced or by its group rollout's bucket | The definition, or the bucket |
| `missing`, `wrong_type` | The caller's fallback (R1) | Never recorded |

The switch rules are part of the definition the authority publishes (`evaluation.hh:11-13`); R2 removes the reader's local override layer, not them. The exporter reads each value from the published definition at the process's revision, so no value, and no bucket, is recorded per lookup. `no_hash` is its own arm because a caller that passes no hash on a ramped knob silently gets the published value for every user.

### D5. Freshness, off the hot path

The holder of `refreshing` writes, per attached namespace, the revision, the sequence seen and the attach time (`CLOCK_MONOTONIC`, beside the refresh path's existing clock reads, `core.cc:359,375`), into the view it publishes. A snapshot also loads the namespace's current sequence from the mapped `change` file without refreshing (`detent_revision` refreshes, `core.cc:733-748`), so `current - seen` is the number of publishes a process has not yet looked up. On the same host `CLOCK_MONOTONIC` is one clock, so the agent compares a process's attach time with the time it renamed `selected`: that difference is the refresh lag, and it needs no change to the `change` file.

### D6. The application's view: snapshot and hook (proposed; Q1)

```c
typedef struct { size_t size; detent_str name; uint64_t samples[8]; } detent_knob_usage;
typedef struct { size_t size; detent_str name_space; uint64_t revision, seen, current;
                 uint64_t attached_ns; } detent_view_usage;
typedef struct { size_t size; detent_str name; detent_status status; uint32_t arm;
                 detent_value value; uint64_t revision; } detent_resolution;
typedef void (*detent_hook)(void *context, const detent_resolution *);

detent_status detent_usage(detent_reader *, uint32_t *period,
                           void (*knob)(void *, const detent_knob_usage *),
                           void (*view)(void *, const detent_view_usage *), void *context);
/* detent_open_options grows (by its size field): usage_slots, hook, hook_context */
```

1. `detent_usage` reports the sampling period (64) and walks the table and the installed view with relaxed loads; it takes no lock and runs beside lookups.
2. The hook is fixed at open, so a lookup reads it from the handle with no atomic: unused, it measures +0.9 to +5.5 ns at 8 threads, inside the A/A bands (D2, `hook_off`). It runs on the caller's thread after the pin is released: a slow hook delays its caller, never a publish (reader.md Limit 3). It may look up knobs; it must not close the handle. Its `value` is what the lookup returned, the caller's fallback included; it stays in the caller's process.
3. The hook sees every lookup, so an application that needs exact counts keeps them there; the built-in table samples whether a hook is set or not, and the hook cannot suppress it.
4. C++, Python and Rust wrap both as they wrap lookups (client-local-source.md D1): `Reader::usage()` returning plain structs, a hook as a callable. A Python hook takes the GIL the lookup already holds.

### D7. Leaving the process and the host

```
app process: usage table ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file, D3 layout) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the table's counters live in it. The directory is the agent's (owner `publisher_uid`, mode 1733: others create, only the agent lists, nobody deletes another's file). The page costs what an in-memory table costs (D2 item 5), and sampling sends it one write per 64 lookups; its creation is one file per handle, at open.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own counts, which it could do anyway.
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** A child must not count into its parent's page: the child handler replaces the page's range with private anonymous memory (one `mmap`, no file I/O, matching reader.md D10's handler rules), and the child's next refresh creates its own page and copies its names.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (hand-built, `host.cc:59-127`): per knob the estimated reads by arm (samples × period) and the derived value; per process the namespaces, revisions, lag and age. Label-free totals (reads by kind and status, refused publishes, stale answers, processes) are spec §12's metrics (`justknobs-implementation-spec.md:690-738`).
6. **Fleet.** Per-knob reads and the count of hosts reading each knob are what answers "which knobs are dead". Sampling makes a knob read a few times per process invisible on one host but not across a fleet: 1,000 processes reading it 10 times each yield about 156 samples. Aggregated by knob, the authority-side state is bounded by the store's knob count, not by hosts. It needs an upstream message from agent to relay to authority, a wire change, so it is Q4.

### D8. Privacy and limits

| Data | Exported as | Why |
| --- | --- | --- |
| `hash_val`, `switch_val` | Never stored | User or tenant identifiers (spec :580, :708) |
| Switch rule that matched | Never stored; only `switch_on` and `switch_off` | The rule's key equals a caller's switch value |
| Caller's fallback value | Never stored; only that it was returned | Caller-supplied, unbounded (R1) |
| Knob name | In the usage document and the snapshot, never a metric label | Spec §12 forbids name labels (:708) and allows a protected diagnostics endpoint (:712) |
| Resolved value | Derived from the published definition, as the value (Q5) | Already on every agent's disk |

Unbounded sets are capped where they enter: `K` names per handle, then the overflow slot; names at most 200 bytes (`core.cc:265-278`); the host document at most 65,536 knobs, ordered by reads, with a `truncated` count; fleet rows at most the store's knobs.

### D9. Kronuz/metrics

The agent keeps host aggregates and its scan and attach durations in `kronuz::metrics::Counter` and `DurationHistogram` (single owner, `metrics.hh` at the pin `metrics:9db73de`), as the authority does. The read path uses neither `AtomicCounter` (+59 to +66 ns at 8 threads) nor `ContendedCounter` (+46 to +55 ns), both measured in D2. No new dependency, so `docs/SHARED_LIBRARY_DECISIONS.md` gains nothing. An exporter for `kronuz::metrics` snapshots is not proposed: the JSON writers stay where they are (`service.cc:56-67`, `host.cc:59-127`).

## Checks

Rerun with `campaign job rerun` on the four jobs at the commit under review (`observability-bench.sh build`, then `run smoke`, `run screen`, `run confirm`, on slot `obs-bench`, ref `s6=b1d281c`).

| Criterion | Check |
| --- | --- |
| A1 What is observed, bounds | D1 table; reviewer reads |
| A2 Hot-path cost | `observability-bench.sh run confirm` (exclusive), job `261109-120858-28cd50`; D2 table |
| A3 Export path | D7; reviewer reads |
| A4 Hook | D6; `hook_off` and `hook_on` rows of D2 |
| A5 Privacy and caps | D8 |
| A6 Build plan | "Build plan" below |
| A7 Jobs | Build `261109-120856-787b87`, smoke `261109-120857-5b7ebc`, screen `261109-120857-3fb43d`, confirm `261109-120858-28cd50` (cited in D2) |

## Limits

1. **Silent:** a 64-bit name-hash collision merges two knobs' counts (D3 item 2).
2. **Silent:** sampling misses a knob read rarely in one process: 37% at 64 reads, 1% at 292 (D3 item 6). An application that must know every read uses the hook.
3. **Silent:** past `K` names a handle counts into one overflow slot; a process with more live knobs than `K` loses per-knob detail for the rest (it sees `overflow` grow).
4. **Silent:** a process that never looks up never refreshes, so its view record is old but its `current − seen` says so only when someone reads it.
5. Linux writeback of a dirty shared file page write-protects it again, so the first write after each writeback takes a minor fault on the lookup's thread; the page belongs on tmpfs (`/run`). D2's soak measures macOS only.
6. The per-knob table costs memory per handle; with the hook alone and `usage_slots = 0` the table is off.
7. The agent's view lags by its scan interval; the application's snapshot does not.
8. A process can forge its page; host and fleet counts are as honest as the processes on the host.

## Alternatives considered

**Exact per-knob counting** (`knob_rows8` and its variants). Answers "was it read at all" exactly, at +24 to +69 ns per lookup under 8 threads (D2 item 2), up to a quarter of D3's integer baseline. Sampling gets the same table inside the noise; the hook keeps exactness for the applications that pay for it.

**A shared `fetch_add` per lookup.** +8.5 to +32.3 ns at 8 threads and no per-knob detail; reader.md removed it for cost.

**Per-thread counters** (`tls`, +0.6 to +7.0 ns). No contention, but a thread registry per handle (threads come and go in Python and Rust pools), `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5), and they carry no per-knob detail.

**Count in the application only (no agent path).** Every service would have to wire `detent_usage` into its own metrics stack before an operator sees anything; the owner's direction is counting in the client, published through a metrics engine. Kept as one of D7's consumers, not the only one.

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are eight cells, and the value is recoverable from the definition.

**Knob-name labels in Prometheus.** Spec §12 forbids it, and per-knob series across 250k agents do not fit a time-series store.

## Open questions for the owner

**Q1. The application API (reserved).** (a) D6: `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. (b) Snapshot only. (c) Hook only, and the core keeps no table. *Recommendation: (a).* The table serves the agent with no application code; the hook serves applications that want every event.

**Q2. Prepared knob handles.** (a) Keep names per call; a sampled lookup hashes the name. (b) Add `detent_knob` handles resolved once (name validated, slot found), passed to lookups. *Recommendation: (a).* Hash and probe cost 14 to 34 ns (D2 item 2), but sampling runs them once per 64 lookups; (b) changes every binding to save under 1 ns on average.

**Q3. Host export.** (a) `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file in a configured directory (the node exporter's textfile collector reads it). (b) An HTTP `/metrics` listener in the agent. (c) OpenTelemetry. *Recommendation: (a):* no listener, port or credential on every host.

**Q4. Fleet aggregation.** (a) Agents send per-knob summaries upstream on their relay sessions; relays sum; the authority serves per-knob readers. (b) None; fleet tools collect host documents. *Recommendation: (a), after namespace delivery S4-S10*, which changes the same sessions.

**Q5. Values in the usage document.** (a) The value. (b) A digest of it. *Recommendation: (a)*: every agent already holds the definition.

**Q6. The page by default.** (a) On whenever the agent's store has `usage/` (agent setting). (b) Only with an open option. *Recommendation: (a)*, so operators get host data without changing applications.

**Q7. Sampled or exact built-in counts.** (a) Sampled, one lookup in 64 (D2). (b) Exact per-knob rows. (c) Sampled by default, exact behind an open option. *Recommendation: (a).* (b) costs 24 to 69 ns per lookup for exactness the hook already offers; (c) is two shapes of one table.

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| OB1 Usage table, sampling and arms in the core | `src/detent_local/evaluation.hh`, `src/detent_reader/core.cc`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/` | Exact under 16 threads with the period forced to 1 (TSan); a 1,024-name cycle samples every name with the random reload; T1's rows within D2's `knob_sampled` cost, rerun as an exclusive job; zero allocation and syscall tests (reader.md T1) unchanged; fork child counts apart; test generations from the reader tests' generation writer | high (hot path) | reader S6 landed; Q1, Q7 |
| OB2 View records and `detent_usage` | `core.cc`, `reader.h`, tests | Lag and age correct across publishes (T3's helper) | medium | OB1; reader S5 (landed) |
| OB3 Hook | `core.cc`, `reader.h`, `reader.hh` | Hook after unpin (park-hook test: a stalled hook never refuses a publish); `hook_off` cost | medium | OB1, Q1 |
| OB4 Usage page and agent collector | `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz (truncation, oversize, bad names) never crashes the agent; dead-process retention; Linux tmpfs fault count 0 over 10 min (VM) | medium | OB1, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | reader S8, S9 (`reader-s7-s9`) |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred | high | Q4; namespace S4-S10 |

reader S7 (`detent get` on the core) needs nothing from this design; `detent get` opens a handle with `usage_slots = 0`.
