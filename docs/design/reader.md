> Status: DRAFT (2026-11-03) Designing, ready for preliminary review. R1-R4 are the owner's rulings; everything after them is the designer's draft on `design/reader`.

# Reader

One reader core with a C ABI over the agent's mapped generation. C++, Rust and Python wrap it; `detent get` is one more caller.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03. Ledger: `docs/status/reader-design.md`.
- Base: `Detent:d4abd4a`. Line citations are at that commit unless another commit is named.

| Review | State |
| --- | --- |
| Architect, preliminary | Pending |

Applications read knobs the way the `detent` CLI does: map the agent's selected generation, look up the knob, evaluate it. One reader core with a C ABI serves every language; C++, Rust and Python wrappers are thin. The legacy inventory (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`) maps what it replaces.

## Decisions settled by the owner

**R1. Defaults per call.** Every lookup takes the caller's default and returns it when the knob is absent. There is no defaults file and no defaults merged under the generation. Ruled 2026-11-03.

**R2. No overrides in the reader.** The reader returns what the authority published. Tests get their values by writing a real generation through a test helper, not through an override path in the production reader. Ruled 2026-11-03.

**R3. Prepared attachment only.** The reader attaches to agent-prepared generations. Full validation is an agent-side check, not a reader mode. (Lead's recommendation; the owner may revise.)

**R4. Explicit handles.** One reader handle per process, owned by the application; no global free functions. (Lead's recommendation; the owner may revise.)

## Abstract

A warm lookup today crosses `detent::Client`, a virtual read source, a `std::shared_ptr` atomic load and a trie walk that takes one `std::mutex` per page, shared by every thread. Two background threads keep it fresh and retire old generations. The design replaces that stack with `libdetent_reader`: eight C functions over an opaque handle, a lock-free page table, a generation switch detected by **one load of a mapped change word per lookup**, and old generations retired through per-handle striped reader counts, reclaimed on caller threads. The core starts no thread. Evaluation lives in one function. The CLI and both bindings move onto the core before legacy step H, so H becomes pure deletion.

Nothing here is measured yet. The gate rows hold the core to today's warm baseline at 5M knobs (p50 **268.5 ns** for an integer, **475 ns** for a bool, **762 ns** for a hashed bool through a pinned view) and to **200/200** fresh-process first reads below 1 ms, which today's path misses in three of the four capsule-trial arms.

## What a lookup costs today

| Step | Code | What it does |
| --- | --- | --- |
| Entry | `src/detent/client.cc:165-166` | Virtual `read_view()`, then a second virtual `eval` |
| Pin | `src/detent_local/client_source.cc:137` | `atomic_shared_ptr::load`, which is `std::atomic_load_explicit` on a `std::shared_ptr` (`atomic_shared_ptr.h:82-83` at `atomic-shared-ptr:9af0aa7`). libc++ and libstdc++ implement it with a global mutex pool, then bump a reference count every thread writes. |
| Layers | `client_source.cc:42-50` | Override table, mapped view, baked-defaults fallback |
| Evaluate | `src/detent_local/view.cc:373-412` | `ReadView::try_eval` walks the page trie |
| Each page | `src/detent_local/lazy_page_access.cc:132-158` | `std::lock_guard` on one mutex per generation (`:133`), a hash-map find (`:137`), an LRU find (`:154`) |
| First touch | `lazy_page_access.cc:89-113,159-167` | `openat`, `fstat`, ACL check, `mmap`, `fstatat` per segment; SHA-256 verification per page |
| Threads | `client_source.cc:113-128`; `src/detent_distribution/retirement.cc:156-166` | A refresh thread polls `selected` every 200 ms; the default retirement runtime (`view.cc:37-44`) starts a cleanup pthread |
| CLI | `src/detent_agent/main.cc:316-327` | Bool lookup first, then a second lookup for an integer |

The spec forbids a mutex on the read path (`justknobs-implementation-spec.md:99`). Today's path takes two kinds per lookup.

Baselines, single thread, Intel i9-9980HK, macOS 15.8.1, 5M records, `benchmarks/client-read-5m-20261025.json` (medians of four repetitions):

| Operation | Pinned view p50 / p99, ns | `Client` p50 / p99, ns |
| --- | ---: | ---: |
| Integer, hot key | 268.5 / 338.5 | 301 / 384 |
| Bool, hot key | 475 / 586 | 517.5 / 643 |
| Hashed bool (cohort), hot key | 762 / 1013.5 | 810.5 / 1081.5 |
| Integer, 1M scattered keys | 1777 / 2789.5 | 1818.5 / 2806.5 |

Multi-threaded contention on the two mutexes is unmeasured.

First reads: 200/200 below 1 ms with the publisher running, 198/200 stopped, median 0.715 ms (`docs/reviews/prepared-managed-qualification-2026-10-26.md`); the capsule trial records 193 to 200 of 200 across four arms (`docs/measurements/reader-capsule-managed-5m-20261029/README.md`). Attachment is partly attributed: an instrumented median of about 0.43 ms, of which directory trust is 0.21 to 0.22 ms of caller CPU, with 75 to 79 allocations in the constructor (`docs/measurements/reader-metadata-20261029/README.md` at `Detent:52246e2`). The first lookup is not attributed; an exploratory 40-process run splits a median of 0.49 ms attach and 0.53 ms first lookup (`benchmarks/prepared-client-5m-20261025.json`).

## Design

### D1. The C ABI

```c
/* include/detent/reader.h: the whole public surface */
typedef struct detent_reader detent_reader;
typedef struct { const char *data; size_t size; } detent_str;   /* data == NULL: absent */
typedef enum {
  DETENT_OK, DETENT_MISSING, DETENT_WRONG_TYPE, DETENT_INVALID_NAME,
  DETENT_NOT_OPENED,      /* namespace outside the handle's list */
  DETENT_SOURCE_FAILURE,  /* the generation failed verification */
  DETENT_UNAVAILABLE,     /* no prepared generation selected */
  DETENT_UNTRUSTED,       /* ownership, mode, ACL or ancestry refused */
  DETENT_INVALID_ARGUMENT, DETENT_NO_MEMORY, DETENT_IO
} detent_status;
typedef struct {
  size_t size;                                          /* sizeof, for extension */
  detent_str store;                                     /* generation directory */
  const detent_str *namespaces; size_t namespace_count; /* 0: all, including later ones */
  uint32_t publisher_uid;
} detent_open_options;
typedef struct { int kind; /* 1 bool, 2 int */ bool boolean; int64_t integer; } detent_value;

detent_status detent_open(const detent_open_options *, detent_reader **out);
detent_status detent_eval(detent_reader *, detent_str name, detent_str hash,
                          detent_str switch_value, bool fallback, bool *out);
detent_status detent_get(detent_reader *, detent_str name, int64_t fallback, int64_t *out);
detent_status detent_read(detent_reader *, detent_str name, detent_str hash,
                          detent_str switch_value, detent_value *out);
detent_status detent_refresh(detent_reader *);
detent_status detent_revision(detent_reader *, detent_str name_space, uint64_t *out);
const char *detent_status_name(detent_status);
void detent_close(detent_reader *);
```

1. Every lookup writes `fallback` to `*out` for any status but `DETENT_OK` (R1). Lookups never throw across the boundary and never allocate.
2. Names are `ns:key` as the spec defines them, validated as today (`view.cc:18-23`).
3. `detent_get` has no switch: the spec reserves and ignores it for integers (`justknobs-implementation-spec.md:541`), and the mapped path already has none (`view.cc:346`).
4. `detent_read` returns kind and value in one descent, for the CLI.
5. `publisher_uid` is required. Trust is never inferred from the directory owner, as today (`src/detent_local/prepared_reader.cc:89-160`).
6. Lookups are thread-safe on one handle; `detent_close` must not race them. No function works without a handle (R4).

### D2. Per-namespace generations

The handle holds a *view*: an immutable array of (namespace, generation root) sorted by name. A lookup splits the name, finds the namespace by binary search and walks that root. Today's store has one generation holding every namespace (`include/detent_local/paged_layout.hh:30-37`), so the first view maps every namespace to the same root and the core runs on today's store unchanged.

`namespace-delivery.md` is still a brief; the reader assumes:

1. **A1.** Each namespace has its own selected descriptor and prepared generation, attachable with today's checks (`prepared_reader.cc:89-181`).
2. **A2.** The store lists its namespaces in one record; a handle opened with none follows all of them, including later ones (namespace-delivery D2).
3. **A3.** One store-wide change word (D4) changes when any namespace's selection changes or a namespace appears or disappears.
4. **A4.** A namespace's revision never goes backwards (namespace-delivery D1); the reader refuses a regression as today (`src/detent_local/reader.cc:59-61`).

A lookup reads one namespace from one generation. The view swaps namespaces together, but the reader promises nothing across them. Open on the namespace side: the on-disk layout, whether namespaces share segments, and which namespaces "all" means for a host whose agent filters by credential.

### D3. No lock, allocation or syscall on a warm lookup

The core implements the existing page seam, `PagedPageAccess::resolve` (`src/detent_local/paged_page_access.hh:10-17`), with two tables fixed at attach, replacing the mutex, hash map and LRU in `lazy_page_access.cc:47-177`:

1. **Segments.** An open-addressed table of `2 × root.segments` atomic pointers keyed by segment identity. A hit is one acquire load and a compare. A miss opens and maps the segment with today's checks (`:89-113`) and publishes it with a compare-and-swap; a losing thread unmaps its copy.
2. **Verified pages.** A direct-mapped table of 16-byte tags, sized by `verification_cache_bytes` (1 MiB by default, `include/detent_local/view.hh:36`, so 65,536 slots). The tag hashes segment slot, offset, length and both digests. A hit is two acquire loads; a miss verifies the page as today (`lazy_page_access.cc:159-167`) and stores the tag. A collision or a torn tag reads as a miss and costs one re-verification, never a wrong answer.

The `failed` flag stays one atomic (`:67`). Once the pages on a path are in the tables, a lookup makes no syscall, takes no lock and allocates nothing. First touch still pays syscalls and SHA-256 on the caller's thread; T2 bounds it.

### D4. Generation switch: one load per lookup

The agent keeps an 8-byte change word in a store file that readers map read-only. It stores an incremented value after every replacement of a selected descriptor (`src/detent_local/disk_store.cc:664-666`) and never truncates, unlinks or replaces that file while the store exists. The file name belongs to namespace delivery's store format.

Each lookup loads the word and compares it with the value its view was built from. On a mismatch, the lookup tries one compare-and-swap on the handle's `refreshing` flag. The winner attaches the namespaces whose descriptors changed, builds a view that shares the unchanged roots, publishes it (D5), reclaims what it can, and answers from the new view. Every other thread keeps reading the installed view without waiting. A failed attach keeps the installed view and remembers the word it failed at; the next attempt waits for the word to move or for `detent_refresh`, which runs the same check from a thread the application picks (Q1).

### D5. Retiring a generation while readers hold it

Each handle holds a ring of four view slots, each with 64 cache-line-padded reader counters, and a 64-bit `current` sequence; slot = sequence mod 4.

```c++
// A lookup pins the slot for the sequence it saw, then rechecks.
auto s = current.load(acquire); auto &slot = ring[s % 4];
slot.count[stripe].fetch_add(1, seq_cst);
if (current.load(seq_cst) != s) { slot.count[stripe].fetch_sub(1, release); retry; }
/* ... look up through slot.view ... */
slot.count[stripe].fetch_sub(1, release);
```

The refresh winner places the new view in the next slot, which must be empty, and stores `current = s + 1`. A retired slot is freed when its counters sum to zero: the winner, `detent_refresh` and `detent_close` check. Freeing unmaps segments that no newer view shares and closes the lease descriptor, which drops its shared `flock` (`prepared_reader.cc:114`).

It is safe because sequences never repeat. A reader that pins a retired slot rechecks `current`, sees a newer sequence and backs off without touching the view; its sequentially consistent increment precedes the recheck, and the publisher's store precedes its count scan. Comparing sequences rather than slots keeps a reused slot safe. If the next slot still holds an unreclaimed view, the winner does not publish and retries at the next word change, so at most four views live per handle.

`stripe` is a thread-local index handed out round-robin on first use. A warm lookup costs two uncontended atomic read-modify-writes on a line only its stripe writes (estimate, not measured), instead of a mutex pool and a reference count every thread writes. The core owns its generations directly and does not use `RetirementDomain`, whose default runtime starts a thread (`view.cc:37-44`).

### D6. Evaluation in one place

`detent::local::evaluate(const BorrowedDefinition &, std::string_view qualified, hash, switch)` is lifted unchanged from `view.cc:389-411`: a matching switch override (forced bool or group rollout), then the global rollout, then the default; no hash returns the default; the bucket is `stable_bucket` (`include/detent/hashing.hh:15`). The core and `ReadView::try_eval` call it.

| Copy deleted | Where | Deleted by |
| --- | --- | --- |
| `detail::evaluate` over `BoolDef` | `include/detent/detail/evaluation.hh:9-22`; callers `client.cc:199`, `src/detent/read_view.cc:30` | Legacy H |
| `ShmReader::eval` | `src/detent_agent/shm_reader.cc:191-232` | Legacy G |
| Override precedence (R2) | `evaluation.hh:23-34`; `client_source.cc:44,52` | Legacy H |
| Baked-defaults fallback (R1) | `client_source.cc:49,57` | Legacy H |

### D7. Wrappers

| Language | Shape | Dependency change |
| --- | --- | --- |
| C++ | Header-only `include/detent/reader.hh`: move-only `detent::Reader`, `eval(name, fallback, hash, switch)`, `get(name, fallback)`, `lookup(...)` returning value and status | None |
| Rust | Hand-written `extern "C"` declarations and a safe `Reader` (`Send + Sync`, closes on drop) | Drops `cxx` and `cxx-build` (`bindings/rust/Cargo.toml`) |
| Python | A C extension over the C ABI, `METH_FASTCALL`: `Reader(store, namespaces=(), publisher_uid=...)`, `eval`, `get`, `lookup` | Drops pybind11 (`bindings/python/CMakeLists.txt:7-11`) |

Each returns the value and exposes the status through `lookup` (Q2). Python holds the GIL across a lookup.

### D8. `detent get` on the core

`cmd_managed_get` (`main.cc:364-435`) becomes open, one `detent_read`, print, close. It keeps starting a user-mode agent when the store is unavailable (`main.cc:401-415`), printing `--default` unless the source failed (`main.cc:428-431`), and exit status 0 or 1. It drops `--defaults` (R1), `Client`, `LocalReadSource` and the double lookup.

### D9. The test helper (R2)

`libdetent_testing`, never linked into `libdetent_reader`, publishes knob definitions through the agent's own paged publication (`DiskStore`, `include/detent_local/disk_store.hh:31-72`), which writes the prepared reader metadata (`src/detent_local/paged_publication_artifacts.cc:70`). Each call publishes the next revision and moves the change word, so tests exercise switching. It exposes C++ and a C function taking the knob-definition JSON the authority already parses, for Python and Rust tests. It replaces `detent::testing::TestKnobs` (`include/detent/testing.hh:15-28`) and `Client(Snapshot)` (`include/detent/client.hh:131`), and shares its writer with namespace delivery's offline image builder (brief item 4). The hand-built fixture (`tests/detent_local/prepared_reader_fixture.hh:38-146`) stays for malformed-input tests.

## Gate rows

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Warm lookup | C and C++ p50 and p99 no worse than the pinned-view baseline above, per operation, hot and scattered. Rust within 10 ns of C at p50; Python within 150 ns (both estimates, fixed after the first screen). 8 threads on one handle: per-thread p50 within 10% of 1 thread. Zero core allocations; zero syscalls. | `benchmarks/reader_bench` (S6) and its Rust and Python twins over the 5M corpus (`5000000-restore-r12`), hot key and 1M keys sampled with seed `0x524541444552` as in `client-read-5m`, 1M calls, four repetitions interleaved with the pinned-view baseline, under `quiet exclusive`. Allocations: `tests/support/allocation_probe.hh` around the loop; Python, `tracemalloc` on bool results. Syscalls, Linux CI: a child enters `SECCOMP_MODE_STRICT` after warmup and runs 1M lookups; any syscall kills it. |
| T2 First read | 200/200 below 1 ms at 5M, publisher running and stopped, from `detent_open` entry to the first checked value | The prepared qualification runner (`docs/measurements/prepared-managed-5m-20261026-runner.py.txt`) with a C probe over the core; 40 keys, five rounds, a warmup process per key, `quiet exclusive`. S1's counters recorded per sample. |
| T3 Switch | 16 threads read for 60 s while the helper publishes 1,000 generations, each setting `t:rev` to its revision. Every thread sees non-decreasing values and only `DETENT_OK`; live views never exceed four; descriptors and mappings return to their pre-open counts after close. TSan, and ASan with UBSan, clean. | `ctest -R reader_switch --repeat until-fail:20` under each sanitizer preset. |
| T4 Memory | Core heap at most 1.5 MiB after open plus 1M scattered lookups at 5M; private dirty memory delta at most 4 MiB. Mapped virtual size reported, not gated. | The T1 probe prints live heap bytes from the allocation probe, then `Private_Dirty` from `/proc/self/smaps_rollup` (Linux) or `phys_footprint` (macOS). |
| T5 Identity | Same stdout and exit status as today's `detent get` | A 2,000-knob store written by the helper with fixed inputs (default bools, global rollouts, forced and rollout switch overrides, integers including `INT64_MIN` and `INT64_MAX`), every key across hash, switch and `--default` combinations, plus missing, wrong-type and invalid names. Expected output recorded once from the binary at the commit before S7 (`tests/detent_reader/get_identity.tsv`). At scale: core against `ReadView::try_eval` on 1M sampled 5M keys. |
| T6 No threads | Thread count unchanged across open, lookups, refresh and close | `/proc/self/task` (Linux), `task_threads` (macOS), in the T3 test. |

## Alternatives considered

**A pure reader per language.** Rust and Python would need no C++ runtime and would read idiomatically. They would also each reimplement the paged format, SHA-256 page verification, directory-ancestry trust with platform ACL rules (FreeBSD alone needed two corrections, per the qualification review) and evaluation. Every format change would land three times and drift. Not selected.

**Wrap `detent::Client`, as the bindings do today.** It brings the shared-pointer pin, two virtual hops, the refresh thread and the defaults and override layers that R1 and R2 remove. Not selected.

**pybind11 or cxx over a C++ class.** A generator dependency per language and an ABI tied to one compiler and standard library. A C ABI costs eight functions. Not selected.

**A refresh thread or an interval stat of `selected`.** The thread is today's design and the brief rules it out. An interval stat needs no new store file, but adds a clock read to every lookup and a few syscalls per interval on whichever caller trips it (Q4).

| Retirement scheme | Why not |
| --- | --- |
| Reference count per lookup (today) | One counter written by every lookup on every thread, behind a mutex pool |
| Hazard pointers | A per-thread slot registry per handle; threads come and go (Python, Rust pools) |
| Epoch-based reclamation | The same registry, and a stalled thread blocks every later retirement |
| Per-thread pin, released on the next lookup | An idle thread keeps its last generation mapped indefinitely, a silent memory leak in thread pools |

## Limits

1. The first lookup after a publish pays the attach on the caller's thread (about 0.43 ms instrumented today), and the first touch of a segment or page pays syscalls and SHA-256 (Q1, Q3).
2. **Silent:** if the change-word file is replaced, a reader keeps its old mapping and keeps answering from the old generation. The agent invariant in D4 prevents it; `detent_refresh` also re-stats the file and remaps it.
3. **Silent:** a thread descheduled mid-lookup pins its slot; with all four slots pinned, publication waits and readers stay on an older view.
4. **Silent:** with R1, a misspelled name returns the caller's default unless the caller reads the status. This reverses the spec's fail-fast rule (`justknobs-implementation-spec.md:339`), which needs rewriting with the R1 ruling.
5. The publisher's account can rewrite a mapped page after verification; the reader cannot see it. Unchanged from today.
6. A handle is not usable across `fork`; the child opens its own.
7. Rust and Python consumers link the C++ runtime through `libdetent_reader`.

Documents this design changes once built: `docs/ACTORS.md:131-145` (refresh thread, baked defaults, `Client`), spec sections 5, 8 and 11, `docs/FAST_CLIENT_ARCHITECTURE.md` (first read measured from `Client` construction), and `docs/PREPARED_ACTIVATION_ARCHITECTURE.md` (`reader_validation: "full"` goes with R3).

## Open questions for the owner

**Q1. Who pays the attach after a publish?** (a) The first lookup that sees the change, with `detent_refresh` to pay it earlier from a chosen thread. (b) Only `detent_refresh`; lookups never do I/O, but an application that never calls it never moves. *Recommendation: (a)*, the only option that keeps a library-only consumer fresh with no thread.

**Q2. What do wrappers do on a wrong type, an invalid name or a namespace outside the handle?** (a) Return the default; `lookup` gives the status; the CLI prints the error. (b) Raise or panic, as Python raises today (`bindings/python/tests/test_detent.py:47-59`). *Recommendation: (a)*, one shape in every language and no exceptions on a hot path.

**Q3. Keep SHA-256 verification of each page at first touch?** (a) Keep it. (b) Trust the agent's preparation proof and check bounds only: a faster first lookup, no defense against on-disk corruption after preparation. *Recommendation: (a)* until S1 measures its share of the first read; then decide with that number.

**Q4. A mapped change word or an interval stat?** (a) A new store file the agent increments (D4). (b) Stat `selected` at most once per interval. *Recommendation: (a)*: one load per lookup and no clock. It adds a store artifact, which namespace delivery should own.

**Q5. Reader capacity settings** (`docs/READER_CAPACITY_ARCHITECTURE.md`, `src/detent_agent/reader_capacity.cc`). (a) Keep a mapped-bytes cap as an open option and drop the heap ledger, since the core's heap is fixed at attach. (b) Keep all of them. (c) Drop them. *Recommendation: (a)*. Dropping operator-set caps narrows behavior, so it is the owner's call.

## First steps

Each step lands alone with CI green. S1 through S9 all land before legacy step H, which reverses the inventory's plan to land the reader inside H: H then deletes `Client`, its sources, `detent::global`, `TestKnobs` and the first evaluator copy with no callers left.

| Step | Change | Needs |
| --- | --- | --- |
| S1 | First-read counters behind a build flag: directory trust, record opens, segment opens, pages verified, bytes hashed. The lead measures. | Nothing |
| S2 | D6: lift `evaluate` out of `ReadView::try_eval`; existing tests unchanged | Nothing |
| S3 | D3 inside `LazyPageAccess`, with a 16-thread TSan test; today's readers lose the per-page mutex | Nothing |
| S4 | D9 test helper, C++ and C | Nothing |
| S5 | D4 change word in the agent, with a test | Q4 |
| S6 | `libdetent_reader`: D1, D2, D5, the C++ header; T3, T6 and the seccomp test; `reader_bench` | S2, S3, S4; S5 for switching |
| S7 | D8, `detent get` on the core; T5. Lands before or after legacy G, which removes the shared-memory path beside it | S6, Q5 |
| S8 | Python extension over the C ABI, replacing pybind11 | S6, Q2 |
| S9 | Rust crate over the C ABI, replacing cxx | S6, Q2 |

What I want from preliminary review:

1. The D5 protocol: the ordering argument and the four-slot bound.
2. Whether D3's tag table keeps today's verification guarantee.
3. The assumptions A1 to A4 against namespace delivery.
4. Whether T1's targets are strict enough: matching today's locked path is a floor.
