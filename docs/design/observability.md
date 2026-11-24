> Status: DRAFT (2026-11-24) Ready for whole-design re-review: R1-R8 of the re-review in `observability-core-design.md` (`Detent:e4218ae2`) folded in, on the lead's ruling that the read record is segment bitmaps (D10), which keeps Q7's exactness for every definition. `tests/detent_reader/usage_claim_model.cc` models the marks, the fold and the row claim under TSan. The owner accepted D6's ABI shape (PLAN.md D158); D6 adds `ever_read` to it and D10 moves `DETENT_MINIMUM_MAPPING_BYTES`, both for the owner. Earlier rounds: F1-F8 and N1-N8 (the same file), OD-1 to OD-9 and OR1 to OR10 (`observability-design-design.md`), Q1-Q7 (PLAN.md D47), `ever_sampled` (D48, D61). Quests `observability-design` (D37) and `observability-core`.

# Knob observability

Which knobs each service reads, what they resolve to, when they fall back, and how fresh each process's view is, recorded inside the reader core: one bit per definition that the handle read, exact, and one lookup in 64 sampled into per-name arm counters.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-09.
- Base: `Detent:805cd0c6` (main): the paged reader core, one knob type (PLAN.md D65). Measurements in D2 and D10 ran on the S6 core, `Detent:b1d281c`.
- Depends on: reader.md R1 (the caller passes each lookup's default) and R2 (no overrides in the reader), D3 (no lock, allocation or syscall on a warm lookup), D4 (the `change` file), D5 (striped pins), D10 (fork), T4 (heap budget); dense-pages.md (page layout, disjoint pages); verified-page-bitmap.md (the segment table); client-local-source.md D7 (a namespace's entry); the `detent_*` C ABI. Benchmark for D2 and D10's measurements: `docs/design/observability_bench.cc` with `observability-bench.sh`, `observability-summary.py` and `observability-paired.py`, at `Detent:7aed2593^`.

## Abstract

An operator today cannot tell which knobs a service reads, which arm of a ramp its callers land in, or whether a process is answering with its caller's default. The reader core counts nothing per lookup, and reader.md removed a shared per-lookup `fetch_add` for cost (`docs/design/reader.md:39`).

The design records in the core, after the answer. Each mapped segment of an attached generation gets a **mark bitmap**, one bit per 8 bytes, beside its verified-page bitmap; a lookup that finds a definition tests the bit at the body's offset and sets it if clear, so a knob's later reads cost one load. When a publish retires a generation, a **fold** names its marked definitions and marks them in the generation that replaced it, or carries a deleted knob's name, so the record is exact for every definition since open. One lookup in 64 per stripe also adds to a per-name **row** of nine arm counters (the arm that answered, or why the caller's default was returned), bounded by `usage_slots`. `detent_usage` reports every knob read, its sampled counts, exact per-status totals and one view record per attached namespace. The hook and the agent's shared page are later quests. Values never enter the hot path: the arm and the generation determine the value, except for a forced switch value (D4).

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
| Knob read | A mark per definition, exact; folded across publishes; a deleted knob's name carried (D3) | Marks: mapped bytes / 64; carried names: one per deleted knob read | The union over processes | The union over hosts |
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

### D3. The read record, the rows and the fold

```
generation ── per mapped segment: mark bitmap, 1 bit per 8 bytes (16 KB per 1 MiB segment),
              allocated with the segment and its verified-page bitmap, charged to the mapping cap
handle ── rows[usage_slots]: hash (8 B) | name length (1 B) | name (200 B) | 9 sample cells (72 B)   288 B
       ── name set: 2^⌈log2(2·usage_slots)⌉ × u32 (0 empty, else row + 1), insert-only
       ── next (bump), spares[64] (0 empty, else row + 1), overflowed flag, overflow row's 9 cells
       ── carried names (heap): knobs read whose definition a publish deleted
       ── pending fold: at most one retired generation per namespace, part folded
```

**Marks.** `PageTable::State::resolve` already holds the leaf's `Segment` (`src/detent_local/lazy_page_access.cc:237`). It returns the segment's mark bitmap and the page's offset with the page, and `PagedReadState::definition` returns the body's mark word and bit with the body. After the answer, the lookup loads the word (relaxed) and, if the bit is clear, sets it with `fetch_or` (relaxed): one load on a warm hit, one read-modify-write per knob per generation. Two definitions' bodies are at least 12 bytes apart (the smallest definition header, `paged_definition.hh`) on disjoint pages (dense-pages.md, no shared-page DAG), so their 8-byte granules differ. The segment mapping and its bitmap live as long as the `PageTable` (`lazy_page_access.cc:110-126`). The mark is idempotent and set by the reader itself, so there is no claim protocol, no visibility window and no bound: a lookup that returns OK has set or seen its bit, and anything that synchronizes with that return sees it.

**Rows.** A sampled lookup in an opened namespace with a valid name, of any status (`OK`, `missing` or another fallback), adds 1 to its name's row; a row is found or claimed by name on the sampled path only.

1. **Find or claim by name.** Hash the name, probe the name set from `hash & mask`. A nonzero slot (acquire) names a published row: equal hash and equal name bytes is a match, anything else probes on. To claim at an empty slot, take a private row (a spare first, else `next`, once per claim), write its hash, length and name, then compare-and-swap the slot from 0 to the row (release on success, acquire on failure). A failed compare-and-swap re-examines the same slot: a match returns that row and gives the private row back to the spares; a different name probes on.
2. **No duplicates, no waiting.** A name-set slot goes from empty to published once and never back, and every claimer examines its probe sequence in order until it matches or publishes, so two claimers of one name meet at the same first empty slot and one matches the other's row. Nothing is published before its name is complete, and no loop waits on another thread: a parked claimer delays only itself. Equal hashes never merge two names.
3. **Spares.** A claimer that loses to its own name returns its private row into the first empty of 64 spare cells; the next claim takes it. More than 64 such losers at once, with no claim between, lose a row each (Limit 9).
4. **Full.** A claim with no spare and `next` at `usage_slots` re-examines the slot it saw empty: a row published there since is taken as in item 1. Only if the slot is still empty does it set `overflowed` (release, test before set) and count in the overflow row. The flag says that some counts are not per name; the read record does not depend on it.

At the default 4,096: rows 1.18 MB and name set 32 KB, anonymous `mmap`s so a fork child replaces them without a copy. `usage_slots` is at most `DETENT_MAXIMUM_USAGE_SLOTS` (8,388,608); larger values fail the open with `DETENT_INVALID_ARGUMENT`.

**The fold.** A generation's marks are final once no view holds it: the freer (`free_retired`, `core.cc:221-232`) frees a generation only when its views count reaches zero and no pin remains on its slot. Before freeing it, the freer folds it into the generation the installed view holds for the same namespace (its successor):

1. **Name the marks.** Walk the retiring generation's trie from its root, entering only pages whose mark range has a set bit; every such page and its ancestors were verified by the lookups that marked them, so the walk verifies nothing. Each set bit names a key.
2. **Carry each key.** Look the key up in the successor (`PagedReadState::definition`). Found: set its mark there (`fetch_or`, beside lookups that may set it too). Not found, or the lookup fails (`DETENT_NO_MEMORY` under the cap, a source failure): add the name to the handle's carried names.
3. **Budget.** One freer pass folds at most 1,024 marked keys. A generation left part-folded becomes the namespace's pending fold: it keeps its pages, still charged to the mapping cap, and its walk position. The next freer pass resumes it before any other; a namespace that retires another generation while one is pending finishes the pending one first, unbudgeted, so a namespace has at most one; `detent_refresh` and `detent_usage` finish every pending fold.
4. **Where it runs.** On the holder of `refreshing`: the lookup that found its namespace's sequence moved (which already pays the attach, reader.md Limit 1), `detent_refresh`, or `detent_usage`. Never on a warm lookup.
5. **Exactness.** A key read OK since open is marked in an attached generation, marked in a retired generation still pinned or pending (both reported), or carried by name. The model checks it (`fold stress`: 8 readers, 50 reordering, deleting publishes, the report equals the keys read).

**Price of the fold** (estimates: 162,367 pages at 5M from dense-pages.md, the same per record at 100K and 1M; 1.8 µs to walk a marked page and its share of ancestors, about one 1,943-byte page decoded; 0.7 µs per successor lookup on verified pages, reader-lookup.md's scattered p50; up to 20 µs more per successor page touched first, its 2,360-byte digest at 243 bytes per µs plus structure checks). R knobs read, scattered; the low figure has the successor's pages already verified by the application's own reads, the high one has none:

| Store | Pages | R = 1,000 | R = 4,096 | R = every knob |
| --- | ---: | ---: | ---: | ---: |
| 100K | 3,247 | 2.2 to 19.5 ms | 7.1 to 53.6 ms | 76 to 141 ms |
| 1M | 32,473 | 2.5 to 22 ms | 9.8 to 87 ms | 0.76 to 1.4 s |
| 5M | 162,367 | 2.5 to 22 ms | 10 to 91 ms | 3.8 to 7.0 s |

The budget caps one freer pass at about 1,024 × 22.5 µs ≈ 23 ms (estimate); larger folds spread over passes or land on `detent_refresh` and `detent_usage`. Page verification in the successor is work the application's next reads of the same knobs would do anyway. The fold's memory is the carried names (heap, one per deleted knob read) and, while pending, the retiring generation's mapped pages (at most one generation per namespace, within the cap). The implementation measures the walk and the lookups (Checks, A2).

**Fork.** The child handler replaces the rows, name set and every attached generation's mark bitmaps with fresh anonymous mappings (`mmap` with `MAP_FIXED`, no copy of the parent's pages), clears the carried names and drops pending folds' marks (the parent's reads), zeroes `next`, the spares, the flag and the stripe totals, and reseeds each stripe. The child's record is its own reads.

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

The owner accepted this shape (PLAN.md D158); `switch_forced` keeps the cell count at nine, in the order below. `ever_read` is new since D158: A1 asks for each knob's read bit, and a name's row alone cannot give it (a name sampled only as `missing` has a row and was never read OK).

```c
typedef enum detent_arm {
  DETENT_ARM_PUBLISHED, DETENT_ARM_NO_HASH, DETENT_ARM_RAMP_IN, DETENT_ARM_RAMP_OUT,
  DETENT_ARM_SWITCH_FORCED, DETENT_ARM_SWITCH_IN, DETENT_ARM_SWITCH_OUT,
  DETENT_ARM_MISSING, DETENT_ARM_FALLBACK_OTHER, DETENT_ARM_COUNT
} detent_arm;
typedef enum { DETENT_VIEW_CURRENT, DETENT_VIEW_FAILED, DETENT_VIEW_DROPPED } detent_view_state;
typedef struct { size_t size; detent_str name;          /* data == NULL: the overflow row */
                 uint32_t ever_read;                     /* 1: read OK since open, exact */
                 uint64_t samples[DETENT_ARM_COUNT]; } detent_knob_usage;
typedef struct { size_t size; detent_str name_space; uint64_t revision, seen, current;
                 uint64_t attached_ns;                   /* CLOCK_MONOTONIC */
                 uint32_t state; } detent_view_usage;
typedef struct { size_t size; uint32_t period;          /* 64; 0 on an off handle */
                 uint32_t overflowed;                    /* 1: some samples are in the overflow row */
                 uint64_t statuses[16];                  /* exact lookups by detent_status */
                 uint64_t publishes, refused, attached; } detent_usage_totals;
detent_status detent_usage(detent_reader *, detent_usage_totals *,
                           void (*knob)(void *, const detent_knob_usage *),
                           void (*view)(void *, const detent_view_usage *), void *context);
/* detent_open_options grows: uint32_t usage_slots;  0: 4,096; DETENT_USAGE_OFF: no usage */
#define DETENT_USAGE_OFF UINT32_MAX
#define DETENT_MAXIMUM_USAGE_SLOTS 8388608u
```

1. **Knobs.** One record per name that has a row or was read OK since open (or, in a fork child, since the fork): every marked definition of the attached generations and of retired ones not yet folded, every carried name, and every row. `ever_read` is exact for every definition; `samples` × `period` estimates reads by arm, and is zero for a knob read but never sampled. The overflow row is reported once `overflowed` is set. A record's cells are independent relaxed counters, not a snapshot of one instant, and say nothing about which revision answered.
2. **Absence.** A defined knob with no record, or with `ever_read` 0, was not read OK since open. This holds at any `usage_slots`; `overflowed` affects only counts.
3. **Collection.** `detent_usage` takes `refreshing` (waiting as `detent_refresh` waits), finishes pending folds, collects the records and view records into memory it allocates, releases `refreshing`, then calls the callbacks. While it holds `refreshing`, lookups answer stale rather than refresh, and a namespace's first attach waits for it (Limit 11). Its cost is the walk of the marked pages of the attached generations, about 1.8 µs per marked page (estimate), plus the folds it finishes.
4. **Callbacks.** They run on the calling thread, with no lock or pin held. They may look up knobs and call `detent_usage`; they must not call `detent_close`, and the strings they receive live only during the call. Either callback may be NULL.
5. **Off.** A handle opened with `DETENT_USAGE_OFF` keeps no marks, rows, countdown or totals: `period` and `overflowed` are 0, the totals are zero but for `publishes`, `refused` and `attached`, no knob is reported, and view records are reported.
6. **ABI.** `usage_slots = 0` means the default, as `mapping_bytes = 0` does (`core.cc:745-746`), so a caller that zero-fills the grown struct, as `Reader` does (`reader.hh:43`), keeps usage on; off is explicit. `DETENT_ABI_VERSION` stays 1: the change adds a function and grows size-tagged structs, and no library has shipped (`reader.hh:37` compares equality). `DETENT_MINIMUM_MAPPING_BYTES` grows by the mark bitmap (D10).
7. **C++.** `Reader::usage()` returns the totals with vectors of plain structs. Python and Rust wrap it as they wrap lookups (client-local-source.md D1; OB5).

The hook (Q1) is fixed at open, runs after the pin is released, and is a later quest.

### D7. Leaving the process and the host (OB4)

```
app process: marks, rows ─┬─ detent_usage ─→ app's own metrics stack
                          └─ usage page (shared file) ─→ agent reads with pread
agent: per-host usage document ─→ detent-agent --status --usage (JSON)
                               └─→ label-free totals, Prometheus text file (Q3, D9)
fleet: agent summary ─→ relay ─→ authority (Q4, deferred)
```

1. **The page.** When the agent's store has a `usage/` directory, `detent_open` creates `usage/<pid>-<handle>` (mode 0640, `O_EXCL | O_NOFOLLOW`) and maps it shared; the rows, name set, totals and, per attached generation, its revision and its segments' mark bitmaps by segment identity live in it. The agent holds the same generations, so it names marks by walking its own copy. The directory is the agent's: owner `publisher_uid`, group the agent's group, mode 3733 (setgid and sticky), so others create files but cannot list, nobody deletes another's file, and on Linux each file takes the agent's group. If the create fails, the handle keeps private memory and reports it in its totals.
2. **The agent reads untrusted pages with `pread`**, never `mmap`: a process that truncates its page would raise `SIGBUS` in a mapped reader. Every field is bounds-checked; names pass the reader's name validation. A process can lie about its own reads (Limit 7).
3. **Lifetime.** The agent scans every few seconds, keeps the last counts of a process whose pid is gone (checked with its start time against pid reuse), reports them once more, then deletes the page.
4. **Fork.** The child's handler maps fresh anonymous memory over the page's range (D3, Fork), and its next refresh creates its own page.
5. **Formats.** Host: one JSON usage document in the shape of the relay's health snapshot (`src/detent_relay/host.cc:59-127`): per knob and revision the estimated reads by arm (samples × period) and the derived value (D4); the read set, outside the capped count rows (D8); per process its namespaces, revisions, lag and age. Label-free totals (lookups by status, refused publishes, processes) are written as a Prometheus text file (D9).
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

**The paged core has no position**, so the mark sits on the body's location: a bit per 8 bytes of each mapped segment (D3, Marks). The lead ruled it over a claim index keyed by the body's address (Alternatives), because it keeps Q7's exactness for every definition, which the index meets only at `usage_slots` = every knob read. A warm hit under either is one load after the walk; the bitmap's needs no multiply and no probe.

Memory per handle with one namespace attached (63 bytes of segment per knob, from the 5M store's 315,759,038 bytes, `dense-pages.md:251`), reading R knobs scattered. Both keep the default rows (1.18 MB) and name set (32 KB) for counts; the claim index needs `usage_slots` = max(4,096, R) to be exact:

| Store | Segments | R = 1,000 or 4,096: bitmaps | claim index | R = every knob: bitmaps | claim index |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100K | 7 | 1.33 MB | 1.34 MB | 1.33 MB | 34.0 MB |
| 1M | 61 | 2.21 MB | 1.34 MB | 2.21 MB | 330 MB |
| 5M | 302 | 6.0 to 6.2 MB | 1.34 MB | 6.2 MB | 1.78 GB |

The bitmaps' figure is the rows and set plus 16 KB per segment touched (all 7 or 61; 291 of 302 at R = 1,000); each further attached namespace adds its own. These are one generation's; across publishes the bitmaps add the fold (D3): carried names on the heap, and a pending generation's pages within the cap. reader.md T4 bounds the core heap at 1.5 MiB plus the per-segment bitmaps, reported: the mark bitmaps are per-segment bitmaps under that clause, and the rows (1.18 MB at the default) and carried names are core heap, which the implementation reports against 1.5 MiB with the current core's heap measured beside them.

**Surface in `src/detent_local`**, which the quest's file set gains before any edit (the journal names each file first):

| File | Change |
| --- | --- |
| `paged_page_access.hh` | `PagedPageAccess` gains `resolve_marked`, returning the page with its segment's mark bitmap and the page's offset in it; its default returns no bitmap, so `prepared_reader.cc` and the tests that implement or call `resolve` (`test_lazy_page_access`, `test_lazy_page_bitmap`, `test_dense_reads`, `test_dense_fullbuild`) do not change |
| `lazy_page_access.hh`, `lazy_page_access.cc` | `PageTable` takes a marks switch; `Segment` allocates its mark bitmap beside `VerifiedPages` from the same account (`:63-71`); `resolve_marked` (`:237`); `child_after_fork` replaces each bitmap with zeroed anonymous memory (`:292-295`); `mark_bytes()` for `testing::Stats` |
| `paged_read_state.hh`, `paged_read_state.cc` | `definition` returns the body's mark word and bit; `for_each_marked`, a resumable walk of the marked pages that yields keys, for the fold and `detent_usage` |
| `evaluation.hh` | `evaluate` returns `{value, arm}` (D4); its callers `view.cc`, `tests/detent_local/test_evaluation.cc`, `tests/detent/test_one_knob_differential.cc` take `.value` |

**Mapping cap.** The mark bitmap is charged with its segment to the handle's mapping account, as the verified bitmap is (`lazy_page_access.cc:69`): 16,384 bytes per 1 MiB segment, mapped bytes / 64, 8 MB at the 512 MiB default cap with every byte mapped. So `DETENT_MINIMUM_MAPPING_BYTES` (`include/detent/reader.h:32`), one full segment and its bitmaps, grows from 1,055,136 to 1,071,520, and `core.cc:59-60`'s `static_assert` with it. An off handle allocates no mark bitmap and accepts the same minimum. Uncharged, the bitmaps would sit outside every cap, which the cap exists to prevent; the constant is in the reserved C ABI, so the change is the owner's.

## Lifecycle

States per handle: **off** (`DETENT_USAGE_OFF`) or **on**. Per generation: **attached** (in the installed view), **retired, pinned** (replaced; a view still holds it or a lookup pins its slot), **retired** (unpinned, not yet folded), **pending** (part-folded, at most one per namespace) and **freed**. Per entry: current, failed or dropped (D5). The answer of a lookup never depends on usage. Each test is in `tests/detent_reader/test_reader_usage.cc` and fails without the property its row names; seams are in `src/detent_reader/testing.hh`: `seed`, forced period, forced equal hashes, fold budget, the windows `UsageSawEmpty`, `UsageRowWritten`, `UsageNamePublished`, `UsageMarked`, and `Stats::marks`, `samples`, `probes`, `rmw`, `folded`, `carried`, `pending`, `mark_bytes`.

| Event (code) | Usage change | `detent_usage` then | Test |
| --- | --- | --- | --- |
| Open, on (`core.cc:735-788`) | Rows, name set, stripe lines mapped; countdowns seeded | No records; period 64; totals 0 | `fresh_handle`: over 10,000 fresh handles the first lookup is sampled 30 to 160 times (expected 78.7; a fixed start of 1 gives 10,000, of 64 gives 0) |
| Open, off | Nothing mapped; no mark bitmaps | Period 0, no records; views reported | `off_handle`: lookups leave every usage stat at 0 and `mark_bytes` 0; views present |
| Open, `usage_slots` past the maximum, or `mapping_bytes` below the new minimum | `DETENT_INVALID_ARGUMENT` | | `open_bounds` (each maximum and minimum opens, one past fails) |
| First attach of a namespace (`core.cc:575-584`) | Generation attached; attach time read; state current | A view record: revision, `seen == current`, `attached_ns` between the test's clock reads | `view_records` |
| Segment mapped (first touch) | Its mark bitmap allocated and charged with it | | `mark_charge`: `mapped_bytes` grows by segment + verified + 16,384 bytes per segment on, without the 16,384 off; a cap of exactly the new minimum reads one segment |
| First OK read of a knob in a generation | One `fetch_or` | `ever_read` 1 | `ever_read`: over 1,000 knobs, the records with `ever_read` equal the knobs read OK; `marks` equals 1,000 |
| Later OK read | One load | Unchanged | `gate_events` (G1: 0 marks after warm-up) |
| Sampled lookup, any status in an opened namespace | Row found or claimed by name; one cell | The name's cell grows | `arms`: period 1, every arm's cell equals its lookups (all nine, through a definition per arm and each fallback) |
| A claimer parked at `UsageRowWritten` or `UsageNamePublished` | Others proceed and converge | One row | `claim_windows`: a second sampled lookup of the name returns within 1 s; one row after both |
| The last row taken while a claimer that saw the slot empty finds the table full | It re-examines the slot and takes the row | One row, `overflowed` 0 | `claim_boundary` (park at `UsageSawEmpty`, `usage_slots` 1) |
| Two names with one hash | Two rows | Both | `equal_hashes` (forced hash) |
| Countdown at zero | Reload from the stripe's xorshift, [1, 127] | | `cycle`: seeded; a 1,024-name cycle samples at least 512 names in 64 cycles and all within 1,000 (a fixed period of 64 samples 16, forever) |
| `DETENT_MISSING` or another fallback inside `lookup` | Stripe total; sampled: the row's `missing` or `fallback_other` cell | A record with `ever_read` 0 for a name never read OK | `fallbacks`: each status exact in `statuses`; a missing name's record has `ever_read` 0 |
| `NOT_OPENED`, `INVALID_NAME`, `INVALID_ARGUMENT` (`core.cc:792-798`) | Stripe total only | | `fallbacks` |
| `detent_revision` (`core.cc:816-831`) | Nothing | Unchanged | `revision_uncounted` |
| Table full | Samples in the overflow row; flag set | The overflow record; every knob read still `ever_read` 1 | `overflow`: `usage_slots` 4, six knobs read and sampled: six records `ever_read` 1, four with counts, two knobs' samples in the overflow row |
| Publish, new generation (`core.cc:536-538`) | The old one retired, pinned or not | Same records | `publish_reorder`: a publish that reorders and deletes definitions keeps every knob read `ever_read` 1, the kept ones marked in the new generation |
| Publish keeping the generation (same id and slot, `core.cc:523-524`) | Nothing retires | Unchanged | `kept_generation`: `folded` unchanged |
| Re-added with the same generation id at a new slot | A new `Generation`; the old one retires and folds into it | Unchanged | `readd_same_generation_usage`: `folded` grows by the knobs read, and the new generation holds their marks |
| Slot reused by another name; namespace dropped; attach failed (`core.cc:564, 567-573`) | The old generation stays attached (no retire, no fold) | Records unchanged; view states dropped or failed | `slot_reused_views`, `dropped_view`, `entry_failure_view` |
| First attach failed (`core.cc:579-582`) | No generation | No view record; `statuses` counts each failed lookup | `first_attach_failure_usage` |
| Refused publish (`core.cc:464-470`) | `refused` + 1 | `refused` | `refused_counted` |
| Stale answer (`core.cc:673-675`) | Counted as OK under its arm; marks its (old) generation | `current − seen` > 0 | `stale_view` (`testing::hold_retry`) |
| Freer meets a retired, pinned generation | Not folded | Its marks reported | `late_mark`: a lookup parked at `UsageMarked` on the old generation after the publish; its knob is reported meanwhile and marked in the successor after its unpin |
| Freer folds a retired generation within budget | Each marked key marked in the successor, or carried; then freed | Unchanged | `publish_reorder`; `fold_deleted`: a deleted knob is carried (`carried` 1) |
| A successor lookup fails during the fold | The name carried | Unchanged | `fold_failure`: a successor at the mapping cap carries the key |
| Fold past the budget | The generation pending: pages kept, walk position kept | Its marks reported | `fold_budget`: budget 4, ten knobs: `pending` 1 after the publish, 0 after the next refresh, all ten reported throughout |
| The namespace retires another generation while one is pending | The pending one finishes first, unbudgeted | Unchanged | `fold_budget` (a second publish deleting five: five carried, none lost) |
| Two retired generations, one successor | Both fold into the installed generation | Unchanged | `fold_two` (one held pinned across a second publish) |
| `detent_refresh` or `detent_usage` with a fold pending | Finished | Unchanged | `fold_budget` |
| Fold beside lookups marking the successor | Idempotent `fetch_or` | Exact set | `fold_concurrent` (TSan): 8 readers, 50 reordering, deleting publishes; the read set equals the knobs read OK |
| `detent_usage` beside lookups and refreshes | Takes `refreshing`; collects; releases before callbacks | Cells grow independently | `concurrent_usage` (TSan, period 1, 16 threads: cells sum to lookups after join); `usage_during_refresh` (ASan) |
| A callback looks up knobs and calls `detent_usage` | No lock or pin held | The inner call reports | `callback_reentry` |
| View freed (`core.cc:221-232`) | Generations no view holds retire (rows above); the rest stay | Unchanged | `usage_during_refresh` |
| Fork prepare, parent (`core.cc:251-272`) | Nothing | Unchanged | `fork_child` (parent half) |
| Fork child (`core.cc:275-287`) | Marks, rows, name set remapped empty; carried names, pending marks, totals and flag cleared; stripes reseeded | Only the child's reads | `fork_child`: the child's records are its own, the parent's unchanged |
| Close (`core.cc:850-855`) | Everything freed; pending folds dropped | | The suite under ASan |

The fold adds these transitions, the class the sweep covers: a generation's retire while pinned, its fold within budget, a deleted key, a failed successor lookup, a fold past the budget, a second retire while pending, two retirements into one successor, the fold beside marking readers, `detent_refresh` and `detent_usage` finishing it, a fork child with one pending, and close with one pending. Each has its row and test above. `tests/detent_reader/usage_claim_model.cc` (run by `usage_claim_model.sh` under TSan) checks the fold, the row claim and the sampling apart from the core.

## Checks

| Criterion (quest `observability-core`) | Check |
| --- | --- |
| A1 `detent_usage` reports each knob's read bit and counts | `test_reader_usage` (the lifecycle table) through the C ABI |
| A2 Lookup cost | G1 (`gate_events`) in the suite; G2 (`reader_bench --usage-gate`) strict at the quest's head, exclusive when strict does not pass; reader-lookup.md's base/head `reader_bench` against main; the fold's walk and lookups timed at 100K and 1M, R = 1,000 and 4,096 (light), against D3's estimates |
| A3 Suite, TSan, CI | `campaign verify`; the TSan CI job runs `concurrent_usage`, `claim_windows`, `fold_concurrent` and `usage_during_refresh` |

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
10. The fold runs on the holder of `refreshing`: up to about 23 ms per freer pass (estimate), and a namespace publishing again with a fold pending finishes it there unbudgeted (D3, Price).
11. `detent_usage` holds `refreshing` while it folds and collects: lookups answer stale meanwhile, and a namespace's first attach waits for it.
12. Carried names grow with the deleted knobs a handle read, on the heap, for the handle's life.

## Alternatives considered

**A claim index per generation** (this design's round 2). Keyed by the body's address with rows by name; core.cc only, no walk and no fold, and smaller at the default (1.34 MB against up to 6.2 MB at 5M). Exact only up to `usage_slots` knobs read, and exact for every knob only at 26 to 288 times the bitmaps' memory (D10); the lead ruled for the bitmaps, which keep Q7.

**Rows instead of the fold** (the re-review's alternative). A retiring generation's marked names go into rows, so the record across publishes is bounded by `usage_slots`, as the claim index is. Not taken: it gives up Q7 across publishes.

**Exact per-knob counting** (D2's rows, D10's five mechanisms). Answers "how often" exactly at +18.8 to +47.8 ns per lookup under 8 threads for `knob_rows64`; no exact count passes scattered names at 64 threads.

**A shared `fetch_add` per lookup.** +21.7 to +33.6 ns at 8 threads and no per-knob detail; reader.md removed it for cost.

**Per-thread counters** (`tls`, +0.4 to +14.6 ns). A thread registry per handle, and `thread_local` in a `dlopen`ed library allocates on first access (reader.md D5).

**Rows keyed by a 64-bit hash, claimed in place.** A claimer writes the name after taking the slot, so another either waits on it or makes a second row for the name (`observability-core-design.md` F2).

**Count in the application only.** Every service would have to wire `detent_usage` before an operator sees anything; kept as one of D7's consumers.

**Push to the agent over a socket.** A syscall per batch and a thread to send it; the core starts no thread (reader.md T6).

**Record values instead of arms.** Integers would need an unbounded value histogram; arms are nine cells.

## Owner decisions (PLAN.md D47)

The owner took each recommendation on 2026-11-09. The labels stay, because the sections above cite them.

**Q1.** `detent_usage` plus a hook fixed at open, both behind `detent_open_options` growth. **Q2.** Names per call, no prepared knob handles. **Q3.** `detent-agent --status --usage` prints the JSON document, and the agent writes label-free totals to a Prometheus text file rendered by Kronuz/metrics; no listener. **Q4.** Fleet aggregation upstream on relay sessions, after namespace delivery S4-S10. **Q5.** Values, not digests, in the usage document. **Q6.** The page is on whenever the agent's store has `usage/`. **Q7.** Exact read records, sampled counts (`ever_bit`, D10), unless an exact count meets the gate; none did. D48 and D61 keep `ever_sampled`, with D2's in-core gate deciding. D158 accepted D6's ABI shape; `ever_read` and the mapping minimum (D6, D10) are before the owner.

## Build plan

| Quest | Files | Criteria | Risk | Needs |
| --- | --- | --- | --- | --- |
| `observability-core` (OB1-OB3 here: arms, marks and fold, rows, `detent_usage` with view records) | D10's `src/detent_local` surface; `src/detent_reader/`, `include/detent/reader.h`, `reader.hh`, `tests/detent_reader/`, `benchmarks/reader_bench.cc` | The lifecycle table's tests; G1 and G2 (D2); the fold's timing (Checks); zero allocation and syscall tests (reader.md T1) unchanged | high (hot path) | `ever_read` and the mapping minimum ruled |
| Hook | `core.cc`, `reader.h`, `reader.hh` | A stalled hook never refuses a publish; `hook_off` within G2 against a handle with no hook | medium | observability-core |
| OB4 Usage page, agent collector and export | Kronuz/metrics (Prometheus text renderer), `core.cc`, `src/detent_agent/`, `docs/mechanisms/` | Hostile page fuzz never crashes the agent; a pre-created page makes the handle keep private memory; on Linux a page created by another uid is readable by the agent (VM); dead-process retention; tmpfs fault count 0 over 10 min (VM); the usage document matches a golden schema, caps at 65,536 count rows with `truncated` set, and keeps every read knob past the cap; the text file parses as Prometheus exposition format with no knob-name label and is replaced by rename | medium | observability-core, Q3, Q6 |
| OB5 Bindings | `bindings/python`, `bindings/rust` | Usage and hook in both | low | observability-core |
| OB6 Fleet aggregation | delivery protocol, relay, authority | Deferred; carries the read sets and sampled reads by arm (Q4) | high | Q4; namespace S4-S10 |

`detent get` (reader S7) opens its handle with `DETENT_USAGE_OFF`.
