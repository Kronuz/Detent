> Status: DRAFT (2026-11-03) Designing; the preliminary review and its re-check are folded in (L2, N1, N2 and the D3 corrections on `design/reader-rev2`). R1-R4 are the owner's rulings; everything after them is the designer's draft, revised against [the preliminary review and re-check](../reviews/reader-design-preliminary.md).

# Reader

One reader core with a C ABI over the agent's mapped generations. C++, Rust and Python wrap it; `detent get` is one more caller.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-03. Ledger: `docs/status/reader-design.md`.
- Base: `Detent:d4abd4a`. Line citations are at that commit unless another commit is named.
- Depends on: `namespace-delivery.md` D8 (the store table and the `change` file) and the lead's call on its ND-R5, one store directory with no per-namespace directories.

| Review | State |
| --- | --- |
| Architect, preliminary | [APPROVE WITH CONDITIONS](../reviews/reader-design-preliminary.md); L1-L4 and C1-C4 folded in; re-check 2026-11-03: L1, L3, L4 closed, L2 partial, N1 and N2 new, all folded in (see "Review findings") |

Applications read knobs the way the `detent` CLI does: map the agent's selected generation, look up the knob, evaluate it. One reader core with a C ABI serves every language; C++, Rust and Python wrappers are thin. The legacy inventory (`docs/status/legacy-removal-inventory.md`, branch `explore/legacy-inventory`) maps what it replaces.

## Decisions settled by the owner

**R1. Defaults per call.** Every lookup takes the caller's default and returns it when the knob is absent. There is no defaults file and no defaults merged under the generation. Ruled 2026-11-03.

**R2. No overrides in the reader.** The reader returns what the authority published. Tests get their values by writing a real generation through a test helper, not through an override path in the production reader. Ruled 2026-11-03.

**R3. Prepared attachment only.** The reader attaches to agent-prepared generations. Full validation is an agent-side check, not a reader mode. (Lead's recommendation; the owner may revise.)

**R4. Explicit handles.** One reader handle per process, owned by the application; no global free functions. (Lead's recommendation; the owner may revise.)

## Abstract

A warm lookup today crosses `detent::Client`, a virtual read source, a locking `std::shared_ptr` atomic load and a trie walk that takes one `std::mutex` per page, shared by every thread. Two background threads keep it fresh and retire old generations. The design replaces that stack with `libdetent_reader`: nine C functions over an opaque handle, a lock-free segment table with a verified-page bitmap per segment, a generation switch detected by **one load of the namespace's mapped sequence per lookup**, namespaces attached on their first lookup, and old generations retired through per-handle striped reader counts with one freer. The core starts no thread and survives `fork`. Evaluation lives in one function. The CLI and both bindings move onto the core before legacy step H, so H becomes pure deletion.

Nothing here is measured by this design. The gate rows hold the core to today's warm baseline at 5M knobs (p50 **268.5 ns** for an integer, **475 ns** for a bool, **762 ns** for a hashed bool through a pinned view), ratcheted to S3's numbers once they exist, and to **200/200** fresh-process first reads below 1 ms with 1 and with 1,000 namespaces; today's path misses 1 ms in three of the four capsule-trial arms.

## What a lookup costs today

| Step | Code | What it does |
| --- | --- | --- |
| Entry | `src/detent/client.cc:165-166` | Virtual `read_view()`, then a second virtual `eval` |
| Pin | `src/detent_local/client_source.cc:137`; `src/detent/client.cc:185,223,256,273` | `atomic_shared_ptr::load` on `LocalReadSource::installed` (`client_source.cc:82`) and `Client::current_` (`include/detent/client.hh:228`). On libc++ `__cpp_lib_atomic_shared_ptr` is undefined, so `atomic_shared_ptr.h:82-83` (`atomic-shared-ptr:9af0aa7`) call `std::atomic_load_explicit`, which takes a lock from a global mutex pool. On libstdc++ the header is an alias for `std::atomic<std::shared_ptr>`, which spins on a lock bit in the control-block pointer per object. Both are not lock-free and bump a reference count every thread writes: 27.2, 30.7 and 18.5 ns per `load()` at 1 thread, 685, 793 and 1,895 ns at 8 (Apple clang 17, LLVM 23 libc++, GCC 16 libstdc++; the review's `asp_probe`). |
| Metrics | `client.cc:28-34` | A shared `fetch_add` per lookup |
| Layers | `client_source.cc:42-50` | Override table, mapped view, baked-defaults fallback |
| Evaluate | `src/detent_local/view.cc:373-412` | `ReadView::try_eval` walks the page trie |
| Each page | `src/detent_local/lazy_page_access.cc:132-158` | `std::lock_guard` on one mutex per generation (`:133`), a hash-map find (`:137`), an LRU find (`:154`) |
| First touch | `lazy_page_access.cc:89-113,159-167` | `openat`, `fstat`, ACL check, `mmap`, `fstatat` per segment; SHA-256 verification per page, again after an LRU eviction |
| Threads | `client_source.cc:113-128`; `src/detent_distribution/retirement.cc:156-166` | A refresh thread polls `selected` every 200 ms; the default retirement runtime (`view.cc:37-44`) starts a cleanup pthread |
| CLI | `src/detent_agent/main.cc:316-327` | Bool lookup first, then a second lookup for an integer |

The spec forbids a mutex on the read path (`justknobs-implementation-spec.md:99`). Today's path takes two locks per lookup.

Baselines, single thread, Intel i9-9980HK, macOS 15.8.1, 5M records, `benchmarks/client-read-5m-20261025.json` (medians of four repetitions; the review rescored all twelve rows from the raw cases):

| Operation | Pinned view p50 / p99, ns | `Client` p50 / p99, ns |
| --- | ---: | ---: |
| Integer, hot key | 268.5 / 338.5 | 301 / 384 |
| Bool, hot key | 475 / 586 | 517.5 / 643 |
| Hashed bool (cohort), hot key | 762 / 1013.5 | 810.5 / 1081.5 |
| Integer, 1M scattered keys | 1777 / 2789.5 | 1818.5 / 2806.5 |

Multi-threaded contention on the two locks is unmeasured; T1 asks the lead for today's 8-thread baseline. The scattered row is below one re-verification of a 200-byte page (2.2 µs, the review's `sha_page`), so either few scattered lookups miss the verification cache or pages are smaller than assumed; S1's counters say which.

First reads: 200/200 below 1 ms with the publisher running, 198/200 stopped, median 0.715 ms (`docs/reviews/prepared-managed-qualification-2026-10-26.md`); the capsule trial records 193 to 200 of 200 across four arms (`docs/measurements/reader-capsule-managed-5m-20261029/README.md`). Attachment is partly attributed: an instrumented median of about 0.43 ms, of which directory trust is 0.21 to 0.22 ms of caller CPU, with 75 to 79 allocations in the constructor (`docs/measurements/reader-metadata-20261029/README.md` at `Detent:52246e2`). The first lookup is not attributed; an exploratory 40-process run splits a median of 0.49 ms attach and 0.53 ms first lookup (`benchmarks/prepared-client-5m-20261025.json`).

## Design

### D1. The C ABI

```c
/* include/detent/reader.h: the whole public surface */
#define DETENT_ABI_VERSION 1
typedef struct detent_reader detent_reader;
typedef struct { const char *data; size_t size; } detent_str;   /* data == NULL: absent */
typedef enum detent_status {
  DETENT_OK = 0, DETENT_MISSING = 1, DETENT_WRONG_TYPE = 2, DETENT_INVALID_NAME = 3,
  DETENT_NOT_OPENED = 4,       /* namespace outside the handle's list */
  DETENT_SOURCE_FAILURE = 5,   /* the generation failed verification */
  DETENT_UNAVAILABLE = 6,      /* no prepared generation for this namespace yet */
  DETENT_UNTRUSTED = 7,        /* ownership, mode, ACL or ancestry refused */
  DETENT_INVALID_ARGUMENT = 8, DETENT_NO_MEMORY = 9, DETENT_IO = 10
} detent_status;               /* any value other than DETENT_OK, known or not, is an error */
typedef enum detent_kind { DETENT_KIND_BOOL = 1, DETENT_KIND_INT = 2 } detent_kind;
typedef struct {
  size_t size;                                          /* sizeof, for extension */
  detent_str store;                                     /* store directory */
  const detent_str *namespaces; size_t namespace_count; /* 0: all, including later ones */
  uint32_t publisher_uid;
} detent_open_options;
typedef struct { detent_kind kind; bool boolean; int64_t integer; } detent_value;

uint32_t detent_abi_version(void);
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

1. Every lookup writes `fallback` to `*out` for any status but `DETENT_OK` (R1). Lookups never throw across the boundary and never allocate once warm.
2. Names are `ns:key` as the spec defines them, validated as today (`view.cc:18-23`).
3. `detent_get` has no switch: the spec reserves and ignores it for integers (`justknobs-implementation-spec.md:541`), and the mapped path already has none (`view.cc:346`).
4. `detent_read` returns kind and value in one descent, for the CLI.
5. `publisher_uid` is required. Trust is never inferred from the directory owner, as today (`src/detent_local/prepared_reader.cc:89-160`).
6. `detent_open` copies `store` and every namespace string; the caller's buffers may die on return. `detent_abi_version` returns the `DETENT_ABI_VERSION` the library was built with; a wrapper refuses a library whose major value differs from its header's.
7. Lookups are thread-safe on one handle; `detent_close` must not race any other call on the handle. No function works without a handle (R4).
8. No function is async-signal-safe: a lookup may attach (D2, D4), which allocates and opens files.
9. Whether `detent_open` succeeds on a store with no selected generation is Q6; this design assumes (a), today's behavior: open succeeds and lookups return the fallback with `DETENT_UNAVAILABLE` until a generation appears.

### D2. Namespaces, attached on first lookup

The handle holds a *view*: an immutable array of attached namespaces (name, generation root, sequence slot, sequence seen), sorted by name, plus the store table it was built from. A lookup splits the name, finds the namespace by binary search and walks that root.

The store is namespace delivery's (`namespace-delivery.md` D8): one store directory, a `selected` table with one entry per namespace (name, cursor, generation id, root length, root and `.read` digests), and the `change` file (D4). The reader's assumptions, agreed with that design's "Reader alignment" table:

1. **A1.** Each namespace has its own table entry and prepared generation, attachable with today's checks (`prepared_reader.cc:89-181`). Every file sits in the one store directory.
2. **A2.** The table lists the namespaces the agent's credential allows; a handle opened with none follows all of them, including later ones (namespace-delivery D2).
3. **A3.** The `change` file carries a store-wide word and one sequence per namespace (D4).
4. **A4.** A namespace's revision never goes backwards within an incarnation (namespace-delivery D1); the reader refuses a regression as today (`src/detent_local/reader.cc:59-61`). A new incarnation is a new namespace to the reader.

`detent_open` checks directory trust once for the store (`open_publisher_directory`, `prepared_reader.cc:51`), opens and checks `change` (D4 rule 4), reads the table and attaches nothing. The first lookup in a namespace attaches it: open its `.read` record and lease, check them, build a view that adds it, publish (D5). Attach reuses the store's directory descriptor, so no later attach repeats directory trust. A namespace's first attach makes concurrent lookups in that namespace wait for the winner (an atomic wait on the handle's `refreshing` flag), so no thread answers a real knob with its default because it lost the race; a switch never waits (D4). A lookup reads one namespace from one generation; the reader promises nothing across namespaces (namespace-delivery D1).

### D3. No lock, allocation or syscall on a warm lookup

The core implements the existing page seam, `PagedPageAccess::resolve` (`src/detent_local/paged_page_access.hh:10-17`), replacing the mutex, hash map and LRU in `lazy_page_access.cc:47-177`:

1. **Segments.** Per generation, an open-addressed table of `2 × root.segments` atomic pointers keyed by segment identity, fixed at attach. Each points to a segment object holding the mapping and its verified bitmap; an attach that keeps a segment of the namespace's previous generation points to the same object. A hit is one acquire load and a compare. A miss opens and maps the segment with today's checks (`:89-113`) and publishes it with a compare-and-swap; a losing thread unmaps its copy. Segment objects are counted at attach and freed by the D5 freer, never per lookup.
2. **Verified pages.** Each segment object carries one bit per 80-byte granule of its extent, indexed by `offset / 80`. A reference is accepted only with `offset % 8 == 0`, a length of 80 to 65,536 bytes and `offset + length` inside the segment (`src/detent_local/paged_layout.cc:25-30`), and the validators that prepared the generation (R3) reject two references covering one 8-byte cell (`src/detent_local/paged_graph_validator.cc:254-261`). Pages are therefore disjoint and at least 80 bytes long, so they start at least 80 bytes apart and never share a granule. A segment holds at most 1 MiB (`paged_layout.hh:7`), so its bitmap is at most 13,108 bits, about 1.6 KiB, allocated when the segment is mapped. A hit is one acquire load of a 64-bit word and a bit test; a miss verifies the page as today (`lazy_page_access.cc:159-167`) and sets the bit with a release `fetch_or`. Two threads verifying one page both set the bit.

A page is verified once per mapping, never again on a capacity miss (Q3), and the bit binds segment identity, not a table slot. Segment identity means immutable bytes: the agent never rewrites or replaces a payload under one identity. That is what lets a kept segment object, and its bits, serve the next generation without today's per-generation re-open and device and inode check (`lazy_page_access.cc:108-110`). The revision-dependent checks at verification (`:162-165`) are monotone under A4, so a bit set under one revision never admits a page a later view would reject.

A hit no longer compares the reference's digests with the ones that verified the page (today, `:155-156`). In a generation that passed the validators every page is referenced once, so the compare cannot fail; a second digest for one offset needs a reference the validators reject or a payload rewritten under its identity, both publisher actions. What is lost is narrow and stated in Limit 5. A hit still bounds the page by the reference's length.

A segment miss that would pass the mapped-bytes cap maps nothing and returns the fallback with `DETENT_NO_MEMORY` for that lookup; it does not set `failed`, since nothing failed verification, and once the cap is reached later misses refuse before any syscall. Today the same miss throws (`:102-104`) and the catch-all marks the whole generation failed (`:173-175`); the core keeps answering from the segments it holds. Nothing is evicted: segment objects leave only through the D5 freer. The bitmaps total 1/640 of mapped bytes, about 2.4 MiB with all 1.51 GB of the 5M store mapped (estimate from namespace-delivery ND-R8's size), shared between views and counted against the mapped-bytes cap (Q5, T4). They replace `verification_cache_bytes` (`include/detent_local/view.hh:36`).

The `failed` flag stays one atomic (`:67`), per namespace generation. Once the pages on a path are verified and their segments mapped, a lookup makes no syscall, takes no lock and allocates nothing. First touch still pays syscalls and SHA-256 on the caller's thread; T2 bounds it.

### D4. Generation switch: one load per lookup

The `change` file is namespace delivery's (D8); the reader needs this shape of it (RD-5, to agree there before S5):

- Word 0 is the store word. The agent increments it after every replacement of `selected`.
- Word 1 + *slot* is a namespace's sequence. The table entry carries the slot, assigned when the namespace first appears and never reused while the store exists. The agent increments it after every `selected` replacement that changes that namespace's entry.
- The file grows by extension, never shrinks, and is never truncated, unlinked or replaced while the store exists.

Each lookup loads its namespace's sequence (acquire) through the slot pointer in its view entry and compares it with the sequence the entry was built from. A publish in another namespace never moves it, so a process re-attaches only what it reads and what moved. On a mismatch, the lookup tries one compare-and-swap on the handle's `refreshing` flag. The winner re-reads the table, re-attaches each attached namespace whose generation id changed, builds a view that shares the rest, publishes it (D5), reclaims what it can, and answers from the new view. Every other thread keeps reading the installed view without waiting. A publish refused by a pinned slot is D5's. A failed attach frees what it built, keeps the installed view and remembers the sequence it failed at; the next attempt waits for the sequence to move or for `detent_refresh`, which runs the same check from a thread the application picks (Q1). A lookup in a namespace absent from the view loads the store word: unchanged since the view's table, it returns `DETENT_UNAVAILABLE` (or `DETENT_NOT_OPENED` outside the handle's list); moved, the winner re-reads the table and attaches it if present (D2).

The protocol (RD-4):

1. **Load before read.** The winner loads the store word and the namespace's sequence before reading `selected`, and records those values in the view. A publish landing between the load and the read leaves the view a step behind its own sequence, so the next lookup refreshes again; recording a value loaded after the read would hide that publish until the next one.
2. **Startup bump.** The agent increments the store word and every namespace's sequence once at startup, after its startup barrier and before it reports ready (namespace-delivery ND-R9). A crash between a `selected` rename and its increments then hides nothing; a winner that finds an unchanged generation id keeps that namespace's generation.
3. **One writer, atomic increments.** Increments are release `fetch_add`s on the agent's shared read-write mapping of the file, after the rename. The agent holds the store's `owner.lock` exclusively while it writes the store, so there is one writer; the atomic increments keep a second writer from losing one regardless. Today's store takes no exclusive lock: the only `LOCK_EX` is the daemon's pid lock (`src/detent_cli/daemon.cc:39,63`), so S5 adds it.
4. **Checks before mapping.** `change` is opened `O_RDONLY | O_NOFOLLOW | O_CLOEXEC` from the trusted store directory and gets the record checks: owner `publisher_uid`, no group or other write, a mode-only ACL (as `lazy_page_access.cc:100`). Its size must be a multiple of 8 and cover word 0 and every slot the table names; the reader maps that size read-only. A slot past the mapping is remapped by the winner only. Each mapping is a reference-counted member of every view built over it, freed by the D5 freer with the last such view, so a remap never unmaps under a reader and slot pointers stay valid for their view's life.
5. **No fallback.** The project has no legacy agent: the agent creates `change` with the store, before the first `selected`, and always writes it. A store with `selected` and no valid `change` is refused at open with `DETENT_SOURCE_FAILURE`; a store with neither has no generation yet (Q6).

### D5. Retiring a generation while readers hold it

Each handle holds a ring of four view slots, each with 64 cache-line-padded reader counters, and a 64-bit `current` sequence; slot = sequence mod 4. The reader's increment and recheck form a Dekker pair with the publisher's `current` store and counter scan, so both sides are sequentially consistent (RD-1):

```c++
// Reader: pin the slot for the sequence it saw, then recheck.
auto s = current.load(acquire); auto &slot = ring[s % 4];
slot.count[stripe].fetch_add(1, seq_cst);
if (current.load(seq_cst) != s) { slot.count[stripe].fetch_sub(1, release); retry; }
/* ... look up through slot.view ... */
slot.count[stripe].fetch_sub(1, release);

// Publisher: only the holder of `refreshing`, taken by compare_exchange(false, true, acquire)
// and cleared by store(false, release). free_view(p) unmaps, closes and sets p = nullptr.
auto s = current.load(relaxed);                     // the last holder stored it before its release
auto &next = ring[(s + 1) % 4];
if (next.view) {
  if (sum_seq_cst(next.count) != 0) { ++refused; back_off(); return; }  // nothing attached yet
  free_view(next.view);
}
auto built = attach(...);                           // on failure: frees what it built, returns
next.view = built;
current.store(s + 1, seq_cst);
for (each slot k != (s + 1) % 4 with a view) if (sum_seq_cst(ring[k].count) == 0) free_view(ring[k].view);
```

Either the reader's recheck sees the new sequence and it backs off without touching the view, or the publisher's later scan sees its increment and keeps the view. Sequences never repeat, so a reader that pinned a reused slot fails its recheck. With `current.store(release)` and acquire scans, a store-buffer interleaving frees a view in use, on x86 too; the review's model (`d5_model`) shows the seq_cst protocol clean under ASan, UBSan and TSan.

**Refusal.** The winner checks the next slot before it attaches, so a refused publish has built nothing to free. It does not advance the view's recorded sequences, so the stale view keeps mismatching and the publish retries once the parked reader resumes. To keep a stall (Limit 3) from costing an attempt on every lookup, a refusal sets a retry time on the handle, 1 ms doubling to 200 ms and reset by a successful publish; a mismatched lookup before that time answers from the installed view without trying `refreshing`. Only that path reads the clock (`CLOCK_MONOTONIC_COARSE` or `mach_absolute_time`, no syscall); `detent_refresh` ignores the retry time.

**One freer.** Only the holder of `refreshing` frees a slot: the lookup that won it, or `detent_refresh`, which takes it too and returns `DETENT_OK` without work if another thread holds it. `detent_close` runs alone (D1 item 7) and frees every slot. Freeing unmaps segments no live view shares and closes the lease descriptor, which drops its shared `flock` (`prepared_reader.cc:114`). At most four views live per handle.

`stripe` is a hash of a stack address in the lookup's frame, computed once per lookup and kept for the decrement: threads have disjoint stacks, so they spread over the 64 stripes without thread-local storage, whose first access in a `dlopen`ed module allocates. A collision shares a line, never a count. A warm lookup costs two atomic read-modify-writes on a line its stripe writes (the review measured 13.9 ns for pin, word load and unpin at 1 thread and 16.4 ns mean at 8 in its model), instead of a lock and a reference count every thread writes. The core owns its generations directly and does not use `RetirementDomain`, whose default runtime starts a thread (`view.cc:37-44`).

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

Each returns the value, or the caller's default on any error, and exposes the status through `lookup` (Q2 (a)). Each checks `detent_abi_version` once at load. Python holds the GIL across a lookup.

### D8. `detent get` on the core

`cmd_managed_get` (`main.cc:364-435`) becomes open, one `detent_read`, print, close. It keeps starting a user-mode agent when the store is unavailable (`main.cc:401-415`), printing `--default` unless the source failed (`main.cc:428-431`), and exit status 0 or 1. It drops `--defaults` (R1), `Client`, `LocalReadSource` and the double lookup.

### D9. The test helper (R2)

`libdetent_testing`, never linked into `libdetent_reader`, publishes knob definitions through the agent's own paged publication (`DiskStore`, `include/detent_local/disk_store.hh:31-72`), which writes the prepared reader metadata (`src/detent_local/paged_publication_artifacts.cc:70`). Each call publishes the next revision and moves the namespace's sequence and the store word, so tests exercise switching. It exposes C++ and a C function taking the knob-definition JSON the authority already parses, for Python and Rust tests. It replaces `detent::testing::TestKnobs` (`include/detent/testing.hh:15-28`) and `Client(Snapshot)` (`include/detent/client.hh:131`), and shares its writer with namespace delivery's offline image builder (namespace-delivery D9). The hand-built fixture (`tests/detent_local/prepared_reader_fixture.hh:38-146`) stays for malformed-input tests.

### D10. Fork

Pre-forking servers inherit handles, so a handle is fork-safe (RD-3 option (a)). The first `detent_open` registers `pthread_atfork` handlers once (`pthread_once`); live handles sit on a global list under a mutex that open and close take. The prepare handler takes that mutex, then each live handle's `refreshing` (acquire), waiting out any attach or publish in progress, which holds it for under a millisecond to a few. The parent handler releases each `refreshing` (release), then the mutex. The child handler zeroes every counter of every slot of every handle, then releases each `refreshing` and the mutex. No child therefore sees a half-finished publish, freed pointer or half-built view, and the zeroing is sound because the child runs one thread, the one that forked, which is not inside a lookup (D1 item 8). Counts left by parent threads would otherwise pin their slots forever, and a held `refreshing` would stop every switch: the review's model publishes 3 of 10 and 0 of 10 in those two cases (`d5_model fork`); a fork during a publish parked after `free_view` aborted with a use-after-free when the child only cleared `refreshing`, and publishes 10 of 10 with these handlers (re-check, `fork-midfree`).

The child's lease descriptors share the parent's open file descriptions, so the agent sees a generation in use until both processes close it, which is correct.

## Gate rows

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Warm lookup | C and C++ p50 and p99 no worse than the pinned-view baseline above, per operation, hot and scattered; once S3 lands, no worse than S3's numbers. Rust within 10 ns of C at p50; Python within 150 ns (both estimates, fixed after the first screen). 8 threads on one handle: per-thread p50 within 10% of 1 thread, reported beside today's 8-thread baseline. Zero core allocations; zero syscalls. Pages verified per lookup recorded for the scattered run. | `benchmarks/reader_bench` (S6) and its Rust and Python twins over the 5M corpus (`5000000-restore-r12`), hot key and 1M keys sampled with seed `0x524541444552` as in `client-read-5m`, 1M calls, four repetitions interleaved with the pinned-view baseline and S3's build, under `quiet exclusive`; the lead measures, also today's 8-thread baseline. Allocations: `tests/support/allocation_probe.hh` around the loop; Python, `tracemalloc` on bool results. Syscalls, Linux CI: after warmup a child installs a `SECCOMP_MODE_FILTER` program allowing only `write` on the result pipe and `exit`/`exit_group` (killing on anything else), runs 1M lookups and writes its result; strict mode would allow `read`. macOS: `task_info(TASK_EVENTS_INFO)` before and after 1M lookups, gated on a BSD syscall delta of 0 (the review's probe counts 1,000 for 1,000 `close(-1)`, 0 for a syscall-free loop, 1 Mach call per bracket). |
| T2 First read | 200/200 below 1 ms at 5M, publisher running and stopped, from `detent_open` entry to the first checked value, for a store of 1 namespace and one of 1,000 namespaces (the 5M namespace plus 999 of 100 knobs) | The prepared qualification runner (`docs/measurements/prepared-managed-5m-20261026-runner.py.txt`) with a C probe over the core; 40 keys in the 5M namespace, five rounds, a warmup process per key, `quiet exclusive`. S1's counters recorded per sample: directory-trust calls (1 per open), table bytes read, namespaces attached (1), pages verified. |
| T3 Switch | 16 threads read for 60 s while the helper publishes 1,000 generations, each setting `t:rev` to its revision. Every thread sees non-decreasing values and only `DETENT_OK`; live views never exceed four; descriptors and mappings return to their pre-open counts after close. Injected: a reader parked between its `current` load and its increment across four publishes, and one parked mid-lookup; each resumes with no freed view read, and refused publishes are counted. TSan, and ASan with UBSan, clean. | `ctest -R reader_switch --repeat until-fail:20` under each sanitizer preset, with the core's test-only park hooks (`DETENT_READER_TEST_HOOKS`, never in a release build). |
| T4 Memory | Core heap at most 1.5 MiB plus the verified bitmaps after open and 1M scattered lookups at 5M; bitmaps at most mapped bytes / 640, reported. Private dirty memory delta at most 4 MiB plus the bitmaps. Mapped virtual size reported, not gated. | The T1 probe prints live heap and bitmap bytes from the allocation probe, then `Private_Dirty` from `/proc/self/smaps_rollup` (Linux) or `phys_footprint` (macOS). |
| T5 Identity | Same stdout and exit status as today's `detent get` | A 2,000-knob store written by the helper with fixed inputs (default bools, global rollouts, forced and rollout switch overrides, integers including `INT64_MIN` and `INT64_MAX`), every key across hash, switch and `--default` combinations, plus missing, wrong-type and invalid names. Expected output recorded once from the binary at the commit before S7 (`tests/detent_reader/get_identity.tsv`). At scale: core against `ReadView::try_eval` on 1M sampled 5M keys. |
| T6 No threads, fork | Thread count unchanged across open, lookups, refresh and close. A child forked while a parent thread is parked mid-lookup, while another holds `refreshing` idle, and while a third is parked inside a publish after `free_view` (the fork-midfree case), sees every one of 10 publishes and passes T3's checks; the parent finishes its parked publish. | `/proc/self/task` (Linux), `task_threads` (macOS), in the T3 test; the fork cases use T3's park hooks, including one after `free_view`. |

## Alternatives considered

**A pure reader per language.** Rust and Python would need no C++ runtime and would read idiomatically. They would also each reimplement the paged format, SHA-256 page verification, directory-ancestry trust with platform ACL rules (FreeBSD alone needed two corrections, per the qualification review) and evaluation. Every format change would land three times and drift. Not selected.

**Wrap `detent::Client`, as the bindings do today.** It brings the shared-pointer pin, two virtual hops, the refresh thread and the defaults and override layers that R1 and R2 remove. Not selected.

**pybind11 or cxx over a C++ class.** A generator dependency per language and an ABI tied to one compiler and standard library. A C ABI costs nine functions. Not selected.

**A refresh thread or an interval stat of `selected`.** The thread is today's design and the brief rules it out. An interval stat needs no new store file, but adds a clock read to every lookup and a few syscalls per interval on whichever caller trips it (Q4).

**One store-wide word per lookup.** Simpler, but any namespace's publish makes every reader process re-read the table and compare every attached namespace (RD-5). Not selected.

**A verified-page tag table of fixed size.** The draft's 65,536 slots against about 5.5M pages at 5M records re-verify scattered pages on every capacity miss, at 2.2 µs to 0.54 ms per page (RD-7). Not selected.

| Retirement scheme | Why not |
| --- | --- |
| Reference count per lookup (today) | One counter written by every lookup on every thread, behind a lock |
| Hazard pointers | A per-thread slot registry per handle; threads come and go (Python, Rust pools) |
| Epoch-based reclamation | The same registry, and a stalled thread blocks every later retirement, with unbounded memory |
| Per-thread pin, released on the next lookup | An idle thread keeps its last generation mapped indefinitely, a silent memory leak in thread pools |

D5 is SRCU's shape with a bounded ring: a stalled reader freezes freshness (Limit 3) instead of growing memory.

## Limits

1. The first lookup in a namespace, and the first after it publishes, pays its attach on the caller's thread (about 0.43 ms instrumented today, less directory trust after open), and the first touch of a segment or page pays syscalls and SHA-256 (Q1, Q3).
2. **Silent:** if `change` is replaced, a reader keeps its old mapping and keeps answering from the old generation. D4's agent invariant prevents it; `detent_refresh` also re-stats the file and remaps it.
3. **Silent:** one reader descheduled inside a lookup blocks publication as soon as the ring wraps to its slot, not when all four are pinned: after at most three more publishes the handle stays on the view before it until that reader resumes (`d5_model stall`: 3 of 10 publishes, then 10 of 10 after resuming). Refused publishes are counted (S1, S6).
4. **Silent:** with R1, a misspelled name returns the caller's default unless the caller reads the status. This reverses the spec's fail-fast rule (`justknobs-implementation-spec.md:339`), which needs rewriting with the R1 ruling.
5. The publisher's account can rewrite a mapped page after verification, or truncate a mapped file and raise `SIGBUS` in every reader; the reader cannot see either. This is slightly weaker than today: a rewritten reference in a verified parent to a page already verified is no longer observed, since D3 drops the digest compare on a hit (`lazy_page_access.cc:155-156`). Today's compare caught only that case, and a consistent forgery of the child passed it anyway.
6. **Silent:** a page verified once and corrupted on disk later is served unchecked by a long-lived handle until namespace delivery's scrub finds it (namespace-delivery ND-R8). Verify-once-per-page makes this the stated behavior rather than a cache accident.
7. Lookups are not async-signal-safe (D1 item 8).
8. Rust and Python consumers link the C++ runtime through `libdetent_reader`.

Documents this design changes once built: `docs/ACTORS.md:131-145` (refresh thread, baked defaults, `Client`), spec sections 5, 8 and 11, `docs/FAST_CLIENT_ARCHITECTURE.md` (first read measured from `Client` construction), and `docs/PREPARED_ACTIVATION_ARCHITECTURE.md` (`reader_validation: "full"` goes with R3).

## Open questions for the owner

The review's positions are recorded with each; none changes R1-R4. The lead took (a) on Q1-Q6 under "move fast" (`docs/status/reader-design.md`); the owner may reverse any.

**Q1. Who pays the attach after a publish?** (a) The first lookup that sees the change, with `detent_refresh` to pay it earlier from a chosen thread. (b) Only `detent_refresh`; lookups never do I/O, but an application that never calls it never moves. *Recommendation: (a)*, the only option that keeps a library-only consumer fresh with no thread; with D2's lazy attach the paying lookup attaches one namespace. Review: (a).

**Q2. What do wrappers do on a wrong type, an invalid name or a namespace outside the handle?** (a) Return the default; `lookup` gives the status; the CLI prints the error. (b) Raise or panic, as Python raises today (`bindings/python/tests/test_detent.py:47-59`). *Recommendation: (a)*, one shape in every language and no exceptions on a hot path. Review: (a). Python's raising is removed behavior, so it is the owner's call.

**Q3. Keep SHA-256 verification of each page at first touch?** (a) Keep it, once per page per mapping (D3). (b) Trust the agent's preparation proof and check bounds only: a faster first lookup, no defense against on-disk corruption after preparation. *Recommendation: (a)*; S1 measures its share of the first read. Review: (a), verifying once per page, not once per eviction. Namespace delivery D10 also needs (a).

**Q4. A mapped change file or an interval stat?** (a) The `change` file (D4). (b) Stat `selected` at most once per interval. *Recommendation: (a)*: one load per lookup and no clock. Namespace delivery owns the file and must agree D4's per-namespace sequences (RD-5). Review: (a), conditional on D4's protocol and RD-5.

**Q5. Reader capacity settings** (`docs/READER_CAPACITY_ARCHITECTURE.md`, `src/detent_agent/reader_capacity.cc`). (a) Keep a mapped-bytes cap, covering the verified bitmaps, as an open option and drop the heap ledger, since the core's heap is fixed at attach. (b) Keep all of them. (c) Drop them. *Recommendation: (a)*. Dropping operator-set caps narrows behavior, so it is the owner's call. Review: (a), with the cap covering the bitmaps.

**Q6. Does `detent_open` succeed on a store with no selected generation?** Today the client starts and attaches later (`docs/ACTORS.md:143`, per the review). (a) Open succeeds; lookups return the fallback with `DETENT_UNAVAILABLE` until `change` and a generation appear, and the same for a named namespace that does not exist yet. With no `change` mapped there is no word to load, so while the handle has no store a lookup that wins `refreshing` retries the open at most once per 200 ms, today's poll interval (`client_source.cc:113-128`), reading a clock on that path only. (b) Open fails with `DETENT_UNAVAILABLE` and the application retries. *Recommendation: (a)*, which keeps today's behavior; (b) narrows it. Raised by RD-8.

## Review findings

| Finding | Where it landed |
| --- | --- |
| RD-1, L1 | D5: seq_cst `current` store and counter scans, the publisher written out, one freer; T3 |
| RD-2 | Limit 3 reworded; refused publishes counted (S1, S6, T3); alternatives note |
| RD-3, L2 | D10 (option (a), `pthread_atfork`); the prepare handler takes each handle's `refreshing`, the child only zeroes counters (re-check L2); T6 adds the fork-midfree case |
| N1 (re-check) | D5: `free_view` clears the slot, the next slot is checked before attaching so a refusal builds nothing, recorded sequences do not advance on refusal and a backoff paces retries, `refreshing` orderings stated; D4 frees a failed attach |
| N2 (re-check) | D4 rule 4: the `change` mapping is a member of every view built over it |
| D3 corrections (re-check) | D3: granules argued from the reference and overlap checks; immutable segment identity; cap misses; Limit 5 |
| RD-4, L3 | D4 rules 1-5; rule 5 has no older-agent case |
| RD-5 | D4's per-namespace sequences; alternatives; Q4; agreement with namespace delivery before S5 |
| RD-6, L4 | D2: lazy attach, directory trust once per store; T2 for 1 and 1,000 namespaces |
| RD-7, C1 | D3's per-segment bitmap; Q3; T1 and S1 record pages verified; T4; Limit 6 |
| RD-8 | D1 items 6-9, explicit status values, `detent_kind`, `detent_abi_version`; D5's stack-derived stripe; Q6 |
| RD-9, C2 | T1: seccomp filter mode on Linux, `task_info` on macOS |
| RD-10 | T1 ratchets to S3; today's 8-thread baseline |
| RD-11 | "What a lookup costs today", Pin and Metrics rows |
| C3 | T3 park hooks; T6 after a fork |
| C4 | RD-2, RD-5, RD-8, RD-11 above; RD-5 still to agree with namespace delivery |

## First steps

Each step lands alone with CI green. S1 through S9 all land before legacy step H, which reverses the inventory's plan to land the reader inside H: H then deletes `Client`, its sources, `detent::global`, `TestKnobs` and the first evaluator copy with no callers left.

| Step | Change | Needs |
| --- | --- | --- |
| S1 | First-read and lookup counters behind a build flag: directory trust, record opens, table bytes read, segment opens, pages verified (per lookup in the scattered bench), bytes hashed. The core adds namespaces attached and refused publishes in S6. The lead measures. | Nothing |
| S2 | D6: lift `evaluate` out of `ReadView::try_eval`; existing tests unchanged | Nothing |
| S3 | D3 inside `LazyPageAccess`, with a 16-thread TSan test; today's readers lose the per-page mutex and the LRU | Nothing |
| S4 | D9 test helper, C++ and C | Nothing |
| S5 | D4's `change` file in the agent: per-namespace sequences, startup bump, exclusive `owner.lock`, with a test | Q4; RD-5 agreed with namespace delivery |
| S6 | `libdetent_reader`: D1, D2, D5, D10, the C++ header; T3, T6 and the syscall tests; `reader_bench`. Attach takes the store's directory descriptor as a parameter: today `PreparedReader::open` opens the directory itself (`prepared_reader.cc:96`) | S2, S3, S4; S5 for switching; Q6 for open's behavior on an empty store |
| S7 | D8, `detent get` on the core; T5. Lands before or after legacy G, which removes the shared-memory path beside it | S6, Q5 |
| S8 | Python extension over the C ABI, replacing pybind11 | S6, Q2 |
| S9 | Rust crate over the C ABI, replacing cxx | S6, Q2 |
