> Status: DRAFT (2026-11-24) Ready for re-review: T1-T11 of `observability-core-design.md` (round 3, `Detent:780c353f`) folded in, on the owner's D161: `ever_read` is exact per generation, read since the installed generation attached, with no fold across publishes; tooling that wants "since start" unions successive reports (D7, Q4). `tests/detent_reader/usage_claim_model.cc` models the marks, the walk, fork, close and the row claim under TSan. The owner accepted D6's ABI shape (PLAN.md D158); D6 adds `ever_read`, which D161 defines. Earlier rounds: R1-R8, F1-F8 and N1-N8 (the same file), OD-1 to OD-9 and OR1 to OR10 (`observability-design-design.md`), Q1-Q7 (PLAN.md D47), `ever_sampled` (D48, D61). Quests `observability-design` (D37) and `observability-core`.

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, recorded inside the reader core: one bit per definition of the installed generation that the handle read, exact, and one lookup in 64 sampled into per-name arm counters.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:805cd0c6` (main): the paged reader core, one knob type (PLAN.md D65). Measurements in D2 and D10 ran on the S6 core, `Detent:b1d281c`.
- Depends on: reader.md R1 (the caller passes each lookup's default) and R2 (no overrides in the reader), D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork), T4 (heap budget); dense-pages.md (page layout, disjoint pages); verified-page-bitmap.md (the segment table); client-local-source.md D7 (a namespace's entry); the `detent_*` C ABI. Benchmark for D2 and D10's measurements: `docs/design/observability_bench.cc` with `observability-bench.sh`, `observability-summary.py` and `observability-paired.py`, at `Detent:7aed2593^`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering with its caller's default. The reader core counts nothing per lookup, and reader.md removed a shared per-lookup `fetch_add` for cost (`docs/design/reader.md:39`).

The design records in the core, after the answer. Each attached generation gets one anonymous **mark mapping**, one bit per 8 bytes of each of its segments; a lookup that finds a definition tests the bit at the body's offset and sets it if clear, so a knob's later reads in that generation cost one load. The record is exact per generation (PLAN.md D161): a publish starts the next generation's record empty, and the retiring generation's marks leave with it. One lookup in 64 per stripe also adds to a per-name **row** of nine arm counters (the arm that answered, or why the caller's default was returned), bounded by `usage_slots`, kept since open. `detent_usage` reports every knob of the installed generations read and every row, exact per-status totals for the statuses other than OK, and one view record per attached namespace. The hook and the agent's shared page are later quests. Values never enter the hot path: the arm and the generation determine the value, except for a forced switch value (D4).

On the S6 core the shape measured +2.3 to +6.4 ns per warm lookup at 8 threads (D10). The in-core gate (D2) counts events exactly and times on against off as a ratio against D48's ceilings.

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
| Knob read | A mark per definition of the installed generation, exact (D3; PLAN.md D161) | Marks: one mapping of `root.segments` × 16 KiB per attached generation | The union over processes and generations | The union over hosts |
| Reads per name, by arm | 9 sampled counters per row (D2, D4) | At most `usage_slots` rows, then one overflow row | One row per knob and revision | One row per knob |
| Caller's default taken | Exact totals per status; sampled `missing` and `fallback_other` cells in the name's row | 10 status counters | Summed | Summed |
| Generation each process reads | Per attached namespace: revision, sequence seen and current, attach time, state (D5) | One record per attached namespace | One per process and namespace | Revision lag per host |
| Publishes, refused publishes, attaches | The handle's existing counters (`core.cc:194`) | 3 counters | Summed | |

Nothing is keyed by `hash_val`, `switch_val`, the caller's fallback value or the switch rule that matched (D8). The value is what the generation published; the fallback is the caller's argument (R1, R2).

### D2. Where it is counted, and what it costs

The count runs inside `detent_get`'s answer, once the status and arm are known (`core.cc:801-811`), before the pin is released; the stripe is the lookup's own (`core.cc:663-664`). A status the entry point returns before `lookup` (`DETENT_INVALID_ARGUMENT`, `DETENT_INVALID_NAME`, `DETENT_NOT_OPENED`, `core.cc:792-798`) counts in the totals only. `detent_revision` shares `lookup` (`core.cc:825`) but reads no knob, and counts nothing.

**Sampling: one lookup in 64 per stripe.**

1. **Where the countdown lives.** The handle holds 64 cache lines, one per stripe, apart from D5's pin lines. Each holds the countdown word, a 64-bit xorshift state and the stripe's status totals.
2. **Per lookup.** A relaxed load and store decrements the countdown; a lost decrement between two threads on one stripe only moves a sample.
3. **At zero.** The lookup adds 1 to its name's row (D3, Rows) and reloads the countdown from the stripe's xorshift with a value in [1, 127] (mean 64). A fixed period of 64 over a cycle of 64 × k names samples the same k names forever (16 of a 1,024-name cycle); the random reload samples every name of the cycle: in the model, at least 512 of 1,024 names within 64 cycles and all within 1,000, over 100 seeds.
4. **Seeding.** At open, each stripe's state is seeded from one `CLOCK_MONOTONIC` read mixed with the handle's address and the stripe index, and its countdown starts at a value in [1, 127], so a fresh handle samples its first lookup with probability 1/127. A fork child reseeds in the child handler from `getpid()` and the clock (D3, Fork). `testing::seed` fixes the seed, for tests only.

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

1. **Exact per-knob counting costs tens of ns at 8 threads**: +23 to +64 ns here, and every median of `knob_rows8`, `knob_rows64` and `knob_shared` in the three later runs is above +12 ns. Most of it is finding the slot: hash and probe alone (`knob_probe`) cost +15 to +45 ns, so the design hashes a name only on the sampled path.
2. **Exact per-handle counts are cheap** (`stripe_rmw` +2.5 to +13.6 ns, `tls` +0.4 to +14.6 ns, within this method's A/A spread), but say nothing per knob. The core keeps per-handle totals only for statuses other than OK, which are off the answer's path.
3. **The mapped page costs what memory costs.** `knob_rows8_mapped` is within 21 ns of `knob_rows8` either way. The 60-second single-thread soak on scattered integers: in memory p50 1,198.9 ns, worst 64-call batch 22,095 ns, 73 minor faults; mapped p50 1,192.0 ns, worst batch 4,921 ns, 73 minor faults (macOS; Linux is Limit 6).
4. **One thread resolves nothing in this method** (`none` moves 221 to 287 ns between repetitions); D10's interleaved method resolves it to about ±4 ns.

**The gate.** G1 decides at any run level. G2 passes at strict, and anything a strict run cannot pass goes to one exclusive run of the finished feature (PLAN.md D135).

1. **Events (G1), exact.** `test_reader_usage gate_events` counts what lookups do through `testing::Stats` (`marks`, `samples`, `probes`, `rmw`), with the stripes seeded by `testing::seed`. After a warm-up that reads 1,024 knobs once, 1,048,576 further lookups of them make: 0 marks (no bit is clear); 0 allocations and 0 syscalls (reader.md T1's counters); `samples` within 2% of 1,048,576 / 64; one read-modify-write per sample and no other; at most 1.5 name-set probes per sample on average. With `usage_slots` 256 against the same 1,024 names: the same counts, at most 2.5 probes per sample on average (an unsuccessful search at load at most 1/2, for the 768 names past the bound), and their samples in the overflow row. The warm-up itself makes exactly 1,024 marks. Any write or probe the hot path gains fails it.
2. **Time (G2), as a ratio.** `reader_bench --usage-gate` opens three handles over one store: one on and two with `DETENT_USAGE_OFF`. On each thread it alternates them per block of 64 lookups over the same names, rotating the order. Per repetition it takes the median over blocks of on − off and of off′ − off (the A/A), each divided by the median off block. Cases: 1, 8 and 64 threads; hot (one name) and scattered (1,024 names); plain and hashed lookups; scattered with `usage_slots` 256. At least 7 repetitions. A case's threshold is D48's ceiling over that case's own off median in the same run: **5 ns at 1 thread and 10 ns at 8 and 64 threads** (D48, D61). A case passes when the median on-ratio minus the median A/A ratio is at most its threshold. Load from other holds stretches on and off alike within a block, so the ratio cancels it, and dividing by a loaded off median only makes the threshold stricter: a strict pass is a pass. A strict run that does not pass every case, or whose A/A exceeds half a threshold, decides nothing; one exclusive run then decides, with the same rule.

### D3. The read record and the rows

```
generation ── mark mapping: one anonymous mmap, root.segments × 16,384 bytes (1 bit per 8 bytes
              of each 1 MiB segment), made at attach, charged once to the mapping cap
handle ── rows[usage_slots]: hash (8 B) | name length (1 B) | name (200 B) | 9 sample cells (72 B)   288 B
       ── name set: 2^⌈log2(2·usage_slots)⌉ × u32 (0 empty, else row + 1), insert-only
       ── next (bump), spares[64] (0 empty, else row + 1), overflowed flag, overflow row's 9 cells
```

**Marks.** At attach an on handle's `PageTable` maps one anonymous region of `root.segments` × 16,384 bytes, segment `s`'s bitmap at `s` × 16,384; the segment sizes are unknown at attach, so each slot is sized for a full segment. `PageTable::State::resolve` already holds the leaf's `Segment` (`src/detent_local/lazy_page_access.cc:233-240`) and returns its index with the page; `PagedReadState::definition` returns the body's mark word (`s` × 2,048 + (body − segment base) / 512) and bit ((body − segment base) / 8 mod 64) with the body. After the answer, the lookup loads the word (relaxed) and, if the bit is clear, sets it with `fetch_or` (relaxed): one load on a warm hit, one read-modify-write per knob per generation. Two definitions' bodies are at least 12 bytes apart (the smallest definition header, `paged_definition.hh`) on disjoint pages (dense-pages.md, no shared-page DAG), so their 8-byte granules differ. The mapping lives as long as the `PageTable` and is unmapped with it. The mark is idempotent and set by the reader itself, so there is no claim protocol and no bound: a lookup that returns OK has set or seen its bit in the generation that answered, and anything that synchronizes with that return sees it.

**Per generation (D161).** The record of a namespace is the marks of the generation its installed view holds. A publish that installs a new `Generation` starts an empty record; the retiring generation's marks are never reported, whether a lookup still pins it or not, and are unmapped when the freer frees it. The freer does nothing it did not do before. A publish that keeps the `Generation` (same id and slot) keeps its marks. Tooling that wants reads since start unions successive reports (D7).

**Naming the marks.** `detent_usage` pins the installed view as a lookup does (striped pin, `core.cc:666-667`) and, per namespace, walks its generation's trie from the root, entering each page whose verified bit is set, and names each body there whose mark is set. A marked body's page and its ancestors were verified by the lookup that marked it, so the walk verifies nothing, maps nothing and reads only verified pages; it enters pages of branch nodes, which hold no body and so no mark (`paged_read_state.cc:69-81, 103-105`). Cost: one decode per verified page, about 1.8 µs (estimate, one 1,943-byte page): 7 ms at R = 4,096 scattered knobs, 0.29 s at 5M with every knob read.

**Rows.** A sampled lookup in an opened namespace with a valid name, of any status (`OK`, `missing` or another fallback), adds 1 to its name's row; a row is found or claimed by name on the sampled path only. Rows are kept since open, across publishes.

1. **Find or claim by name.** Hash the name, probe the name set from `hash & mask`. A nonzero slot (acquire) names a published row: equal hash and equal name bytes is a match, anything else probes on. To claim at an empty slot, take a private row (a spare first, else `next`, once per claim), write its hash, length and name, then compare-and-swap the slot from 0 to the row (release on success, acquire on failure). A failed compare-and-swap re-examines the same slot: a match returns that row and gives the private row back to the spares; a different name probes on.
2. **No duplicates, no waiting.** A name-set slot goes from empty to published once and never back, and every claimer examines its probe sequence in order until it matches or publishes, so two claimers of one name meet at the same first empty slot and one matches the other's row. Nothing is published before its name is complete, and no loop waits on another thread: a parked claimer delays only itself. Equal hashes never merge two names.
3. **Spares.** A claimer that loses to its own name returns its private row into the first empty of 64 spare cells; the next claim takes it. More than 64 such losers at once, with no claim between, lose a row each (Limit 9).
4. **Full.** A claim with no spare and `next` at `usage_slots` re-examines the slot it saw empty: a row published there since is taken as in item 1. Only if the slot is still empty does it set `overflowed` (release, test before set) and count in the overflow row. The flag says that some counts are not per name; the read record does not depend on it.

At the default 4,096: rows 1.18 MB and name set 32 KB, anonymous `mmap`s. `usage_slots` is at most `DETENT_MAXIMUM_USAGE_SLOTS` (8,388,608); larger values fail the open with `DETENT_INVALID_ARGUMENT`.

**Fork.** The child handler (`core.cc:275-287`) already visits every generation of every ring view, installed or retired, pinned or not, and calls `PageTable::child_after_fork`; that call maps fresh zeroed anonymous memory over the generation's mark mapping (`mmap` with `MAP_FIXED`, no copy of the parent's pages). The handler also remaps the rows and name set the same way, zeroes `next`, the spares, the flag and the stripe totals, and reseeds each stripe. The child's record is its own reads.

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

The refresh winner reads `CLOCK_MONOTONIC` with `clock_gettime` when it attaches a generation (not `now_ns`, which is `mach_absolute_time` on macOS, `core.cc:71-84`), so on one host every process's attach time is on the agent's clock. Each entry also records its state:

| Entry state | Set at | `revision` | `seen`, `current` |
| --- | --- | --- | --- |
| current | attach or publish | the answering generation's | the entry's slot: the sequence attached at, and the sequence now; `current − seen` is the publishes not yet looked up |
| failed | a newer generation failed to attach (`core.cc:567-573`) | the old, answering generation's | the slot, with `seen` at the failed sequence, so `current − seen` is 0 though the answer is old; the state says so |
| dropped | the table no longer lists it (`core.cc:564`) | the last generation's | the store word, which the entry then watches |

### D6. The application's view (Q1)

The owner accepted this shape (PLAN.md D158); `switch_forced` keeps the cell count at nine, in the order below. `ever_read` is new since D158 and defined by D161: A1 asks for each knob's read bit, and a name's row alone cannot give it (a name sampled only as `missing` has a row and was never read OK).

```c
typedef enum detent_arm {
  DETENT_ARM_PUBLISHED, DETENT_ARM_NO_HASH, DETENT_ARM_RAMP_IN, DETENT_ARM_RAMP_OUT,
  DETENT_ARM_SWITCH_FORCED, DETENT_ARM_SWITCH_IN, DETENT_ARM_SWITCH_OUT,
  DETENT_ARM_MISSING, DETENT_ARM_FALLBACK_OTHER, DETENT_ARM_COUNT
} detent_arm;
typedef enum { DETENT_VIEW_CURRENT, DETENT_VIEW_FAILED, DETENT_VIEW_DROPPED } detent_view_state;
typedef struct { size_t size; detent_str name;          /* data == NULL: the overflow row */
                 uint32_t ever_read;                     /* 1: read OK in the installed generation */
                 uint64_t samples[DETENT_ARM_COUNT]; } detent_knob_usage;
typedef struct { size_t size; detent_str name_space; uint64_t revision, seen, current;
                 uint64_t attached_ns;                   /* CLOCK_MONOTONIC */
                 uint32_t state; } detent_view_usage;
typedef struct { size_t size; uint32_t period;          /* 64; 0 on an off handle */
                 uint32_t overflowed;                    /* 1: some samples are in the overflow row */
                 uint64_t statuses[16];                  /* exact lookups by detent_status; [DETENT_OK] is 0 */
                 uint64_t publishes, refused, attached; } detent_usage_totals;
detent_status detent_usage(detent_reader *, detent_usage_totals *,
                           void (*knob)(void *, const detent_knob_usage *),
                           void (*view)(void *, const detent_view_usage *), void *context);
/* detent_open_options grows: uint32_t usage_slots;  0: 4,096; DETENT_USAGE_OFF: no usage */
#define DETENT_USAGE_OFF UINT32_MAX
#define DETENT_MAXIMUM_USAGE_SLOTS 8388608u
```

1. **Knobs.** One record per name that has a row or is marked in an installed generation: `ever_read` is 1 exactly when the name was read OK since its namespace's installed generation attached (or, in a fork child, since the fork), and 0 for a row's name not read OK in that generation. `samples` × `period` estimates reads by arm since open, across publishes; it is zero for a knob read but never sampled. The overflow row is reported once `overflowed` is set. A record's cells are independent relaxed counters, not a snapshot of one instant, and say nothing about which revision answered.
2. **Absence.** A defined knob of the installed generation with no record, or with `ever_read` 0, was not read OK since that generation attached. This holds at any `usage_slots`; `overflowed` affects only counts.
3. **Statuses.** `statuses[s]` counts exactly the lookups that returned `s`, for every `s` but `DETENT_OK`, whose cell is 0: an exact OK count is the shared write per lookup that G1 excludes. OK reads are estimated from the rows' arm cells × `period`.
4. **Collection.** `detent_usage` never takes `refreshing`, never refreshes and never waits on a refresh: it pins the installed view as a lookup does, walks each namespace's verified pages (D3, Naming the marks) into memory it allocates, copies the rows, view records and totals, unpins, then calls the callbacks. A refresher parked inside `refreshing` does not delay it, and it reports the view installed when it pinned. A publish during the walk proceeds; while the walk's pin holds a ring slot, a fourth publish in that time is refused and retried with backoff, as behind a parked lookup (Limit 10). If its allocation fails it returns `DETENT_NO_MEMORY` and calls no callback.
5. **Callbacks.** They run on the calling thread, with no lock or pin held. They may look up knobs and call `detent_usage`; they must not call `detent_close`, and the strings they receive live only during the call. Either callback may be NULL.
6. **Off.** A handle opened with `DETENT_USAGE_OFF` keeps no marks, rows, countdown or totals: `period` and `overflowed` are 0, the totals are zero but for `publishes`, `refused` and `attached`, no knob is reported, and view records are reported.
7. **Sizes.** `detent_open_options` keeps its rule (`core.cc:740-746`): the library reads only fields below the caller's `size`, and `usage_slots` past it is 0, the default. `detent_usage_totals` is written by the library: `totals` may be NULL; otherwise the caller sets `size`, a `size` below the first shipped `sizeof(detent_usage_totals)` returns `DETENT_INVALID_ARGUMENT` before anything is written or called, and the library writes only bytes below min(`size`, its own `sizeof`), then sets `size` to that count. Each `detent_knob_usage` and `detent_view_usage` it passes has `size` set to the library's `sizeof`; a caller reads only fields that end at or below that `size`. Later versions only append fields.
8. **ABI.** `usage_slots = 0` means the default, as `mapping_bytes = 0` does (`core.cc:745-746`), so a caller that zero-fills the grown struct, as `Reader` does (`reader.hh:43`), keeps usage on; off is explicit. `DETENT_ABI_VERSION` stays 1: the change adds a function and grows size-tagged structs, and no library has shipped (`reader.hh:37` compares equality). `DETENT_MINIMUM_MAPPING_BYTES` does not change (D10).
9. **C++.** `Reader::usage()` returns the totals with vectors of plain structs. Python and Rust wrap it as they wrap lookups (client-local-source.md D1; OB5).

The hook (Q1) is fixed at open, runs after the pin is released, and is a later quest.

### D7. Leaving the process and the host (OB4)

```
app process: marks, rows ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3, D9)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the rows, name set, totals and, per attached generation, its revision and its mark mapping live in it. The agent holds the same generations, so it names marks by walking its own copy. The directory is the agent's: owner `publisher_uid`, group the agent's group, mode 3733 (setgid and sticky), so others create files but cannot list, nobody deletes another's file, and on Linux each file takes the agent's group. If the create fails, the handle keeps private memory and reports it in its totals.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own reads (Limit 7).
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** The child's handler maps fresh anonymous memory over the page's range (D3, Fork), and its next refresh creates its own page.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (`src/detent_relay/host.cc:59-127`): per knob and revision the estimated reads by arm (samples × period) and the derived value (D4); the read set, the union of each process's per-generation reports since it started, outside the capped count rows (D8); a generation replaced between two scans loses the reads it answered after the earlier one (Limit 12); per process its namespaces, revisions, lag and age. Label-free totals (lookups by status, refused publishes, processes) are written as a Prometheus text file (D9).
6. **Fleet.** The union of read sets across hosts answers "which knobs are dead" exactly; sampled reads answer "how often", and only at fleet scale for rare knobs: 1,000 processes reading a knob 10 times each yield about 156 samples. It needs an upstream message from agent to relay to authority, a wire change (Q4).

### D8. Privacy and limits

| Data | Exported as | Why |
| --- | --- | --- |
| `hash_val`, `switch_val` | Never stored | User or tenant identifiers (`include/detent/errors.hh:20`) |
| Switch rule that matched | Never stored; only the arm | The rule's key equals a caller's switch value |
| Caller's fallback value | Never stored; only that it was returned | Caller-supplied, unbounded (R1) |
| Knob name | In the usage document and `detent_usage`, never a metric label | Per-knob series across 250k agents do not fit a time-series store |
| Resolved value | Derived from the published definition (Q5), except `switch_forced` (D4) | Already on every agent's disk |

Unbounded sets are capped where they enter: `usage_slots` rows per handle, then the overflow row; names at most 200 bytes (`core.cc:325-333`); the host document at most 65,536 count rows, ordered by reads, with a `truncated` count; fleet rows at most the store's knobs. The read set is not in those capped rows, because ordering by reads would drop the rarely read knobs first, the ones it exists for: it is at most the store's knobs.

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

**The paged core has no position**, so the mark sits on the body's location: a bit per 8 bytes of each segment (D3, Marks). The lead ruled it over a claim index keyed by the body's address (Alternatives), because it is exact for every definition at any `usage_slots`. A warm hit under either is one load after the walk; the bitmap's needs no multiply and no probe.

**Hot-path cost against G2.** After the answer, the mark is the word's address (a subtract, a shift and an add from the segment index and base `resolve` already holds), one relaxed load and a bit test; the first read of a knob in a generation adds one `fetch_or`, and after a publish each knob read pays it once again. D10's `ever_bit` is that load and test-before-set: +2.6 / +3.1 ns at 8 threads (worst +4.6 at 64), and with the countdown (`ever_sampled`) +3.9 / +6.4 at 8 and +2.7 to +3.3 / +6.2 to +6.4 in the three confirming runs, under G2's 10 ns at 8 threads by 3.6 ns at the worst. The address arithmetic is not in those runs; G2 (D2) decides in the core.

Memory per handle with one namespace attached (63 bytes of segment per knob, from the 5M store's 315,759,038 bytes, `dense-pages.md:251`), reading R knobs scattered. Both keep the default rows (1.18 MB) and name set (32 KB) for counts; the claim index needs `usage_slots` = max(4,096, R) to be exact:

| Store | Segments | Mark mapping (charged at attach) | Bitmaps, any R | Claim index, R ≤ 4,096 | Claim index, R = every knob |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100K | 7 | 112 KiB | 1.33 MB | 1.34 MB | 34.0 MB |
| 1M | 61 | 976 KiB | 2.21 MB | 1.34 MB | 330 MB |
| 5M | 302 | 4.72 MiB | 6.16 MB | 1.34 MB | 1.78 GB |

The mark mapping is anonymous memory: it is charged whole at attach, but only the pages that hold a set bit are resident. Each further attached namespace adds its own; a retired generation keeps its mapping until the freer frees it, as it keeps its segments. reader.md T4 bounds the core heap at 1.5 MiB plus the per-segment bitmaps, reported: the mark mapping is per-segment bitmaps under that clause, charged to the cap, and the rows (1.18 MB at the default) are core heap, which the implementation reports against 1.5 MiB with the current core's heap measured beside them.

**Surface in `src/detent_local`**, which the quest's file set gains before any edit (the journal names each file first):

| File | Change |
| --- | --- |
| `paged_page_access.hh` | `PagedPageAccess` gains `resolve_marked`, returning the page with its segment index; its default returns none, so `prepared_reader.cc` and the tests that implement or call `resolve` (`test_lazy_page_access`, `test_lazy_page_bitmap`, `test_dense_reads`, `test_dense_fullbuild`) do not change |
| `lazy_page_access.hh`, `lazy_page_access.cc` | `PageTable` takes a marks switch and, on, maps and charges the mark mapping at construction (`:101-109`) and unmaps it in `~State` (`:110`); `resolve_marked` beside `resolve` (`:233`); `child_after_fork` maps zeroed memory over it (`:292-295`); `mark_bytes()` and `verified(page)` for the walk and `testing::Stats` |
| `paged_read_state.hh`, `paged_read_state.cc` | `definition` returns the body's mark word and bit; `for_each_marked`, the walk of verified pages that yields marked keys, for `detent_usage` |
| `evaluation.hh` | `evaluate` returns `{value, arm}` (D4); its callers `view.cc`, `tests/detent_local/test_evaluation.cc`, `tests/detent/test_one_knob_differential.cc` take `.value` |

**Mapping cap.** One mapping per generation, charged once at attach to the handle's mapping account: `rounded_mapping_bytes(root.segments × 16,384)` (`generation_mapping.hh`), page-rounded once for the whole generation, not per segment. That is 16 KiB for a one-pack store on 4 KiB and 16 KiB pages and 64 KiB on 64 KiB pages; 112 KiB, 976 KiB and 4.72 MiB at 100K, 1M and 5M; 1/64 of the store's segment bytes at most. Uncharged, the marks would sit outside every cap, which the cap exists to prevent. An off handle maps none. The answer of a lookup depends on usage only through this charge: at a cap that holds a generation with marks the on and off handles answer alike, and at a cap between the two needs the on handle's attach fails with `DETENT_NO_MEMORY` where the off handle's succeeds (Limit 11).

**For the owner: `DETENT_MINIMUM_MAPPING_BYTES` does not change.** It stays 1,055,136, the open-time floor reader.md:130 defines ("open attaches nothing"), with `core.cc:59-60`'s `static_assert` as is; the mark mapping is charged at attach beside the page-table allocations, which the floor already leaves out.

## Lifecycle

States per handle: **off** (`DETENT_USAGE_OFF`) or **on**. Per generation: **attached** (in the installed view: its marks are the namespace's record), **retired, pinned** (replaced; a view or a lookup's pin still holds it: its marks are kept, never reported), and **freed** (its mark mapping unmapped with its page table). Per entry: current, failed or dropped (D5). Rows live from open (or the fork) to close. Each test is in `tests/detent_reader/test_reader_usage.cc` and fails without the property its row names; seams are in `src/detent_reader/testing.hh`: `seed`, forced period, forced equal hashes, a failing-allocation seam, the windows `UsageSawEmpty`, `UsageRowWritten`, `UsageNamePublished`, `UsageMarked`, the core's `Refreshing` and `Pinned`, and `Stats::marks`, `samples`, `probes`, `rmw`, `mark_bytes`, `mark_mappings` (live, process-wide).

| Event (code) | Usage change | `detent_usage` then | Test |
| --- | --- | --- | --- |
| Open, on (`core.cc:735-788`) | Rows, name set, stripe lines mapped; countdowns seeded | No records; period 64; totals 0 | `fresh_handle`: over 10,000 fresh handles the first lookup is sampled 30 to 160 times (expected 78.7; a fixed start of 1 gives 10,000, of 64 gives 0) |
| Open, off | Nothing mapped | Period 0, no records; views reported | `off_handle`: lookups leave every usage stat and `mark_bytes` at 0; views present |
| Open, `usage_slots` past the maximum | `DETENT_INVALID_ARGUMENT` | | `open_bounds` (the maximum opens, one past fails; `mapping_bytes` at 1,055,136 opens on and off) |
| Attach (first, or a publish installing a new `Generation`; `core.cc:536-538, 575-584`) | The generation's mark mapping made, zeroed, charged | Its namespace has no marked record; a view record with `seen == current` and `attached_ns` between the test's clock reads | `mark_charge`: on − off `mapped_bytes` after attach equals `rounded_mapping_bytes(root.segments × 16,384)` exactly, for the 100K store (7 segments) and a one-pack store; `view_records` |
| Attach at a cap between the off and on needs | On: `DETENT_NO_MEMORY`; off: attaches | | `usage_answers` (the stated difference) |
| First OK read of a knob in the generation | One `fetch_or` | `ever_read` 1 | `ever_read` on the 100K store, whose definitions sit two pages below the root: over 1,000 knobs read, the records with `ever_read` equal the knobs read OK; `marks` equals 1,000 |
| Later OK read in the generation | One load | Unchanged | `gate_events` (G1: 0 marks after warm-up) |
| Any lookup, on against off | Marks and rows only | | `usage_answers`: one script of lookups and publishes, with a corrupt page no lookup reads, gives the same statuses and values on an on and an off handle at a cap that holds both; `detent_usage` never verifies a page (`verifications` unchanged by it) |
| Sampled lookup, any status in an opened namespace | Row found or claimed by name; one cell | The name's cell grows | `arms`: period 1, every arm's cell equals its lookups (all nine, through a definition per arm and each fallback) |
| A claimer parked at `UsageRowWritten` or `UsageNamePublished` | Others proceed and converge | One row | `claim_windows`: a second sampled lookup of the name returns within 1 s; one row after both |
| The last row taken while a claimer that saw the slot empty finds the table full | It re-examines the slot and takes the row | One row, `overflowed` 0 | `claim_boundary` (park at `UsageSawEmpty`, `usage_slots` 1) |
| Two names with one hash | Two rows | Both | `equal_hashes` (forced hash) |
| Countdown at zero | Reload from the stripe's xorshift, [1, 127] | | `cycle`: seeded; a 1,024-name cycle samples at least 512 names in 64 cycles and all within 1,000 (a fixed period of 64 samples 16, forever) |
| `DETENT_MISSING` or another fallback inside `lookup` | Stripe total; sampled: the row's `missing` or `fallback_other` cell | A record with `ever_read` 0 for a name never read OK | `fallbacks`: each status exact in `statuses`, `statuses[DETENT_OK]` 0 after OK lookups; a missing name's record has `ever_read` 0 |
| `NOT_OPENED`, `INVALID_NAME`, `INVALID_ARGUMENT` (`core.cc:792-798`) | Stripe total only | | `fallbacks` |
| `detent_revision` (`core.cc:816-831`) | Nothing | Unchanged | `revision_uncounted` |
| Table full | Samples in the overflow row; flag set | The overflow record; every knob read still `ever_read` 1 | `overflow`: `usage_slots` 4, six knobs read and sampled: six records `ever_read` 1, four with counts, two knobs' samples in the overflow row |
| Publish installing a new generation, reordering and deleting | The old generation retires; the new record starts empty; rows kept | No record `ever_read` 1 until read again; rows unchanged | `publish_resets`: three knobs read, a publish: zero `ever_read`, three rows; one read again: one `ever_read`. Fails with a fold or carried names |
| Publish keeping the generation (same id and slot, `core.cc:523-524`) | Nothing retires; marks kept | Unchanged | `kept_generation` |
| Re-added with the same generation id at a new slot | A new `Generation`, its record empty | No `ever_read` until read | `readd_same_generation_usage` |
| Slot reused by another name; namespace dropped; attach failed (`core.cc:564, 567-573`) | The old generation stays attached, its marks kept | Records unchanged; view states dropped or failed | `slot_reused_views`, `dropped_view`, `entry_failure_view` |
| First attach failed (`core.cc:579-582`) | No generation, no mark mapping | No view record; `statuses` counts each failed lookup | `first_attach_failure_usage` |
| Refused publish (`core.cc:464-470`) | `refused` + 1 | `refused` | `refused_counted` |
| Stale answer (`core.cc:673-675`) | Marks the installed (answering) generation | `ever_read` 1; `current − seen` > 0 | `stale_view` (`testing::hold_retry`) |
| A lookup pinned on a retired generation marks it (`UsageMarked`) | The mark stays in the retired generation | Not reported, before or after the unpin | `retired_marks`: parked after the publish; its knob has `ever_read` 0 throughout |
| Freer frees a retired generation (`core.cc:221-232`) | Its mark mapping unmapped with its page table; nothing else | Unchanged | `retired_marks`: `mark_mappings` drops by one; `Stats` shows no lookup and no allocation in the freer |
| `detent_usage` beside lookups and publishes | Pins the installed view; walks; unpins before callbacks | Cells grow independently | `concurrent_usage` (TSan, period 1, 16 threads, 50 publishes: cells sum to lookups after join; each report's `ever_read` set is a subset of the knobs read in the generation it pinned) |
| `detent_usage` while a refresher holds `refreshing` | Never waits, never refreshes | The installed view | `usage_during_refresh`: a refresher parked at `Refreshing`; `detent_usage` returns within 1 s with the old generation's records |
| `detent_usage`'s allocation fails | `DETENT_NO_MEMORY` | No callback | `usage_no_memory` (failing-allocation seam) |
| A callback looks up knobs and calls `detent_usage` | No lock or pin held | The inner call reports | `callback_reentry` |
| `detent_usage` with a short or long totals struct | Writes below min(`size`, library's) only; sets `size` | Records' `size` is the library's | `usage_abi`: a totals struct ending after `statuses` with canary bytes behind it, and one larger than the library's: no byte past either is written; a size below the first version returns `DETENT_INVALID_ARGUMENT` with nothing written |
| Fork prepare, parent (`core.cc:251-272`) | Nothing | Unchanged | `fork_child` (parent half) |
| Fork child (`core.cc:275-287`) | Every ring generation's mark mapping remapped zeroed, rows and name set remapped empty, totals and flag cleared, stripes reseeded | Only the child's reads | `fork_child`: forked with a reader parked at `Pinned` on a retired generation that holds marks, the child reports none of the parent's reads, then its own |
| Close (`core.cc:850-855`) | Every ring view freed, every mark mapping unmapped | | `close_retired`: close with a retired, unfreed generation holding marks; `mark_mappings` returns to its value before open; ASan and LSan clean |

`tests/detent_reader/usage_claim_model.cc` (run by `usage_claim_model.sh` under TSan) checks the per-generation record, the walk, fork, close, `detent_usage`'s no-wait and the row claim apart from the core.

## Checks

| Criterion (quest `observability-core`) | Check |
| --- | --- |
| A1 `detent_usage` reports each knob's read bit and counts | `test_reader_usage` (the lifecycle table) through the C ABI |
| A2 Lookup cost | G1 (`gate_events`) in the suite; G2 (`reader_bench --usage-gate`) strict at the quest's head, exclusive when strict does not pass; reader-lookup.md's base/head `reader_bench` against main; `detent_usage`'s walk timed at 100K and 1M, R = 4,096 and every knob (light), against D3's estimate |
| A3 Suite, TSan, CI | `campaign verify`; the TSan CI job runs `concurrent_usage`, `claim_windows` and `usage_during_refresh` |

The design-round evidence (D2, D10) is in jobs `261109-120856-787b87` (build), `261109-120857-5b7ebc` (smoke), `261109-120857-3fb43d` (screen), `261109-120858-28cd50` (confirm) and the D10 jobs cited there.

## Limits

1. **Silent:** a record says a knob was read, not how often; a knob read once per process shows `ever_read` and, with probability 0.98 in one process and 0.85 across 10, no sample.
2. **Silent:** a ramp's split carries a sample count's error: a 50/50 split reads ±16 points at 640 reads, ±4 at 10,000 and ±1.3 at 100,000.
3. **Stated:** past `usage_slots` names sampled, counts go to the overflow row and `overflowed` stays 1 for the handle's life. Rows are never evicted, so names looked up and since deleted, or misspelled, use them up; the read record is unaffected.
4. **Silent:** counts carry no revision; a scan interval that straddles a value-changing publish attributes its reads to the new value.
5. **Silent:** a process that never looks up never refreshes, so its view record is old; `current − seen` says so only when someone reads it.
6. Linux writeback of a dirty shared file page write-protects it again, so the first write after each writeback takes a minor fault on the lookup's thread; the page (OB4) belongs on tmpfs (`/run`). D2's soak measured macOS only.
7. A process can forge its page; host and fleet counts are as honest as the processes on the host.
8. **Silent:** one granule per definition rests on dense-pages' disjoint pages (no shared-page DAG, `dense-pages.md:167`), which the lazy reader does not check; a page aliased by a faulty trusted publisher merges two knobs' marks in one generation.
9. More than 64 claimers losing to their own names at once, with no claim between, each lose a row from `usage_slots`.
10. `detent_usage` holds one pin for its walk, up to 0.29 s at 5M with every knob read (estimate); more than three publishes in that time refuse the next until it unpins, as behind a parked lookup.
11. **Stated:** an on handle needs `rounded_mapping_bytes(root.segments × 16,384)` more of the cap per attached generation, 1/64 of its segment bytes at most; at a cap between the two needs, it answers `DETENT_NO_MEMORY` where an off handle would not.
12. **Stated:** `ever_read` covers the installed generation only (D161): reads answered just before a publish, or by a retired generation a lookup still pins, are not in the next report. "Read since start" is the union of reports taken at least once per generation (D7).

## Alternatives considered

**A fold across publishes** (this design's round 3). The freer named a retiring generation's marks and marked them in its successor, or carried a deleted knob's name, so the record was exact since open. Not taken (PLAN.md D161): it cost up to 3.8 to 7.0 s per publish at 5M on the thread that found the publish, kept retired generations under the cap, verified pages the application never read, and allocated in a `noexcept` freer (round-3 findings T1, T2 and T10); its walk and fork rules were wrong as specified (T3, T4).

**A claim index per generation** (this design's round 2). Keyed by the body's address with rows by name; core.cc only, no walk, and smaller at the default (1.34 MB against up to 6.2 MB at 5M). Exact only up to `usage_slots` knobs read, and exact for every knob only at 26 to 288 times the bitmaps' memory (D10); the lead ruled for the bitmaps.

**Exact per-knob counting** (D2's rows, D10's five mechanisms). Answers "how often" exactly at +18.8 to +47.8 ns per lookup under 8 threads for `knob_rows64`; no exact count passes scattered names at 64 threads.

**A shared `fetch_add` per lookup.** +21.7 to +33.6 ns at 8 threads and no per-knob detail; reader.md removed it for cost.

**Per-thread counters** (`tls`, +0.4 to +14.6 ns). A thread registry per handle, and `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5).

**Rows keyed by a 64-bit hash, claimed in place.** A claimer writes the name after taking the slot, so another either waits on it or makes a second row for the name (`observability-core-design.md` F2).

**Count in the application only.** Every service would have to wire `detent_usage` before an operator sees anything; kept as one of D7's consumers.

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are nine cells.

## Owner decisions (PLAN.md D47)

The owner took each recommendation on 2026-11-09. The labels stay, because the sections above cite them.

**Q1.** `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. **Q2.** Names per call, no prepared knob handles. **Q3.** `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file rendered by Kronuz/metrics; no listener. **Q4.** Fleet aggregation upstream on relay sessions, after namespace delivery S4-S10. **Q5.** Values, not digests, in the usage document. **Q6.** The page is on whenever the agent's store has `usage/`. **Q7.** Exact read records, sampled counts (`ever_bit`, D10), unless an exact count meets the gate; none did. D48 and D61 keep `ever_sampled`, with D2's in-core gate deciding. D158 accepted D6's ABI shape. D161 made the read record exact per generation, with no fold.

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| `observability-core` (OB1-OB3 here: arms, marks, rows, `detent_usage` with view records) | D10's `src/detent_local` surface; `src/detent_reader/`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/`, `benchmarks/reader_bench.cc` | The lifecycle table's tests; G1 and G2 (D2); the walk's timing (Checks); zero allocation and syscall tests (reader.md T1) unchanged | high (hot path) | `first-read-1ms` landed (it holds the reader hot path) |
| Hook | `core.cc`, `reader.h`, `reader.hh` | A stalled hook never refuses a publish; `hook_off` within G2 against a handle with no hook | medium | observability-core |
| OB4 Usage page, agent collector and export | Kronuz/metrics (Prometheus text renderer), `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz never crashes the agent; a pre-created page makes the handle keep private memory; on Linux a page created by another uid is readable by the agent (VM); dead-process retention; tmpfs fault count 0 over 10 min (VM); the usage document matches a golden schema, caps at 65,536 count rows with `truncated` set, and keeps every read knob past the cap; the agent's read set for a process is the union of its per-generation reports; the text file parses as Prometheus exposition format with no knob-name label and is replaced by rename | medium | observability-core, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | observability-core |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred; carries the read sets and sampled reads by arm (Q4) | high | Q4; namespace S4-S10 |

`detent get` (reader S7) opens its handle with `DETENT_USAGE_OFF`.
