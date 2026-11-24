> Status: DRAFT (2026-11-24) Ready for design re-review: F1-F8 and N1-N8 of `observability-core-design.md` (`Detent:549a81d8`) folded in; the claim protocol is checked by `tests/detent_reader/usage_claim_model.cc` under TSan. The owner accepted D6's ABI shape (PLAN.md D158: `usage_slots = 0` is the default, `DETENT_USAGE_OFF` is off, `switch_forced` takes `wrong_type`'s place in the nine cells) and decides D10's mechanism, claim index or segment bitmaps, from D10's comparison. Earlier rounds: OD-1 to OD-9 and OR1 to OR10 (`observability-design-design.md`), Q1-Q7 (PLAN.md D47), `ever_sampled` (D48, D61). Quests `observability-design` (D37) and `observability-core`.

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, recorded inside the reader core: one row per knob read, claimed on its first read, and one lookup in 64 sampled into the row's arm counters.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:805cd0c6` (main): the paged reader core, one knob type (PLAN.md D65). Measurements in D2 and D10 ran on the S6 core, `Detent:b1d281c`.
- Depends on: reader.md R1 (the caller passes each lookup's default) and R2 (no overrides in the reader), D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork), T4 (heap budget); client-local-source.md D7 (a namespace's entry); the `detent_*` C ABI. Benchmark for D2 and D10: `docs/design/observability_bench.cc` with `observability-bench.sh`, `observability-summary.py` and `observability-paired.py`, at `Detent:7aed2593^`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering with its caller's default. The reader core counts nothing per lookup, and reader.md removed a shared per-lookup `fetch_add` for cost (`docs/design/reader.md:39`).

The design records in the core, after the answer, into a per-handle **usage table**: one row per knob name read OK, keyed by the full name, holding nine sampled counters (the arm that answered, or why the caller's default was returned). Each attached generation keeps a **claim index** keyed by the definition's address, so a knob's reads after its first cost one load, and one lookup in 64 per stripe adds to a counter. The rows say exactly which knobs the handle read, up to `usage_slots` knobs (default 4,096); past it a flag says the record is incomplete. Counts are cumulative and never reset, so any number of consumers take deltas. `detent_usage` reports the rows, exact per-status totals and one view record per attached namespace. The hook (D6) and the agent's shared page (D7) are later quests. Values never enter the hot path: the arm and the generation determine the value, except for a forced switch value (D4).

On the S6 core the shape measured +2.3 to +6.4 ns per warm lookup at 8 threads (D10). The in-core gate (D2) counts events exactly and times on against off as a ratio.

## What a lookup can tell today

| Signal | Where it exists | Where it does not |
| --- | --- | --- |
| Knob read | The name passes `split` and `opened` (`src/detent_reader/core.cc:794-798`) | Nothing records it |
| Resolved value | `evaluate` returns the `int64` (`src/detent_local/evaluation.hh:14-36`); `detent_get` writes it (`core.cc:812`) | Not kept |
| Arm | Implicit in `evaluate`'s branches: a matched override (forced or group rollout), then the rollout, then the default | Never returned |
| Caller's default taken | Every status but `DETENT_OK` writes the caller's fallback (`core.cc:812`; R1) | Not counted |
| View freshness | Each entry keeps the sequence it was attached at (`core.cc:139-144`); the handle counts publishes, refusals and attaches (`core.cc:194`) | Readable only through `testing::stats`; no attach time; stale answers (`core.cc:675`) uncounted |

## Decisions

### D1. What is observed, and its bounds

| Signal | Kept as | Per process | Per host (agent, OB4) | Per fleet (OB6) |
| --- | --- | --- | --- | --- |
| Knob read | A row per knob name read OK, exact up to `usage_slots` knobs; past it the overflow flag (D3) | At most `usage_slots` rows | The union of names over processes | The union over hosts |
| Reads per knob, by arm | 9 sampled counters per row (D2, D4) | Per row, and one overflow row | One row per knob and revision | One row per knob |
| Caller's default taken | Exact totals per status; sampled `missing` and `fallback_other` cells in the knob's row when it has one | 10 status counters | Summed | Summed |
| Generation each process reads | Per attached namespace: revision, sequence seen and current, attach time, state (D5) | One record per attached namespace | One per process and namespace | Revision lag per host |
| Publishes, refused publishes, attaches | The handle's existing counters (`core.cc:194`) | 3 counters | Summed | |

Nothing is keyed by `hash_val`, `switch_val`, the caller's fallback value or the switch rule that matched (D8). The value is what the generation published; the fallback is the caller's argument (R1, R2).

### D2. Where it is counted, and what it costs

The count runs inside `detent_get`'s answer, once the status and arm are known (`core.cc:801-811`), before the pin is released; the stripe is the lookup's own (`core.cc:663-664`). A status the entry point returns before `lookup` (`DETENT_INVALID_ARGUMENT`, `DETENT_INVALID_NAME`, `DETENT_NOT_OPENED`, `core.cc:792-798`) counts in the totals only. `detent_revision` shares `lookup` (`core.cc:825`) but reads no knob, and counts nothing.

**Sampling: one lookup in 64 per stripe.**

1. **Where the countdown lives.** The handle holds 64 cache lines, one per stripe, apart from D5's pin lines. Each holds the countdown word, a 64-bit xorshift state and the stripe's status totals.
2. **Per lookup.** A relaxed load and store decrements the countdown; a lost decrement between two threads on one stripe only moves a sample.
3. **At zero.** The lookup adds 1 to its row's arm cell (D3) and reloads the countdown from the stripe's xorshift with a value in [1, 127] (mean 64), so a caller that cycles through 64 × k names does not sample the same names forever, as a fixed reload of 63 would.
4. **Seeding.** At open, each stripe's state is seeded from one `CLOCK_MONOTONIC` read mixed with the handle's address and the stripe index, and its countdown starts at a value in [1, 127], so a handle does not sample every stripe's first lookup. A fork child reseeds in the child handler from `getpid()` and the clock (D3, Fork).

**Sequential screen on the S6 core.** Each arm runs its own pass, and the cost per warm lookup is the median over 7 repetitions of the arm's case p50 minus `none`'s case p50 in the same repetition (`observability-paired.py`), exclusive job `261109-120858-28cd50`, 262,144 calls per thread, the hot set one name and the scattered set 1,024 names. `none` is the S6 lookup with no counting; `none2` is the same arm run again. Three later runs (`261109-131328-0ce3a4`, `261110-021130-d5103d`, `261110-021130-1bad17`) do not reproduce its small cells: their `none2` at 8 threads is −10.2 to +63.9 ns. The table separates only the large differences, +20 to +60 ns; D10's interleaved method resolves the small ones.

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

1. **Exact per-knob counting costs tens of ns at 8 threads**: +23 to +64 ns here, and every median of `knob_rows8`, `knob_rows64` and `knob_shared` in the three later runs is above +12 ns. Most of it is finding the slot: hash and probe alone (`knob_probe`) cost +15 to +45 ns, and keying by pointer (`knob_ptr_rows8`) still costs +16 to +28 ns.
2. **Exact per-handle counts are cheap** (`stripe_rmw` +2.5 to +13.6 ns, `tls` +0.4 to +14.6 ns, within this method's A/A spread), but say nothing per knob. The core keeps per-handle totals only for statuses other than OK, which are off the answer's path.
3. **The mapped page costs what memory costs.** `knob_rows8_mapped` is within 21 ns of `knob_rows8` either way. The 60-second single-thread soak on scattered integers: in memory p50 1,198.9 ns, worst 64-call batch 22,095 ns, 73 minor faults; mapped p50 1,192.0 ns, worst batch 4,921 ns, 73 minor faults (macOS; Linux is Limit 6).
4. **One thread resolves nothing in this method** (`none` moves 221 to 287 ns between repetitions); D10's interleaved method resolves it to about ±4 ns.

**The gate.** Two parts. The first decides at any run level, the second at strict or light, so no gate waits on an exclusive run; one exclusive run of the finished feature is the measurement it publishes (PLAN.md D135).

1. **Events (G1), exact.** `test_reader_usage` counts what a warm lookup does through `testing::Stats` (`claims`, `probes`, `samples`, `rmw`): after a warm-up that reads 1,024 knobs once, 1,048,576 further lookups make 0 claims, 0 allocations and 0 syscalls (reader.md T1's counters), at most 1.25 index probes per lookup, one read-modify-write per sample and none otherwise, and `samples` within 1% of lookups / 64. With `usage_slots` 256 against the 1,024 names, the knobs past the bound take the same counts plus no claims (their index slots hold the overflow row). It fails on any extra write or probe the hot path gains.
2. **Time (G2), as a ratio.** `reader_bench --usage-gate` opens three handles over one store: one on and two with `DETENT_USAGE_OFF`. On each thread it alternates them per block of 64 lookups over the same names, rotating the order. Per repetition it takes the median over blocks of (on − off) / off and of (off' − off) / off, the A/A. Cases: 1, 8 and 64 threads; hot (one name) and scattered (1,024 names); plain and hashed lookups; and scattered with `usage_slots` 256. At least 7 repetitions. A run is valid when the median A/A is within ±0.5%; an invalid run decides nothing and is queued again. A valid run passes a case when the median on-ratio minus the median A/A is at most **1% at 1 thread and 2% at 8 and 64 threads**. On reader-lookup.md's 1-thread lookups (460 to 700 ns) 1% is 4.6 to 7 ns, D48's 5 ns. Load from other holds stretches on and off alike within a block, so the ratio holds where an absolute ceiling does not: strict screens `261109-141658-03e1a9` and `261110-021130-368d1b` failed absolute rows that exclusive runs passed.

### D3. The usage table and the claim protocol

```
handle ── rows[usage_slots]: hash (8 B) | name length (1 B) | name (200 B) | 9 sample cells (72 B)   288 B
       ── name set: 2^⌈log2(2·usage_slots)⌉ × u32 (0 empty, else row + 1), insert-only
       ── next (bump), spares[64] (0 empty, else row + 1), overflowed flag, overflow row's 9 cells
generation ── claim index: 2^⌈log2(2·min(usage_slots, root.records))⌉ × {address u64, row u32} (16 B, padded)
           ── used (slots claimed)
```

At the default 4,096: rows 1.18 MB, name set 32 KB, and up to 128 KB of index per attached generation. Rows, the name set and indexes are anonymous `mmap`s, so a fork child replaces them with one `mmap` each instead of clearing them (Fork, below). `usage_slots` is at most `DETENT_MAXIMUM_USAGE_SLOTS` (8,388,608, past the store's 5M knobs); larger values fail the open with `DETENT_INVALID_ARGUMENT`.

**A row's life.** A row is written privately and published once; its name never changes after.

1. **Find or claim by name** (`find`). Hash the name, probe the name set from `hash & mask`. A nonzero slot (acquire) names a published row: equal hash and equal name bytes is a match, anything else probes on. An empty slot ends a find that may not claim. To claim, take a private row (a spare first, else `next`, once per claim), write its hash, length and name, then compare-and-swap the empty slot from 0 to the row (release on success, acquire on failure). A failed compare-and-swap re-examines the same slot, which is now published: a match returns that row and gives the private row back to the spares; a different name probes on.
2. **No duplicates.** A name set slot goes from empty to published once and never back, and a claimer examines every slot of its probe sequence in order until it matches or publishes. Two claimers of one name reach the same first empty slot; one publishes, the other re-examines that slot and matches. Equal hashes never merge two names, because the match compares the bytes.
3. **No waiting.** Nothing is published before its name is complete, so no reader meets a half-written row, and no loop waits on another thread: each step either publishes, matches or moves to the next slot. A claimer parked anywhere delays only itself.
4. **Spares.** A claimer that loses to its own name returns its private row with a compare-and-swap into the first empty one of 64 spare cells; the next claim takes it. More than 64 losers at once, with no claim between, lose a row each (Limit 9). The name set holds at most `usage_slots` rows in twice as many slots, so probing ends.
5. **Full.** A claim with no spare and `next` at `usage_slots` sets the overflowed flag (release, set once: test before set) and returns the overflow row.

**A knob's mark in a generation.** After `lookup` finds a definition, its body address keys the generation's claim index (the address is stable for the `Generation`'s life, since segments are unmapped only by `PageTable`'s destructor, `lazy_page_access.cc:110-126`; the index is never shared between `Generation` objects or keyed by namespace).

1. **Hit.** Probe from the high bits of `address · φ` (multiply-shift; aligned addresses share their low bits): a slot whose address (acquire) equals the body's is this knob. An unsampled hit stops there. A sampled hit loads the slot's row (acquire); if the row is not stored yet, `find` it, since it was published before the address (item 3).
2. **Miss.** At an empty slot, if `used` has reached half the slots, set the overflowed flag and count in the overflow row. Otherwise `find` or claim the row by name first, then compare-and-swap the slot's address from 0 (acquire-release), add 1 to `used`, and store the row (release). A failed compare-and-swap re-examines the slot: the same address is a hit, another probes on.
3. **Visibility.** When a lookup returns OK, its knob's row is published or the flag is set: it either ran `find` itself, or loaded the address with acquire, which follows the claimer's release of the row (or the flag) before the address. So a `detent_usage` after the return sees the row or the flag. A knob claimed when the table was full stores the overflow row in its index slot, so its later hits take one load too.

**Fallbacks take no row.** A `missing` or other fallback lookup adds to the stripe's exact status total; when sampled it `find`s the name without claiming and adds to the row's `missing` or `fallback_other` cell if the knob has a row, and otherwise to nothing per name. So only knobs read OK take rows: names built from input or knobs deleted before they were read cannot use up `usage_slots`, and a defined knob reaches overflow only past `usage_slots` knobs read OK. The cost is D1's per-name `missing` detail for names that never resolved: they appear only in the totals.

**Across generations.** Rows live for the handle, keyed by name, so the record of "read since open" survives every publish: a new generation starts with an empty index, and each knob's first read there `find`s its existing row. A knob deleted from the store keeps its row.

**Fork.** The child handler replaces the rows, name set and every attached generation's index with fresh anonymous mappings (`mmap` with `MAP_FIXED`, no file I/O and no copy of the parent's pages), zeroes `next`, the spares, the flag and the stripe totals, and reseeds each stripe from `getpid()` and the clock. The child's rows are its own reads.

**Estimates.** A knob read `n` times shows about `n / 64` samples, with a relative standard error near `√(63 / n)` (8% at 10,000 reads, 0.8% at 1M).

### D4. The arm, and values derived from it

`evaluate` returns `{value, arm}` (`evaluation.hh`); the arm is the branch it already takes, and its callers that want the value take `.value`.

| Arm | Value | Derivable from the definition and revision |
| --- | --- | --- |
| `published` | The default: no override matched and the knob has no rollout | Yes |
| `no_hash` | The default, because the caller passed no hash to a rollout or a matched group rollout | Yes |
| `ramp_in`, `ramp_out` | The rollout value, or the default: the rollout's bucket | Yes |
| `switch_forced` | A matched override's forced value | No: overrides force different values per group, and the group is never recorded (D8); OB4 decides how to export it |
| `switch_in`, `switch_out` | The rollout value or the default: a matched group rollout's bucket | Yes |
| `missing`, `fallback_other` | The caller's fallback (R1) | Never recorded |

`no_hash` is its own arm because a caller that passes no hash to a rollout silently gets the default for every user. Counts are not keyed by revision: the agent attributes a process's counts to the revision it has installed when the agent reads it (Limit 4). `Reader::get_as<bool>` returns its fallback for a value other than 0 or 1 (`reader.hh:70-71`); the core has already recorded that lookup as OK under its arm.

### D5. Freshness, off the hot path

The refresh winner reads `CLOCK_MONOTONIC` with `clock_gettime` when it attaches a generation (not `now_ns`, which is `mach_absolute_time` on macOS, `core.cc:71-84`), so on one host every process's attach time is on the agent's clock. Each entry also records its state. A view record is read from the installed view:

| Entry state | Set at | `revision` | `seen`, `current` |
| --- | --- | --- | --- |
| current | attach or publish | the answering generation's | the entry's slot: the sequence attached at, and the sequence now; `current − seen` is the publishes not yet looked up |
| failed | a newer generation failed to attach (`core.cc:567-573`) | the old, answering generation's | the slot, with `seen` at the failed sequence, so `current − seen` is 0 though the answer is old; the state says so |
| dropped | the table no longer lists it (`core.cc:564`) | the last generation's | the store word, which the entry then watches |

### D6. The application's view (Q1)

The owner accepted this shape (PLAN.md D158); `switch_forced` keeps the cell count at nine, in the order below.

```c
typedef enum detent_arm {
  DETENT_ARM_PUBLISHED, DETENT_ARM_NO_HASH, DETENT_ARM_RAMP_IN, DETENT_ARM_RAMP_OUT,
  DETENT_ARM_SWITCH_FORCED, DETENT_ARM_SWITCH_IN, DETENT_ARM_SWITCH_OUT,
  DETENT_ARM_MISSING, DETENT_ARM_FALLBACK_OTHER, DETENT_ARM_COUNT
} detent_arm;
typedef enum { DETENT_VIEW_CURRENT, DETENT_VIEW_FAILED, DETENT_VIEW_DROPPED } detent_view_state;
typedef struct { size_t size; detent_str name;          /* data == NULL: the overflow row */
                 uint64_t samples[DETENT_ARM_COUNT]; } detent_knob_usage;
typedef struct { size_t size; detent_str name_space; uint64_t revision, seen, current;
                 uint64_t attached_ns;                   /* CLOCK_MONOTONIC */
                 uint32_t state; } detent_view_usage;
typedef struct { size_t size; uint32_t period;          /* 64; 0 on an off handle */
                 uint32_t overflowed;                    /* 1: absence from the rows proves nothing */
                 uint64_t statuses[16];                  /* exact lookups by detent_status */
                 uint64_t publishes, refused, attached; } detent_usage_totals;
detent_status detent_usage(detent_reader *, detent_usage_totals *,
                           void (*knob)(void *, const detent_knob_usage *),
                           void (*view)(void *, const detent_view_usage *), void *context);
/* detent_open_options grows: uint32_t usage_slots;  0: 4,096; DETENT_USAGE_OFF: no table */
#define DETENT_USAGE_OFF UINT32_MAX
#define DETENT_MAXIMUM_USAGE_SLOTS 8388608u
```

1. **Rows.** Each published row is a knob the handle read OK since open (or, in a fork child, since the fork); `samples` × `period` estimates its reads by arm. The overflow row is reported once the flag is set. A row's cells are independent relaxed counters, not a snapshot of one instant. A row says nothing about which revision answered, or whether the knob still exists.
2. **Absence.** With `overflowed` 0, a defined knob with no row was never read OK. Once `overflowed` is 1 it stays 1 for the handle's life, and absence proves nothing.
3. **Views.** One record per attached namespace, in name order (D5). For each record `detent_usage` pins the installed view, copies the record and the namespace name (at most 198 bytes) to its stack, and unpins before the callback, so no pin is held during a callback and a slow callback never refuses a publish. A namespace attached or dropped during the walk may be reported or not.
4. **Callbacks.** They run on the calling thread, with no lock or pin held. They may look up knobs and call `detent_usage`; they must not call `detent_close`, and the strings they receive live only during the call. Either callback may be NULL.
5. **Off.** A handle opened with `DETENT_USAGE_OFF` keeps no rows, countdown, index or totals: `period` and `overflowed` are 0, the totals are zero but for `publishes`, `refused` and `attached`, no knob is reported, and view records are reported.
6. **ABI.** `usage_slots = 0` means the default, as `mapping_bytes = 0` does (`core.cc:745-746`), so a caller that zero-fills the grown struct, as `Reader` does (`reader.hh:43`), keeps the table; off is explicit. `DETENT_ABI_VERSION` stays 1: the change adds a function and grows size-tagged structs, and no library has shipped (`reader.hh:37` compares equality).
7. **C++.** `Reader::usage()` returns the totals with vectors of plain structs. Python and Rust wrap it as they wrap lookups (client-local-source.md D1; OB5).

The hook (Q1) is fixed at open, runs after the pin is released, and is a later quest; the table records whether a hook is set or not.

### D7. Leaving the process and the host (OB4)

```
app process: usage table ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file, D3's rows) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3, D9)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the rows, name set and totals live in it. The directory is the agent's: owner `publisher_uid`, group the agent's group, mode 3733 (setgid and sticky), so others create files but cannot list, nobody deletes another's file, and on Linux each file takes the agent's group. If the create fails, the handle keeps private memory and reports it in its totals.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own counts (Limit 7).
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** The child's handler maps fresh anonymous memory over the page's range (D3, Fork), and its next refresh creates its own page.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (`src/detent_relay/host.cc:59-127`): per knob and revision the estimated reads by arm (samples × period) and the derived value (D4); the read set as the union of the processes' row names, outside the capped count rows (D8); per process its namespaces, revisions, lag, age and overflow flag. Label-free totals (lookups by status, refused publishes, processes) are written as a Prometheus text file (D9).
6. **Fleet.** The union of read names across hosts answers "which knobs are dead" for every process whose flag is clear; sampled reads answer "how often", and only at fleet scale for rare knobs: 1,000 processes reading a knob 10 times each yield about 156 samples. It needs an upstream message from agent to relay to authority, a wire change (Q4).

### D8. Privacy and limits

| Data | Exported as | Why |
| --- | --- | --- |
| `hash_val`, `switch_val` | Never stored | User or tenant identifiers (`include/detent/errors.hh:20`) |
| Switch rule that matched | Never stored; only the arm | The rule's key equals a caller's switch value |
| Caller's fallback value | Never stored; only that it was returned | Caller-supplied, unbounded (R1) |
| Knob name | In the usage document and `detent_usage`, never a metric label | Per-knob series across 250k agents do not fit a time-series store |
| Resolved value | Derived from the published definition (Q5), except `switch_forced` (D4) | Already on every agent's disk |

Unbounded sets are capped where they enter: `usage_slots` names per handle, then the overflow row; names at most 200 bytes (`core.cc:325-333`); the host document at most 65,536 count rows, ordered by reads, with a `truncated` count; fleet rows at most the store's knobs. The read set is not in those capped rows, because ordering by reads would drop the rarely read knobs first, the ones it exists for: it is the union of names, at most the store's knobs.

### D9. Kronuz/metrics

The agent keeps host aggregates and its scan and attach durations in `kronuz::metrics::Counter` and `DurationHistogram` (single owner, `metrics.hh` at the pin `metrics:9db73de`), as the authority does. The read path uses neither `AtomicCounter` (+55 to +65 ns paired at 8 threads in D2) nor `ContendedCounter` (+50 to +55 ns). The Prometheus text file needs a renderer for those types; it belongs in Kronuz/metrics, header-only beside the counters it renders, so the relay and authority can serve the same format later. It is a change to the metrics library under its own review (OB4's first step).

### D10. Exact reads: the mechanism

The owner asked for exact counts if they can be made cheap (Q7, D38). Five exact mechanisms beyond D2's rows were measured on the S6 core, each indexed by a dense definition position that core held:

1. `pos_shared`: one row of 64-bit counters, a relaxed `fetch_add` per lookup.
2. `pos_stripe_rmw`: 64 rows of 32-bit counters, one per stripe, a relaxed `fetch_add`.
3. `pos_tls`: one row of 32-bit counters per thread, a relaxed load and store.
4. `dm_cache`: a per-thread cache line of 8 (position, count) pairs, direct-mapped by position; an eviction adds its count to the shared row.
5. `ever_bit`: one bit per definition, test before set: a relaxed load, and a `fetch_or` only on a knob's first read.

Beside them: `ever_sampled` (`ever_bit` and `knob_sampled` together), `knob_sampled` (D2's countdown), `knob_rows64` and `none2` (A/A). The method: the arm and `none` in one binary, alternated per block of 64 lookups on each thread over the same names, in both orders; a repetition's delta is the mean of its on-first and off-first block-pair medians, and the table gives the median over 7 repetitions. Exclusive job `261109-202834-bfb3cf` at `Detent:09f038e`, 262,144 calls per thread, 100,000 knobs, the hot set one name and the scattered set 1,024 names; 64 threads on 16 hardware threads share stripes. Each cell is the larger of the integer and the bool delta, hot / scattered, in ns; bold cells miss D48's ceilings (5 ns at 1 thread, 10 at 8 and 64):

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

1. **Only a test-before-set mark passes in every row** (`ever_bit`, worst +4.6 ns). The cost of an exact count is the written line per distinct knob, which no count avoids: with the lookup removed (`--bare 1`, strict screen `261109-141658-03e1a9`) every mechanism costs at most +3.4 ns except the shared and striped atomics.
2. **The pair.** At `Detent:71e7c76` (exclusive `261109-224833-fa1352`, `261110-021130-06137b`, `261110-021130-445051`) `ever_sampled` measured +2.5 to +3.4 / +7.0 to +7.7 at 1 thread, +2.7 to +3.3 / +6.2 to +6.4 at 8, and +6.6 to +7.2 / +9.2 to +10.4 at 64. The 1-thread excess is the countdown's (`knob_sampled` +4.6 to +6.9 in that row), and both rows near a ceiling sit inside the bench's error: about ±4 ns of placement (the A/A's worst cell, −3.9 ns at 1 thread) and up to 29 ns of code shape, since on and off were two compiled loops. The owner kept the pair (PLAN.md D48, D61); D2's gate re-measures it in the core, where on and off are one compiled path switched by `DETENT_USAGE_OFF`.

**The paged core has no position.** A definition is a leaf body in a lazily mapped trie, found by `PagedReadState::definition` (`src/detent_local/paged_read_state.cc:9-35`); nothing numbers definitions densely. Two marks fit it:

- **(a) The claim index (D3).** Keyed by the body's address, one per attached generation; a hit is one load of a slot that stays shared after the first read, `ever_bit`'s shape, though its footprint is 16 bytes per slot against one bit. Exact up to `usage_slots` knobs, stated past it. Changes `core.cc` only; the rows carry the names; no fold.
- **(b) Segment bitmaps.** One bit per 8 bytes of each mapped segment (bodies are at least 12 bytes apart), allocated beside `VerifiedPages` when `PageTable` maps the segment (`lazy_page_access.cc:63-71`): 16 KB per 1 MiB segment touched. `PagedReadState` returns the body's segment and offset. Exact for every definition. The rows (D3) stay for counts and names up to `usage_slots`; a bit with no row is named only by walking the generation's verified pages, and a retiring generation's bits must be folded into its successor by name, or lost.

Memory per handle with one namespace attached, at the store sizes below (63 bytes of segment per knob, from the 5M store's 315,759,038 bytes, `dense-pages.md:251`), reading R knobs scattered. (a) takes `usage_slots` = max(4,096, R) for exactness; (b) keeps the default rows and pays per segment touched:

| Store | Segments | R = 1,000 or 4,096: (a) | (b) | R = every knob: (a) | (b) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100K | 7 | 1.34 MB | 1.33 MB | 34.0 MB | 1.33 MB |
| 1M | 61 | 1.34 MB | 2.21 MB | 330 MB | 2.21 MB |
| 5M | 302 | 1.34 MB | 6.0 to 6.2 MB | 1.78 GB | 6.2 MB |

(a)'s 1.34 MB is rows 1.18 MB, name set 32 KB and index 128 KB; (b)'s is the same rows and set plus 16 KB per segment touched (all 7 or 61; 291 of 302 at R = 1,000). Each further attached namespace adds its index or its bitmaps. reader.md T4's 1.5 MiB heap budget for the core (1.57 MB) holds (a) at the default with 0.23 MB to spare, and (b) only at 100K.

At the shipped default the claim index is equal at 100K and 1.6 and 4.5 to 4.6 times smaller at 1M and 5M. Exactness for every knob read costs it 26, 149 and 288 times the bitmaps' memory; the crossover is near 6,500 knobs read at 1M and 19,000 at 5M. The bitmaps also cost CPU the index does not: `detent_usage` walks verified pages to name a bit without a row, and the fold runs when a generation retires.

*Recommendation: (a)*, because at the default it costs the least everywhere, needs no walk or fold, and stays in `core.cc`; it narrows Q7 from "exact per definition" to "exact up to `usage_slots` knobs read, past it stated". If the owner holds Q7's every-definition exactness, (b) is the mechanism that meets it at a bounded cost, and the numbers above recommend it. The decision is the owner's (D47, D48, D61).

## Lifecycle

States per handle: **off** (`DETENT_USAGE_OFF`) or **on**. Per attached generation: its claim index, empty at attach. Per entry: current, failed or dropped (D5). The answer of a lookup never depends on usage. Each test is in `tests/detent_reader/test_reader_usage.cc` and fails without the property its row names; seams are in `src/detent_reader/testing.hh` (forced period, forced equal hashes, refused index, index capacity, claim windows, `Stats::claims`, `probes`, `samples`, `rmw`).

| Event (code) | Usage change | `detent_usage` then | Test |
| --- | --- | --- | --- |
| Open, on (`core.cc:735-788`) | Rows, name set, spares, stripe lines mapped; countdowns seeded in [1, 127] | No rows; period 64; totals 0 | `fresh_handle`: over 1,000 fresh handles the first lookup is sampled between 1 and 60 times (a fixed start of 1 gives 1,000, of 64 gives 0) |
| Open, off | Nothing mapped | Period 0, no rows; views reported | `off_handle`: lookups leave `claims`, `samples` and the totals at 0; views present |
| Open, `usage_slots` past the maximum | Open fails `DETENT_INVALID_ARGUMENT` | | `open_bounds` (the maximum opens, one more fails) |
| First attach of a namespace (`core.cc:575-584`) | The generation's index mapped; attach time read; state current | A view record: revision, `seen == current`, `attached_ns` between the test's clock reads | `view_records` |
| Index refused at attach | The generation attaches without an index; its OK lookups set the flag | The same answers; `overflowed` 1 | `index_refused` (answers equal an off handle's) |
| First OK read of a knob in a generation | Row found or claimed by name, then the index slot (D3) | The knob's row | `ever_read`: the rows equal the set of knobs read OK, over 1,000 knobs |
| A claimer parked at each claim window | Others proceed and converge | The row, once, after any lookup that returned | `claim_windows`: park at `UsageRowWritten`, `UsageNamePublished`, `UsageAddressClaimed`; a second thread's lookup returns within 1 s and its `detent_usage` shows exactly one row |
| Two names with one hash | Two rows | Both | `equal_hashes` (forced hash) |
| Later OK read (hit) | Nothing; a sample adds one to the arm's cell | The cell grows | `arms`: period 1, every arm's cell equals its lookups (all nine, through a definition per arm and each fallback) |
| Countdown at zero | Reload from the stripe's xorshift, [1, 127] | | `cycle`: a 1,024-name cycle at period 64 samples every name within 64 cycles (a fixed reload of 63 samples 16) |
| `DETENT_MISSING` | Stripe total; sampled: the row's `missing` cell if the knob has a row | No new row | `fallbacks`: 64 missing names take no row and the defined knob keeps its row; a knob deleted after being read gains `missing` samples |
| Other fallback inside `lookup` (unavailable, source failure, no memory, I/O) | Stripe total; sampled: `fallback_other` if the knob has a row | | `fallbacks` (each status exact in `statuses`) |
| `NOT_OPENED`, `INVALID_NAME`, `INVALID_ARGUMENT` (`core.cc:792-798`) | Stripe total only | | `fallbacks` |
| `detent_revision` (`core.cc:816-831`) | Nothing | Unchanged | `revision_uncounted` |
| Publish, new generation (`core.cc:536-538`) | New empty index; rows stay | Same rows, counts growing; record's revision and `seen` move | `publish_reorder`: rows survive a publish that reorders and deletes definitions; `claims` grows by the knobs re-read |
| Publish keeping the generation (same id and slot, `core.cc:523-524`) | The index stays | Unchanged | `kept_generation`: `claims` unchanged across it (a rebuilt index claims again) |
| Re-added with the same generation id at a new slot | A new `Generation`, so a new index | Same rows; state current | `readd_same_generation_usage`: `claims` grows |
| Slot reused by another name | New name: new generation; dropped name: state dropped | Records for both | `slot_reused_views` |
| Attach failed (`core.cc:567-573`) | The old generation and its index stay | State failed, the old revision | `entry_failure_view` |
| First attach failed (`core.cc:579-582`) | No generation, no index | No record; `statuses` counts each failed lookup | `first_attach_failure_usage` |
| Dropped (`core.cc:564`) | The generation stays | State dropped; `seen`, `current` are store words | `dropped_view` |
| Refused publish (`core.cc:464-470`) | `refused` + 1 | `refused` | `refused_counted` |
| Stale answer (`core.cc:673-675`) | Counted as OK under its arm | `current − seen` > 0 | `stale_view` (`testing::hold_retry`) |
| View freed (`core.cc:221-232`) | Its generations' indexes unmapped; rows stay | Unchanged | `usage_during_refresh` (ASan, TSan): `detent_usage` beside 1,000 publishes |
| `detent_usage` beside lookups | Relaxed loads | Cells grow independently | `concurrent_usage` (TSan): 16 threads, period 1; after join the cells sum to the lookups |
| Table full | The knob's index slot holds the overflow row; flag set | The overflow row; `overflowed` 1 | `overflow`: `usage_slots` 4, six knobs; the two past it probe at most 1.25 times per hit |
| Index past half full | Flag set; no new claims in that generation | `overflowed` 1 | `index_half_full` (index capacity 4) |
| Fork prepare, parent (`core.cc:251-272`) | Nothing | Unchanged | `fork_child` (parent half) |
| Fork child (`core.cc:275-287`) | Rows, name set, indexes remapped empty; totals and flag cleared; stripes reseeded | Only the child's reads | `fork_child`: the child's rows are its own, the parent's unchanged, and the child's first 1,000 countdown reloads differ from the parent's |
| Close (`core.cc:850-855`) | Everything unmapped | | The suite under ASan |

The claim protocol's races are also checked apart from the core: `tests/detent_reader/usage_claim_model.cc` (run by `usage_claim_model.sh` under TSan) forces each window above and the review's spray.

## Checks

| Criterion (quest `observability-core`) | Check |
| --- | --- |
| A1 `detent_usage` reports each knob's read record and counts | `test_reader_usage` (the lifecycle table) through the C ABI |
| A2 Lookup cost | G1 in `test_reader_usage`; G2 (`reader_bench --usage-gate`) at the quest's head; reader-lookup.md's base/head `reader_bench` comparison against main |
| A3 Suite, TSan, CI | `campaign verify`; the TSan CI job runs `concurrent_usage`, `claim_windows` and `usage_during_refresh` |

The design-round evidence (D2, D10) is in jobs `261109-120856-787b87` (build), `261109-120857-5b7ebc` (smoke), `261109-120857-3fb43d` (screen), `261109-120858-28cd50` (confirm) and the D10 jobs cited there.

## Limits

1. **Silent:** a row says a knob was read, not how often; a knob read once per process shows its row and, with probability 0.98 in one process and 0.85 across 10, no sample.
2. **Silent:** a ramp's split carries a sample count's error: a 50/50 split reads ±16 points at 640 reads, ±4 at 10,000 and ±1.3 at 100,000.
3. **Stated:** past `usage_slots` knobs read OK (or an index past half full, or an index refused at attach), the overflow flag is set for the handle's life. Rows are never evicted, so a long-lived handle reaches it through publish churn (renamed and deleted knobs), not only size.
4. **Silent:** counts carry no revision; a scan interval that straddles a value-changing publish attributes its reads to the new value.
5. **Silent:** a process that never looks up never refreshes, so its view record is old; `current − seen` says so only when someone reads it.
6. Linux writeback of a dirty shared file page write-protects it again, so the first write after each writeback takes a minor fault on the lookup's thread; the page (OB4) belongs on tmpfs (`/run`). D2's soak measured macOS only.
7. A process can forge its page; host and fleet counts are as honest as the processes on the host.
8. **Silent:** one body address per knob rests on dense-pages' disjoint pages (no shared-page DAG, `dense-pages.md:167`), which the lazy reader does not check; a page aliased by a faulty trusted publisher merges two knobs' marks in one generation.
9. More than 64 claimers losing to their own names at once, with no claim between, each lose a row from `usage_slots`.
10. A name that never resolved has no per-name detail: its lookups are in the status totals only (D3).

## Alternatives considered

**Exact per-knob counting** (D2's rows, D10's five mechanisms). Answers "how often" exactly at +18.8 to +47.8 ns per lookup under 8 threads for `knob_rows64`; no exact count passes scattered names at 64 threads.

**A shared `fetch_add` per lookup.** +21.7 to +33.6 ns at 8 threads and no per-knob detail; reader.md removed it for cost.

**Per-thread counters** (`tls`, +0.4 to +14.6 ns). A thread registry per handle, and `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5).

**Rows keyed by a 64-bit hash, claimed in place** (D3 of the S6 design). A claimer writes the name after taking the slot, so another claimer either waits on it or skips it and makes a second row for the same name (`observability-core-design.md` F2).

**Fallback lookups claiming rows.** Names built from input would use up `usage_slots` and push defined knobs into overflow (F3).

**Count in the application only.** Every service would have to wire `detent_usage` before an operator sees anything; kept as one of D7's consumers.

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are nine cells.

## Owner decisions (PLAN.md D47)

The owner took each recommendation on 2026-11-09. The labels stay, because the sections above cite them.

**Q1.** `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. **Q2.** Names per call, no prepared knob handles. **Q3.** `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file rendered by Kronuz/metrics; no listener. **Q4.** Fleet aggregation upstream on relay sessions, after namespace delivery S4-S10. **Q5.** Values, not digests, in the usage document. **Q6.** The page is on whenever the agent's store has `usage/`. **Q7.** Exact read records, sampled counts (`ever_bit`, D10), unless an exact count meets the gate; none did. D48 and D61 keep `ever_sampled`, with D2's in-core gate deciding. D10's mechanism and the bound it implies are before the owner. D158 accepted D6's ABI shape.

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| `observability-core` (OB1-OB3 here: arms, rows and claim index, `detent_usage` with view records) | `src/detent_local/evaluation.hh` and its callers, `src/detent_reader/`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/`, `benchmarks/reader_bench.cc` | The lifecycle table's tests; G1 and G2 (D2); zero allocation and syscall tests (reader.md T1) unchanged | high (hot path) | D10's mechanism ruled (D6: D158) |
| Hook | `core.cc`, `reader.h`, `reader.hh` | A stalled hook never refuses a publish; `hook_off` within G2 against a handle with no hook | medium | observability-core |
| OB4 Usage page, agent collector and export | Kronuz/metrics (Prometheus text renderer), `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz never crashes the agent; a pre-created page makes the handle keep private memory; on Linux a page created by another uid is readable by the agent (VM); dead-process retention; tmpfs fault count 0 over 10 min (VM); the usage document matches a golden schema, caps at 65,536 count rows with `truncated` set, and keeps every read name past the cap; the text file parses as Prometheus exposition format with no knob-name label and is replaced by rename | medium | observability-core, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | observability-core |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred; carries the read names and sampled reads by arm (Q4) | high | Q4; namespace S4-S10 |

`detent get` (reader S7) opens its handle with `DETENT_USAGE_OFF`.
