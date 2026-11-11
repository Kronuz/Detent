> Status: DRAFT (2026-11-10) Ready for design re-review: OD-1 to OD-9 (review `observability-design-design.md` at `Detent:6139e4c`) and OR1 to OR10 (the same file at `Detent:c30abc4`) folded in, A8 measured (D10), Q1-Q7 decided by the owner (PLAN.md D47), `ever_sampled` kept with its 1-thread miss (PLAN.md D48); its 64-thread row exceeds the ceiling in one of three runs, which the lead has put to the owner. Quest `observability-design` (campaign detent, D37). The owner accepted D6's application API shape (Q1, D47); its exact C declarations stay the owner's until OB1 freezes them (PLAN.md, Reserved decisions).

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, counted inside the reader core with an exact ever-read bit per definition and one lookup in 64 sampled, at +2.3 to +6.4 ns per warm lookup under 8 threads (on and off interleaved, three exclusive runs, D10).

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:0bd2aba` (main). The reader core is not on main yet: core citations are at reader S6, `Detent:b1d281c` (`quest/detent/reader-s6`).
- Depends on: reader.md R1 (the caller passes each lookup's default; no defaults file) and R2 (no overrides in the reader), D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork); the `detent_*` C ABI (D1 there). It targets the Reader API only: `detent::Client`, defaults files and `parse_defaults` are decided for deletion (PLAN.md D13, D14; quest `legacy-h`), and nothing here reads from them. Benchmark: `docs/design/observability_bench.cc`, run by `observability-bench.sh`, summarized by `observability-summary.py` and paired by `observability-paired.py`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering with its caller's default. The reader core counts nothing per lookup, and reader.md removed a shared per-lookup `fetch_add` for cost (`docs/design/reader.md:39`).

The design counts in the core, after the answer, into a per-handle **usage table**: one slot per knob name, claimed on first sight, holding nine counters (the arm that answered, or why the caller's default was returned), plus per-namespace view records written only on the refresh path. One lookup in 64 per stripe is counted, so the name hash and the table probe run off the common path. Beside the table, one bit per definition records exactly that the handle read it (D10). Counts are cumulative and never reset, so any number of consumers take deltas. Three consumers read the one table: the application through `detent_usage` (a pull), an on-resolve hook for applications that want every event, and the managed agent, which reads a copy of the table that the reader keeps in a shared page and serves a host-wide usage document. Values never enter the hot path: an arm and the generation's revision determine the value, so the exporter reads it from the published definition.

Measured on the S6 core (Intel i9-9980HK, Apple clang, 100,000 knobs) with OB1's method, on and off interleaved per block of 64 lookups in one binary (D10; exclusive jobs `261109-224833-fa1352`, `261110-021130-06137b`, `261110-021130-445051`, one bench binary): the pick, `ever_sampled`, costs +2.3 to +6.4 ns per warm lookup at 8 threads, +6.1 to +10.4 at 64 and +1.5 to +7.7 at one thread, where bool scattered names exceed the 5 ns ceiling in every run (+7.0 to +7.7, PLAN.md D48). Both rows near a ceiling sit inside the bench's own error, about ±4 ns of placement and up to 29 ns of code shape (D10, the bench's error), so OB1 re-measures them in the core. Exact per-knob rows cost +18.8 to +47.8 ns at 8 threads, and no exact count passes scattered names at 64 threads (the cheapest, `pos_tls`, +44.9 to +46.2). Sampling costs accuracy for counts, not for "was it read": a knob read once per process shows its bit, but on a host of 10 such processes no sampled read with probability 0.85 (Q7).

## What a lookup can tell today

| Signal | Where it exists | Where it does not |
| --- | --- | --- |
| Knob read | The name passes `split` and `opened` (`core.cc:649-658`) | Nothing records it |
| Resolved value | `evaluate` returns a bool (`src/detent_local/evaluation.hh:14-39`); `detent_get` returns the published integer (`core.cc:693`) | Not kept |
| Arm | Implicit in `evaluate`'s branches: the definition's switch rule (forced bool or group rollout), then the global rollout, then the published value; no hash answers the published value (`evaluation.hh:30-38`) | Never returned; integers have no arms (`docs/design/knob-model.md`, "Int") |
| Caller's default taken | Every status but `DETENT_OK` writes the caller's fallback (`core.cc:673,699`; R1) | Not counted |
| View freshness | Each view entry keeps the sequence it was built from (`core.cc:116-120`); the handle counts publishes, refusals and attaches (`core.cc:356,478,485`) | Readable only through test hooks; no attach time; stale answers (`core.cc:532-535`) uncounted |
| Agent | `detent-agent --status` prints the pid, durable revision and knob count (`src/detent_agent/main.cc:125-147`); the monitor logs coordinator status every 20 ms (`src/detent_agent/managed_host.cc:28-100`) | No endpoint, no per-process view |
| Relay, authority | `/v1/health` JSON rebuilt every 100 ms (`src/detent_relay/host.cc:59-127,321-336`; `src/detent_relay/service.cc:405-413`); the authority's adds `kronuz::metrics` duration histograms (`src/detent_authority/service.cc:56-67`) | Nothing about readers |

## Decisions

### D1. What is observed, and its bounds

| Signal | Kept as | Per process | Per host (agent) | Per fleet |
| --- | --- | --- | --- | --- |
| Reads per knob, by arm | 9 sampled counters per name slot (D2, D3) | At most `K` slots per handle (default 4,096) plus one overflow slot | One row per distinct knob and revision read on the host | One row per knob in the store (at most the 5M qualified) |
| Knob ever read | 1 bit per definition per attached generation, exact (D10) | One bitmap per attached namespace | The union over processes: one bitset per namespace and revision, outside the capped rows (D8) | The union over hosts (OB6) |
| Caller's default taken | Cells `missing`, `wrong_type`, `fallback_other` per knob; every status per handle, exact | 11 statuses × 2 kinds per handle | Summed | Summed |
| Generation each process reads | Per attached namespace: revision, sequence seen, attach time (D5) | One record per attached namespace | One per process × namespace | Revision lag per host |
| Age and refresh lag | Derived by the reader of the record: now − attach time; agent: its selection time − process attach time | | | |
| Refused publishes, stale answers, failed attaches | Per handle | 3 counters | Summed | |

Nothing is keyed by `hash_val`, `switch_val`, the caller's fallback value or the switch rule that matched (A5, D8). No counter, hook field or value source reads a `Client`, a defaults file or an override layer: the value is what the generation published, and the fallback is the caller's argument to the lookup (R1, R2).

### D2. Where it is counted, and what it costs

The count runs inside `lookup`'s answer, once the status and arm are known (`core.cc:660-672` and its twins in `detent_get` and `detent_read`), and before the pin is released. A lookup that fails before `lookup` with a valid name (`DETENT_NOT_OPENED`, `core.cc:655-656`) counts on the same sampled path. A name that fails validation counts only in the handle's status counters, so a caller spraying invalid names cannot fill the table.

**The pick: sample one lookup in 64 per stripe.**

1. **Where the countdown lives.** The handle holds its own array of 64 cache lines, one per stripe (the stripe is the lookup's existing `stripe_of`, `core.cc:142-144`), allocated at open. Each line holds the countdown word and a 64-bit xorshift state. It does not share D5's pin lines, which rotate with the ring; this is the layout the bench measures.
2. **Per lookup.** A plain relaxed load and store decrements the countdown; a lost decrement between two threads on one stripe only moves a sample.
3. **At zero.** The lookup hashes the name, finds its slot (D3) and adds 1 to the arm's cell; consumers multiply by the period. The countdown reloads from the stripe's xorshift with a value in [1, 127] (mean 64), so a caller that cycles through 64 × k names does not sample the same names forever, as a fixed reload of 63 would. The bench's scattered set, 1,024 names in order, samples only 16 of them at one thread with the fixed reload, by construction.
4. **Seeding.** At open, each stripe's state is seeded from one `CLOCK_MONOTONIC` read mixed with the handle address and the stripe index, and its countdown starts at a random value in [1, 127], so a handle does not sample every stripe's first lookup (startup knobs). A fork child keeps its parent's states until its next refresh, which creates its page (D7 item 4) and reseeds there from `getpid()` and the clock.

A one-run sequential screen: each arm runs its own pass, and the cost per warm lookup is the median over 7 repetitions of the arm's case p50 minus `none`'s case p50 in the same repetition (`observability-paired.py`), exclusive job `261109-120858-28cd50`, 262,144 calls per thread, the hot set one name and the scattered set 1,024 names. `none` is the S6 lookup with no counting; `none2` is the same arm run again. Three later runs of the method (`261109-131328-0ce3a4`, `261110-021130-d5103d`, `261110-021130-1bad17`) do not reproduce its small cells: their `none2` at 8 threads is −10.2 to +63.9 ns. The table separates only the large differences, +20 to +60 ns; D10's interleaved method resolves the pick.

| Arm (8 threads) | What it keeps | int hot | int scattered | bool hot | bool scattered |
| --- | --- | ---: | ---: | ---: | ---: |
| `none` p50 / spread across repetitions | Nothing | 767.5 / 94.7 | 946.5 / 121.8 | 788.0 / 97.5 | 961.2 / 117.1 |
| `none2` | Nothing (A/A) | +0.2 | −1.7 | −2.3 | −2.4 |
| `knob_sampled` (the countdown alone) | Per knob, 1 in 64 | +6.7 | −4.4 | +2.8 | +6.4 |
| `knob_rows8` | Per knob, exact, 8 rows by stripe | +41.9 | +55.6 | +49.4 | +60.3 |
| `knob_rows8_mapped` | The same in a shared file page (D7) | +35.0 | +64.2 | +35.2 | +39.4 |
| `knob_rows64` | Per knob, exact, 64 rows | +25.8 | +59.1 | +23.1 | +50.5 |
| `knob_shared` | Per knob, exact, one row | +55.0 | +42.3 | +54.1 | +53.4 |
| `knob_ptr_rows8` | Per knob keyed by name pointer (no hash) | +23.9 | +27.8 | +16.3 | +21.3 |
| `knob_probe` | Hash and slot probe, no write | +17.0 | +32.2 | +15.2 | +45.3 |
| `stripe_rmw` | Per handle by arm, exact | +4.7 | +10.4 | +2.5 | +13.6 |
| `tls` | Per thread, exact | +2.7 | +14.6 | +0.4 | +7.2 |
| `shared` | One `fetch_add` | +23.6 | +21.7 | +32.3 | +33.6 |
| `metrics_atomic` | `kronuz::metrics::AtomicCounter` | +59.0 | +55.5 | +64.6 | +62.1 |
| `metrics_contended` | `kronuz::metrics::ContendedCounter` | +55.2 | +52.0 | +49.9 | +51.6 |
| `hook_off` | Hook pointer loaded, null | +3.9 | −5.3 | +0.6 | +7.0 |
| `hook_on` | A no-op hook called | +8.7 | −2.9 | +5.3 | +2.4 |

1. **The sequential method does not resolve the pick.** In `28cd50` `knob_sampled` is −4.4 to +6.7 ns at 8 threads; in the three later runs it is −16.2 to +17.9 ns. D10's interleaved runs put `knob_sampled` at +0.4 to +5.0 ns and the pick, `ever_sampled`, at +2.3 to +6.4 ns at 8 threads, under 1% of the bench's 8-thread lookup (about 770 to 980 ns).
2. **Exact per-knob counting costs tens of ns at 8 threads**: +23 to +64 ns in `28cd50`, and in the three later runs every median of `knob_rows8`, `knob_rows64` and `knob_shared` is above +12 ns (`knob_rows64` +12.7 to +490.1), though single `knob_rows64` repetitions fall to −548.7 ns, so only the medians separate it. D10's interleaved `knob_rows64` is +18.8 to +47.8 ns. Most of it is finding the slot: in `28cd50` hash and probe alone (`knob_probe`) cost +15 to +45 ns (+5.0 to +49.4 in the reruns), and keying by pointer (`knob_ptr_rows8`) still costs +16 to +28 ns. More rows help hot names (rows64 +23 to +26 against rows8 +42 to +49) and not scattered ones (+51 to +60), where each lookup touches a different line.
3. **Exact per-handle counts are cheap** (`stripe_rmw` +2.5 to +13.6 ns, `tls` +0.4 to +14.6 ns, within this method's A/A spread), but they say nothing per knob. The core needs neither: the sampled table sums to the handle's estimate, and its exact status counters run only on failure paths.
4. **One thread resolves nothing in this method.** `none` moves 221 to 287 ns between repetitions at one thread, and the paired medians of `knob_sampled` are +15.7, −4.6, +14.0 and −37.5 ns. One thread per process is the common shape under Python's GIL, so OB1's gate (below) measures one thread with the interleaved method, which resolves it to about ±4 ns (D10).
5. **The mapped page costs what memory costs.** `knob_rows8_mapped` is within 21 ns of `knob_rows8` either way, less than `none`'s spread. The 60-second single-thread soak on the scattered integers: in memory p50 1,198.9 ns, worst 64-call batch 22,095 ns, 73 minor faults; mapped p50 1,192.0 ns, worst batch 4,921 ns, 73 minor faults. On macOS the shared page adds no faults; Linux is Limit 7.
6. **Every exact arm counted every call**, and no timed loop allocated (the summary's checks; `counted / calls` 1.0000 for each exact arm in every case).
7. **These deltas are measured at the S6 core's baseline, not D3's.** The bench's `none` at one thread is 889.2 ns for a hot integer and 945.3 ns for a hot bool, against D3's 268.5 and 475 ns measured on the pre-core pinned view at 5M knobs. At about 900 ns a lookup can overlap added work that a 268 ns lookup would expose, so OB1 re-establishes the cost in the core. The gap itself is the S6 core's, for its T1 gate.
8. **No case here shares a countdown word between threads**: every 8-thread case ran on 8 distinct stripes. D10's 64-thread cases do (64 threads on 54 stripes), and OB1's gate keeps a 64-thread case.

**OB1's cost gate.** One binary, two handles over one store, one with `usage_slots = 0` (no table, no countdown and no ever-read bits, Limit 8), alternated per block of 64 lookups on each thread, so on and off share the compiled path, the thread, the frequency state and the cache. The statistic is the median over blocks of the per-block difference, per repetition, then the median over at least 7 repetitions. Ceilings: at most **5 ns at 1 thread** and **10 ns at 8 threads**, and at most 10 ns at 64 threads (shared stripes), for hot and scattered integers and bools. It runs as an exclusive job; a ceiling exceeded fails it.

The counting code in the bench runs after the C ABI call, in `observability_bench.cc`, and recomputes the stripe; inside the core the stripe is already computed. The bench checks its knob set with `parse_defaults` (`detent/json_schema.hh`) and publishes it with the test writer's `publish_json`, which also parses with it (`src/detent_testing/generation_writer.cc:326` at S6); setup does not touch the timed loop, so the numbers stand. OB1 builds its test generations with the generation writer's `publish(Snapshot)` (`include/detent_testing/generation_writer.hh:23` at S6), the path the reader tests use, and needs no JSON.

### D3. The usage table

```
handle ── usage table: K + 1 slots
            slot i: name hash (8 B) | name (≤ 200 B, written once) | 9 sample counters
            cells: published, no_hash, ramp_on, ramp_off, switch_on, switch_off,
                   missing, wrong_type, fallback_other
```

1. A slot is claimed by compare-and-swap on the name's 64-bit hash, linear probing at most 32 slots; the claim writes the name before publishing the hash (release). A full table counts into the overflow slot and its `overflow` counter: bounded memory, no allocation, no lock.
2. A 64-bit hash collision between two names read by one handle merges their rows; with 4,096 names the chance is about 4.5 × 10⁻¹³. The snapshot reports the stored name, so a merge is visible as one name's counts being too large, never as a wrong name.
3. Counters are relaxed atomic adds and only grow. Readers take deltas between snapshots; nothing resets them, so two consumers never race over a reset.
4. `K` is fixed at `detent_open` (an option, Q1), so the table is allocated once and a warm lookup allocates nothing.
5. One row per slot: with sampling, a slot takes one write per 64 lookups of its name, so the extra rows that exact counting needed (D2 item 2) buy nothing, and nine cells need not fit one cache line.
6. `fallback_other` takes every fallback status but `missing` and `wrong_type` (`not_opened`, `unavailable`, `no_memory`, `source_failure`); the handle's exact status counters split it.
7. Estimates: a knob read `n` times shows about `n / 64` samples, with a relative standard error near `√(63 / n)` (8% at 10,000 reads, 0.8% at 1M). What that costs each question is Q7.

### D4. The arm, and values derived from it

`evaluate` returns `{bool value, arm}`; the arm is the branch it already takes (`evaluation.hh:30-38`), so the change adds a register write. Integers always answer `published`. The value a reader saw is a function of the knob, the arm and the generation revision:

| Arm | Value | Source |
| --- | --- | --- |
| `published` | The value the authority published for the knob | The definition (`default_bool()` and `default_int()` are its accessors, `evaluation.hh:16`, `core.cc:693`) |
| `no_hash` | The published value, because the caller passed no hash on a ramped knob or a group rollout | The definition |
| `ramp_on`, `ramp_off` | True, false: the global rollout's bucket | The bucket |
| `switch_on`, `switch_off` | True, false: the definition's switch rule, forced or by its group rollout's bucket | The definition, or the bucket |
| `missing`, `wrong_type`, `fallback_other` | The caller's fallback (R1) | Never recorded |

The switch rules are part of the definition the authority publishes (`evaluation.hh:11-13`); R2 removes the reader's local override layer, not them. The exporter reads each value from the published definition, so no value, and no bucket, is recorded per lookup. `no_hash` is its own arm because a caller that passes no hash on a ramped knob silently gets the published value for every user.

Counts are not keyed by revision: a revision field per lookup would put the view's revision on the sampled path and multiply the table's rows. The agent attributes a process's counts to the revision that process has installed when the agent reads its page, and keeps one row per knob and revision when processes on the host differ. A delta that straddles a publish which changed a knob's value is attributed wholly to the new revision; the error is bounded to the one scan interval that straddles the publish (Limit 5).

### D5. Freshness, off the hot path

The holder of `refreshing` writes, per attached namespace, the revision, the sequence seen and the attach time (`CLOCK_MONOTONIC`, beside the refresh path's existing clock reads, `core.cc:359,375`), into the view it publishes. A snapshot also loads the namespace's current sequence from the mapped `change` file without refreshing (`detent_revision` refreshes, `core.cc:733-748`), so `current - seen` is the number of publishes a process has not yet looked up. On the same host `CLOCK_MONOTONIC` is one clock, so the agent compares a process's attach time with the time it renamed `selected`: that difference is the refresh lag, and it needs no change to the `change` file.

### D6. The application's view: snapshot and hook (Q1)

```c
typedef struct { size_t size; detent_str name; uint64_t samples[9]; } detent_knob_usage;
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
2. The hook is fixed at open, so a lookup reads it from the handle with no atomic. Its unused cost is not resolved yet: `hook_off` at 8 threads is −5.3 to +7.0 ns in D2's sequential run and −16.9 to +22.2 ns in its three later runs, inside that method's A/A spread. OB3's interleaved gate is its first resolving measure. The hook runs on the caller's thread after the pin is released: a slow hook delays its caller, never a publish (reader.md Limit 3). It may look up knobs; it must not close the handle. Its `value` is what the lookup returned, the caller's fallback included; it stays in the caller's process.
3. The hook sees every lookup, so an application that wires it can keep exact counts; that exactness reaches only applications that wire it. The built-in table samples whether a hook is set or not, and the hook cannot suppress it.
4. C++, Python and Rust wrap both as they wrap lookups (client-local-source.md D1): `Reader::usage()` returning plain structs, a hook as a callable. A Python hook takes the GIL the lookup already holds.

### D7. Leaving the process and the host

```
app process: usage table ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file, D3 layout) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3, D9)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the table's counters live in it. The directory is the agent's: owner `publisher_uid`, group the agent's group, mode 3733 (setgid and sticky). Others create files but cannot list, nobody deletes another's file, and setgid gives each file the agent's group on Linux, where a file otherwise takes its creator's group and the agent could not read it (macOS always takes the directory's group). If the create fails (the name exists, or the directory refuses it), the handle keeps a private table and counts the failure in its status counters. The page costs what an in-memory table costs (D2 item 5), and sampling sends it one write per 64 lookups; its creation is one file per handle, at open.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own counts, which it could do anyway.
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** A child must not count into its parent's page: the child handler replaces the page's range with private anonymous memory (one `mmap`, no file I/O, matching reader.md D10's handler rules), and the child's next refresh creates its own page, copies its names and reseeds its stripes (D2 item 4). The child's ever-read bitmaps start clear in that memory, so a child's set holds only its own reads; the parent's stay in the parent's page.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (hand-built, `host.cc:59-127`): per knob and revision the estimated reads by arm (samples × period) and the derived value; per namespace and revision the ever-read set, a base64 bitset by definition position in that revision's generation, outside the capped rows (D8); per process the namespaces, revisions, lag and age. Label-free totals (reads by kind and status, refused publishes, stale answers, processes) are the `Metrics` counters (`include/detent/client.hh:32-48`), written as a Prometheus text file (D9).
6. **Fleet.** The ever-read union across hosts answers "which knobs are dead" exactly (D10); sampled reads per knob answer "how often", and only at fleet scale for rare knobs: 1,000 processes reading a knob 10 times each yield about 156 samples, while one host may show none. Aggregated by knob, the authority-side state is bounded by the store's knob count, not by hosts. It needs an upstream message from agent to relay to authority, a wire change, so it is Q4.

### D8. Privacy and limits

| Data | Exported as | Why |
| --- | --- | --- |
| `hash_val`, `switch_val` | Never stored | User or tenant identifiers (`include/detent/errors.hh:20`, `include/detent/client.hh:32-34`) |
| Switch rule that matched | Never stored; only `switch_on` and `switch_off` | The rule's key equals a caller's switch value |
| Caller's fallback value | Never stored; only that it was returned | Caller-supplied, unbounded (R1) |
| Knob name | In the usage document and the snapshot, never a metric label | `Metrics` takes no name label (`include/detent/client.hh:32-34`); a protected diagnostics endpoint may carry names |
| Resolved value | Derived from the published definition, as the value (Q5) | Already on every agent's disk |

Unbounded sets are capped where they enter: `K` names per handle, then the overflow slot; names at most 200 bytes (`core.cc:265-278`); the host document at most 65,536 knob count rows, ordered by reads, with a `truncated` count; fleet rows at most the store's knobs.

The ever-read set is not in those rows, because ordering by reads would truncate the rarely read knobs first, the ones the bit exists for. The document carries it as one bitset per namespace and revision held by the host's processes, by definition position in that generation: at most one bit per definition, 12.5 KB at 100,000 knobs and 625 KB at 5M (834 KB in base64) per revision, whatever the cap. A bitset is chosen over a list of positions because its bound is the smaller one, where a list of 32-bit positions reaches 20 MB per revision at 5M, and it has the page's shape, so the host and fleet unions are ORs.

### D9. Kronuz/metrics

The agent keeps host aggregates and its scan and attach durations in `kronuz::metrics::Counter` and `DurationHistogram` (single owner, `metrics.hh` at the pin `metrics:9db73de`), as the authority does. The read path uses neither `AtomicCounter` (+55 to +65 ns paired at 8 threads in D2's sequential run, +41.2 to +71.8 in its reruns) nor `ContendedCounter` (+50 to +55 ns, +37.5 to +60.2 in the reruns). The Prometheus text file needs a renderer for those types; it belongs in Kronuz/metrics, header-only beside the counters it renders, so the relay and authority can serve the same format later instead of each writing its own. It adds no dependency; it is a change to the metrics library under its own review (OB4's first step). The JSON writers stay where they are (`service.cc:56-67`, `host.cc:59-127`).

### D10. Exact counting (A8)

The owner asked for exact counts if they can be made cheap (Q7, D38). Five exact mechanisms beyond D2's rows were measured. Each indexes by the definition's position in the generation, which the core holds once `lookup` has found the definition (the bench carries it on each name), so none hashes a name or probes a table:

1. `pos_shared`: one row of 64-bit counters, a relaxed `fetch_add` per lookup.
2. `pos_stripe_rmw`: 64 rows of 32-bit counters, one per stripe, a relaxed `fetch_add` (a locked add on x86).
3. `pos_tls`: one row of 32-bit counters per thread, a relaxed load and store; the row is allocated on the thread's first lookup and registered for a scan to sum.
4. `dm_cache`: a per-thread cache line of 8 (position, count) pairs, direct-mapped by position; a lookup that evicts a pair adds its count to the shared row.
5. `ever_bit`: one bit per definition, test-before-set: a relaxed load, and a `fetch_or` only on a knob's first read, so after it the word stays shared in every core's cache.

Beside them: `ever_sampled` (`ever_bit` and `knob_sampled` together, the shape recommended below), `knob_sampled` (D2's pick), `knob_rows64` (D2's best exact rows) and `none2` (A/A).

The method is OB1's gate: the arm and `none` in one binary, alternated per block of 64 lookups on each thread over the same names, in both orders; a repetition's delta is the mean of its on-first and off-first block-pair medians, and the table gives the median over 7 repetitions (`observability-paired.py`). Exclusive job `261109-202834-bfb3cf` at `Detent:09f038e`, 262,144 calls per thread, 100,000 knobs, the hot set one name and the scattered set 1,024 names; 64 threads on 16 hardware threads share stripes. Each cell is the larger of the integer and the bool delta, hot / scattered, in ns:

| Arm | 1 thread | 8 threads | 64 threads |
| --- | ---: | ---: | ---: |
| `none2` (A/A) | −0.1 / −0.1 | −0.8 / −0.2 | −1.7 / +0.3 |
| `ever_bit` | +3.0 / +3.7 | +2.6 / +3.1 | +4.2 / +4.6 |
| `knob_sampled` | +3.0 / +4.5 | +2.9 / +3.2 | +5.2 / +6.9 |
| `ever_sampled` | +3.7 / **+7.4** | +3.9 / +6.4 | **+11.9 / +14.4** |
| `pos_tls` | +1.4 / +4.3 | +1.4 / +6.1 | +4.1 / **+46.2** |
| `pos_stripe_rmw` | +3.8 / **+8.4** | +3.3 / +8.9 | +4.7 / **+49.9** |
| `pos_shared` | +2.0 / **+7.0** | **+22.4** / +9.1 | **+29.2 / +19.6** |
| `dm_cache` | **+6.1 / +15.3** | +5.1 / **+17.1** | +5.7 / **+36.7** |
| `knob_rows64` | **+21.9 / +47.1** | **+18.8 / +44.3** | **+53.6 / +95.3** |

Bold cells miss the gate. Every cell, the A/A's included, is the larger of its two deltas by value, so the max hides negative per-op cells: the A/A's bool hot row at 64 threads is −5.0 behind the cell's −1.7.

1. **Only `ever_bit` passes OB1's gate in every row** (worst +4.6 ns, integer scattered at 64 threads). It answers "did this handle read this knob" exactly, at 1 bit per definition: 12.5 KB per handle at 100,000 knobs, 625 KB at 5M.
2. **No exact count passes on scattered names.** Each lookup of a different knob writes a different line: a per-thread row touches 1,024 lines per thread (`pos_tls` +46.2 ns at 64 threads), shared and striped rows add coherence misses (`pos_shared`, `pos_stripe_rmw`), and `dm_cache` evicts on nearly every scattered lookup and pays the shared add anyway. On a hot name `pos_tls` and `pos_stripe_rmw` pass, because one line stays private. The instructions are not the cost: with the lookup removed (`--bare 1`, strict screen `261109-141658-03e1a9`) every one of them costs at most +3.4 ns except the shared and striped atomics. The cost is the written line per distinct knob, which an exact count cannot avoid.
3. **The cheapest exact count is `pos_tls`**, and it misses the 10 ns ceiling by 36.2 ns at 64 threads on scattered names; it also allocates 400 KB per thread at 100,000 knobs (20 MB at 5M) and needs a thread registry (Alternatives, per-thread counters).
4. **The A/A spread** (`none2`) is within ±1.7 ns in every per-op cell but one, bool hot at 64 threads (−5.0 ns): there two copies of the same loop at different addresses differ by 5 ns. The A/A bounds placement only; the bench's error, below the gate table, adds code shape.

The strict screen `261109-141658-03e1a9` at `Detent:4871285` (3 repetitions, 131,072 calls) agrees that `ever_bit` passes every row and that every exact count fails scattered names at 64 threads; beside other work it also fails ten 1- and 8-thread rows that the exclusive confirm passes.

**The shape (Q7).** Every attached namespace generation in a view gets an ever-read bitmap, one bit per definition in that generation, allocated by the refresh winner when it attaches the generation (the refresh path already allocates and maps; a warm lookup does neither). A lookup that finds a definition tests its bit and sets it if clear, after the answer, beside the sampled countdown. A lookup that finds no definition has no position; `missing` counts it in the sampled table.

1. **Across generations.** When the D5 freer retires a view, no reader holds it, so its bitmaps are final. The freer ORs each set bit into the bitmap of the generation that replaced it, finding the knob by name there (one lookup per distinct knob read, off the hot path), with `fetch_or` beside lookups that set bits in the new one. A knob absent from the new generation (deleted) is marked in its usage-table slot's `seen` flag, claiming a slot if needed; past `K` it counts in `overflow` (Limit 4). So the current bitmap holds every knob the handle has read since open.
2. **Leaving the process.** The page (D7 item 1) carries, per attached namespace, the revision and its bitmap; the refresh winner grows the page for a larger generation before it publishes the view. When the freer retires a generation, its bitmap's range returns to the page for the next attach, so the page holds one bitmap per generation that a live view holds, the bound on the views themselves. The agent resolves positions against the generation of that revision. A process on a revision the agent no longer holds is reported by its count of set bits until it refreshes, when the fold moves them (Limit 11).
3. **Cost and size.** One relaxed load of a word that stays shared after a knob's first read, and one `fetch_or` per knob per handle per generation. 12.5 KB per handle per 100,000 definitions read from, 625 KB for a whole 5M store. A handle opened with `usage_slots = 0` allocates no bitmaps and runs neither the bit nor the countdown (Limit 8); it is OB1's off handle.
4. **The gate.** At `Detent:09f038e` the pair (`ever_sampled`) misses OB1 at 64 threads (+11.9 / +14.4 above) though each part passes, while its counting code alone adds up (`--bare 1`, 64 threads: +1.3 for the bit, +2.0 for the countdown, +4.0 together). Three suspects were tested with the method above, strict pair-screen `261109-222819-1a0f6c` at `Detent:4ee258e` and exclusive pair-confirm `261109-224833-fa1352` at `Detent:f476cf7`, then rerun at `Detent:71e7c76` as exclusive `261110-021130-06137b` and `261110-021130-445051` and strict `261110-021130-368d1b` (one bench binary, digest `15b89164`):
   1. *A shared countdown.* At 64 threads 64 stacks fall on 54 stripes, so ten threads share a countdown line, and the S6 pin's `lock xadd` drains the previous lookup's countdown store before it. A thread-local countdown (`knob_tls`, `es_tls`, `es_tls_one`) is worse at every thread count (`knob_tls` +10.7 / +15.0 at 64 threads, `es_tls_one` +8.6 hot at 1 thread, against the stripe line's +4.0 / +4.4 and +2.6), so sharing is not the cause and the stripe line stays.
   2. *An unconditional bit store.* There is none: the bit is a load and a test, and a `fetch_or` only on a clear bit (disassembly of the bench at both commits; probe `261110-053432-d2ad94` records both binaries as a job).
   3. *The compiled loop.* The same source, rebuilt beside the new arms, moved `ever_sampled`'s 64-thread cells down by 1.2 to 6.1 ns per op (`bfb3cf` to `fa1352`) and its integer scattered 1-thread cell up from −0.4 ns to between +2.4 and +4.4 across the three later runs. The bench cannot attribute this to code shape or placement; probe `261110-053432-d2ad94` records both binaries' loops. One branch for both tests with an out-of-line slow half (`es_one`) is no better: +12.2 and +12.7 at 64 threads.

   | Arm (exclusive) | 1 thread | 8 threads | 64 threads |
   | --- | ---: | ---: | ---: |
   | `ever_sampled`, before (`09f038e`) | +3.7 / **+7.4** | +3.9 / +6.4 | **+11.9 / +14.4** |
   | `ever_sampled`, after | +2.5 to +3.4 / **+7.0 to +7.7** | +2.7 to +3.3 / +6.2 to +6.4 | +6.6 to +7.2 / +9.2 to **+10.4** |
   | `es_one` (two runs) | +2.2 to +2.7 / **+6.1 to +6.3** | +2.7 / +5.0 to +5.1 | +7.2 to +7.7 / **+12.2 to +12.7** |
   | `knob_sampled` | +2.6 to +2.9 / +4.6 to **+6.9** | +2.8 to +3.2 / +4.7 to +5.0 | +4.2 to +4.6 / +4.0 to +5.9 |
   | `ever_bit` | +2.5 to +2.9 / +2.3 to +2.5 | +2.3 to +2.5 / +1.9 to +2.4 | +2.7 to +3.4 / +6.3 to +6.8 |
   | `none2` | −0.9 / −1.0 to −0.5 | −0.8 to −0.6 / −0.8 to −0.5 | −0.0 to +0.2 / +0.9 to +1.0 |

   The after rows give the range over `fa1352`, `06137b` and `445051` (`es_one` is not in `06137b`); a bold value misses the gate in at least one run. Each cell is the larger of the integer and bool delta by value, which hides two per-op cells: `none2` bool hot at 1 thread is −3.9 in all three runs, and `knob_sampled` int scattered at 64 threads is −25.2, −29.3 and −25.5.

   At 8 threads the pair passes in every run (worst +6.4). At 64 threads bool scattered is +8.3 (`fa1352`), +10.4 (`06137b`) and +8.6 (`445051`), and integer scattered +9.2 to +9.5: one exclusive run of three exceeds the 10 ns ceiling, and the strict `368d1b` reaches +11.8 (integer scattered). At 1 thread bool scattered misses in every run, +7.0 to +7.7 against 5.0, where the countdown alone is +4.6 to +6.9 (+4.5 at `09f038e`, +7.8 in strict screen `261109-141658-03e1a9`). So the countdown, not the bit, carries the 1-thread excess, by an amount that moves by about 3 ns between builds and runs. The bit alone passes every row of every confirm.

   **The bench's error.** Two kinds bound what these tables resolve. Placement: the A/A compares two copies of one loop at different addresses, and its worst per-op cell is −3.9 ns at 1 thread (every run of the after binary) and −5.0 at 64 threads (`bfb3cf`). Code shape: on and off are two compiled loops (`block<arm>` and `block<none>`), which the A/A does not cover; `knob_sampled` adds work, yet on scattered integers at 64 threads it measures −25 to −29 ns against `none`. So the bench resolves about ±4 ns at 1 thread and leaves up to 29 ns of code shape at 64, and both rows D48 weighs, bool scattered at +7.0 to +7.7 against 5 at 1 thread and +8.3 to +10.4 against 10 at 64, sit inside that error in either direction. OB1's method removes the code-shape term: in the core, on and off are one compiled path, switched at run time by the off handle's `usage_slots = 0` (shape item 3). The core observability quest therefore re-measures OB1's gate that way, and its result decides both rows.

   **Decision (PLAN.md D48, 2026-11-10).** The owner keeps `ever_sampled`: the exact ever-read bit plus sampled counts and arm splits. It is the one row over the gate: bool scattered at 1 thread, +7.7 ns against 5.0 (exclusive `261109-224833-fa1352`, `Detent:f476cf7`), a miss that the countdown carries (`knob_sampled` +6.9 in the same row) and the compiled layout moves by about 3 ns. The pair passes the 8- and 64-thread rows (worst +9.3 against 10). OB1 re-measures the gate on the real hot path (stripe in a register, S6 lookup near 900 ns, Limit 7). The design proposes, beyond D48's terms: if the row still misses there, cutting the countdown's cost is OB1's work, and the bit stays.

   D48 was ruled on `fa1352` alone. In the two later exclusive runs the 64-thread row passes once and exceeds the ceiling once (+10.4), so "passes at 64 threads" holds in two runs of three; the lead has put that fact to the owner.

## Checks

Rerun with `campaign job rerun` on the four jobs at the commit under review (`observability-bench.sh build`, then `run smoke`, `run screen`, `run confirm`, on slot `obs-bench`, ref `s6=b1d281c`). Each run writes `<mode>.md` (the summary) and `<mode>-paired.md` (the paired deltas D2 quotes).

| Criterion | Check |
| --- | --- |
| A1 What is observed, bounds | D1 table; reviewer reads |
| A2 Hot-path cost | D10's interleaved exclusive runs `261109-224833-fa1352`, `261110-021130-06137b`, `261110-021130-445051`; D2's sequential screen `261109-120858-28cd50` and its reruns `261109-131328-0ce3a4`, `261110-021130-d5103d`, `261110-021130-1bad17` (large separations only); OB1's gate |
| A3 Export path | D7, D9; reviewer reads |
| A4 Hook | D6; `hook_off` and `hook_on` rows of D2 (unresolved there); OB3's interleaved gate |
| A5 Privacy and caps | D8 |
| A6 Build plan | "Build plan" below |
| A8 Exact counting | D10; exclusive exact-confirm `261109-202834-bfb3cf` (`09f038e`), strict pair-screen `261109-222819-1a0f6c` (`4ee258e`), exclusive pair-confirm `261109-224833-fa1352` (`f476cf7`); at `71e7c76`, exclusive `261110-021130-06137b` and `261110-021130-445051`, strict `261110-021130-368d1b`; PLAN.md D48 |
| A7 Jobs | Build `261109-120856-787b87`, smoke `261109-120857-5b7ebc`, screen `261109-120857-3fb43d`, confirm `261109-120858-28cd50` (cited in D2) |

## Limits

1. **Silent:** a 64-bit name-hash collision merges two knobs' counts (D3 item 2).
2. **Silent:** the ever-read bit says a knob was read, not how often; a knob read once per process shows its bit and, with probability 0.98 in one process and 0.85 across 10, no sampled read. A host's union covers only processes alive since the agent's last scan of them (D7 item 3).
3. **Silent:** a ramp's split carries the error of a sample count: a 50/50 split reads ±16 points at 640 reads, ±4 at 10,000 and ±1.3 at 100,000, and a 1% ramp shows about 1.5 samples per 10,000 reads.
4. **Silent:** past `K` names a handle counts into one overflow slot; a process with more live knobs than `K` loses per-knob detail for the rest (it sees `overflow` grow).
5. **Silent:** counts carry no revision; a scan interval that straddles a value-changing publish attributes its reads to the new value (D4).
6. **Silent:** a process that never looks up never refreshes, so its view record is old but its `current − seen` says so only when someone reads it.
7. Linux writeback of a dirty shared file page write-protects it again, so the first write after each writeback takes a minor fault on the lookup's thread; the page belongs on tmpfs (`/run`). D2's soak measures macOS only.
8. The per-knob table and the ever-read bitmaps cost memory per handle; `usage_slots = 0` turns off the table, the countdown and the bitmaps, and a hook set alone still runs.
9. The agent's view lags by its scan interval; the application's snapshot does not.
10. A process can forge its page; host and fleet counts are as honest as the processes on the host.
11. A process whose installed revision the agent no longer holds reports how many knobs it has read, not which, until its next refresh folds its bits into a newer generation (D10 item 2).
12. The host document's ever-read bitsets are not capped by its 65,536 rows: they cost one bit per definition per namespace revision the host's processes hold, 625 KB per revision at 5M (D8).

## Alternatives considered

**Exact per-knob counting** (`knob_rows8` and its variants, and D10's five mechanisms). Answers "was it read" and "how often" exactly, at +18.8 to +47.8 ns per lookup under 8 threads for `knob_rows64` (D10), and no exact count passes scattered names at 64 threads. Q7 (D47) takes the exact ever-read bit instead.

**A shared `fetch_add` per lookup.** +21.7 to +33.6 ns at 8 threads in D2's sequential run (+3.9 to +28.9 in its reruns) and no per-knob detail; reader.md removed it for cost.

**Per-thread counters** (`tls`, +0.4 to +14.6 ns). No contention, but a thread registry per handle (threads come and go in Python and Rust pools), `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5), and they carry no per-knob detail.

**Count in the application only (no agent path).** Every service would have to wire `detent_usage` or the hook into its own metrics stack before an operator sees anything; the owner's direction is counting in the client, published through a metrics engine. Kept as one of D7's consumers, not the only one.

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are nine cells, and the value is recoverable from the definition.

**Knob-name labels in Prometheus.** `Metrics` (`include/detent/client.hh:32-34`) takes no name label, and per-knob series across 250k agents do not fit a time-series store.

## Owner decisions (PLAN.md D47)

The owner took each recommendation on 2026-11-09 (PLAN.md D47). The labels stay, because the sections above cite them.

**Q1. The application API: D6.** `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. The table serves the agent with no application code; the hook serves applications that want every event. Not taken: a snapshot only, or a hook only with no table in the core.

**Q2. Names per call, no prepared knob handles.** A sampled lookup hashes the name; hash and probe cost +15 to +45 ns (D2 item 2), but sampling runs them once per 64 lookups. Not taken: `detent_knob` handles resolved once, which change every binding to save under 1 ns on average.

**Q3. Host export: a document and a text file, no listener.** `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file in a configured directory (the node exporter's textfile collector reads it), rendered by Kronuz/metrics (D9). No listener, port or credential on every host. Not taken: an HTTP `/metrics` listener, OpenTelemetry.

**Q4. Fleet aggregation upstream, after namespace delivery S4-S10.** Agents send per-knob summaries (reads by arm and the ever-read set, D10) upstream on their relay sessions; relays sum; the authority serves per-knob readers. It waits for S4-S10, which changes the same sessions (OB6).

**Q5. Values, not digests, in the usage document.** Every agent already holds the definition.

**Q6. The page is on whenever the agent's store has `usage/`** (an agent setting), so operators get host data without changing applications.

**Q7. Exact usage, sampled counts.** Each handle keeps an exact "ever read" bit per definition (`ever_bit`, D10) beside D2's sampled table, so "was this knob read" is exact per handle, per host and, with OB6, per fleet, while counts and arm splits stay sampled (Limit 3). The host document carries the ever-read set as bitsets outside its capped count rows (D8), so the row cap never reports a read knob as unread. D47 keeps this shape unless an exact-count mechanism meets OB1's gate at scattered names and 64 threads; none did (D10). D48 keeps it with the pair's one miss, bool scattered at 1 thread (+7.7 against 5.0, D10 item 4); OB1 re-measures it in the core, with one compiled path, which removes the bench's code-shape error. The 64-thread row exceeds its ceiling in one of three exclusive runs (+10.4), a fact the lead has put to the owner. Not taken: sampled only, which misses a knob read once per process with probability 0.98 in one process and 0.85 across 10; exact per-knob counts, which cost +19.6 to +49.9 ns on scattered names at 64 threads (D10).

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| OB1 Usage table, sampling, ever-read bits and arms in the core | `src/detent_local/evaluation.hh`, `src/detent_reader/core.cc`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/` | D2's cost gate with the ever-read bit and the countdown together (D10; paired, interleaved on and off; ≤ 5 ns at 1 thread, ≤ 10 ns at 8 and 64 threads; exclusive job); every knob looked up has its bit, across a publish that reorders and deletes definitions (the D5 fold); exact under 16 threads with the period forced to 1 (TSan); a 1,024-name cycle samples every name with the random reload; a fresh handle's first 64 lookups are not all sampled; zero allocation and syscall tests (reader.md T1) unchanged; fork child counts apart and reseeds; every fallback status lands in its cell; test generations from `publish(Snapshot)` | high (hot path) | reader S6 landed; Q1, Q7 |
| OB2 View records and `detent_usage` | `core.cc`, `reader.h`, tests | Lag and age correct across publishes (T3's helper) | medium | OB1; reader S5 (landed) |
| OB3 Hook | `core.cc`, `reader.h`, `reader.hh` | Hook after unpin (park-hook test: a stalled hook never refuses a publish); `hook_off` within OB1's gate, interleaved against a handle with no hook (exclusive) | medium | OB1, Q1 |
| OB4 Usage page, agent collector and export | Kronuz/metrics (Prometheus text renderer), `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz (truncation, oversize, bad names) never crashes the agent; a pre-created `usage/<pid>-<handle>` makes the handle fall back to a private table; on Linux, a page created by another uid is readable by the agent (VM); dead-process retention; Linux tmpfs fault count 0 over 10 min (VM); the usage document matches a golden schema, caps at 65,536 rows with `truncated` set, and splits rows by revision; past the cap the ever-read bitsets still hold every knob a test process read; the ever-read union matches the knobs a multi-process test read, and a process on a dropped revision reports its count (Limit 11); the text file parses as Prometheus exposition format, carries no knob-name label, and is replaced by rename | medium | OB1, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | reader S8, S9 (`reader-s7-s9`) |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred; carries the ever-read union and sampled reads by arm (Q4) | high | Q4, Q7; namespace S4-S10 |

reader S7 (`detent get` on the core) needs nothing from this design; `detent get` opens a handle with `usage_slots = 0`.
