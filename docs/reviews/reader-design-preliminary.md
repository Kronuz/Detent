# Detent review: reader design, preliminary

- Date: 2026-11-03.
- Reviewed: `docs/design/reader.md` and `docs/status/reader-design.md` at `Detent:a3bb22f`; the cited code at `Detent:d4abd4a`; `atomic-shared-ptr:9af0aa7`; `sha256:f4a3463`.
- Checked against: namespace-delivery D1 and D2; the legacy inventory; `docs/ACTORS.md`; `docs/reviews/calibration.md`.
- Checked by: probes in `.scratch/reader-review/` (`build.sh` builds them; `results-*.txt` hold the output):
  `rescore_baseline.py`, `d5_model.cc` (D5 with its publisher written out; ASan+UBSan and TSan), `asp_probe.cc` (three standard libraries), `sha_page.cc`, `macos_syscalls.cc`.
  - Not run: Detent's build and tests (nothing is implemented), the 5M corpus, Linux (seccomp untested), any remote measurement host.

## Verdict

**APPROVE WITH CONDITIONS: the shape is right; D5 and D4 lack their writer halves, and D2's per-namespace attach breaks T2. S1 to S4 may start; L1 to L4 must be in the design before S5 and S6.**

What holds, and reproduces:

- All twelve baseline rows of `benchmarks/client-read-5m-20261025.json` rescore exactly from the raw cases (268.5, 475, 762 ns p50).
- D5's reader side is sound with a seq_cst publisher and one freer: 10 s, 8 readers and a publisher, 487M lookups and 474,487 publishes under ASan+UBSan, 20.6M lookups under TSan; no freed view read, no regression, at most 4 live views (`d5_model stress`).
- D5 costs 13.9 ns per pin, word load and unpin at 1 thread, 16.4 ns mean (18.1 worst) at 8 (`d5_model bench`, i9-9980HK, `quiet strict`).
- One core behind a C ABI; R3 and R4: no evidence against them.

## Findings

**RD-1 (blocking). D5's safety depends on a publisher side the design does not state.** The reader's increment and recheck form a Dekker pair with the publisher's `current` store and counter scan. With the natural `current.store(s + 1, release)` and acquire scans, the store-buffer interleaving frees a view in use, on x86 too:

```
reader R                              publisher P (current == s)
s = current.load()       -> s
count[k] += 1 (seq_cst)
                                       current.store(s+1, release)   buffered
current.load() == s      -> passes     sum(ring[s%4]) == 0           R's add unseen
read slot.view                         free view s, munmap, close lease
```

Also, three freers ("the winner, `detent_refresh` and `detent_close`") can both see zero and free one slot: a double `munmap`, and a double `close` that may close an unrelated reused descriptor. Fix: the `current` store and the counter loads are seq_cst (or a seq_cst fence separates them), and only the holder of `refreshing` frees; `detent_refresh` takes it too.

**RD-2. Limit 3 understates the stall.** One reader parked inside a lookup blocks publication as soon as the ring wraps to its view, not when all four slots are pinned: 3 of 10 publishes succeed, then the handle stays on view 3 until the reader resumes (`d5_model stall`). That is the flaw the alternatives table holds against epochs, traded from unbounded memory to frozen freshness. The trade is reasonable (publishes are seconds apart, lookups nanoseconds); the scheme is SRCU's shape, simpler than hazard pointers. Fix: reword Limit 3; count refused publishes in S1.

**RD-3 (blocking). A forked child freezes silently.** Nothing enforces Limit 6, and pre-forking Python servers will use the inherited `Reader`. A parent thread pinned at fork leaves its count in the child forever; one holding `refreshing` leaves it set. The child then publishes 3 of 10 and 0 of 10 (`d5_model fork`). Today's child also goes stale (its refresh thread does not survive `fork`), so this is not a regression. Options: (a) a `pthread_atfork` child handler zeroes each live handle's counters and `refreshing` (the child has one thread), making handles fork-safe; (b) the handle records `getpid()` and refresh returns a status after fork. Recommend (a).

**RD-4 (blocking). D4's change word needs its protocol.**

1. The reader keeps the word it loaded before reading `selected`; keeping one loaded after the attach misses a publish landing in between until the next one.
2. The agent bumps the word at startup, so a crash between the `selected` rename and the bump hides nothing.
3. Bumps are atomic read-modify-writes on the mapping, or a single writer is guaranteed. `disk_store.cc` takes no `LOCK_EX`; the only one is the pid lock (`src/detent_cli/daemon.cc:39,63`).
4. The file gets the record checks (owner, mode, ACL) and a size check before mapping; a truncated mapping raises `SIGBUS` in every reader.
5. A new library over an older agent with no word file falls back (stat `selected` in `detent_refresh`) or refuses at open, never silently stops switching.

Ordering otherwise: an aligned release store after the rename, an acquire load per lookup (inside the 13.9 ns above).

**RD-5. A3's store-wide word scales with namespaces.** Under D2, any namespace's publish makes every reader process re-read every namespace's descriptor to find the one that moved. Fix: the mapped file carries a sequence per namespace (namespace delivery's directory, item 2), so the winner attaches only what moved.

**RD-6 (blocking). D2 plus eager attach breaks T2.** Attach is about 0.43 ms, 0.21 to 0.22 ms of it directory trust (`docs/measurements/reader-metadata-20261029/README.md`). Opening "all" eagerly pays that per namespace; three miss 1 ms. Fix: attach a namespace on its first lookup, check directory trust once per store, state T2 for N namespaces.

**RD-7. D3's verified-page table is far smaller than the store.** 65,536 slots against about 5.5M pages at 5M records (111,113 pages per 100,000 records, `benchmarks/paged-snapshot-measurements.json`), so scattered warm lookups re-verify on capacity misses. Two SHA-256 passes cost 2.2 µs for a 200-byte page, 35 µs for 4 KiB and 0.54 ms for 64 KiB (`sha_page`). Today's scattered p50 (1,777 ns) is below one 200-byte re-verification, so S1 must say which assumption is wrong. One 64 KiB page on the first lookup's path spends half of T2's budget. Also: if the table outlives a generation, the tag binds segment identity, not slot; and tables per generation, times four views, times N namespaces must fit T4. Fix: state the capacity behavior (C1).

**RD-8. Fix the ABI details that are hard to change later.** Give `detent_status` explicit values and declare unknown non-OK codes errors; make `detent_value.kind` an enum; add `detent_abi_version`; state that `detent_open` copies its strings; state that lookups are not async-signal-safe (refresh allocates, and first TLS access in a `dlopen`ed module allocates; a stripe derived from a stack address avoids TLS). Decide whether `detent_open` succeeds with no selected generation. Today the client starts on defaults and attaches later (`ACTORS.md:143`), so failing open narrows behavior and is the owner's call: (a) open succeeds and lookups return the fallback with `DETENT_UNAVAILABLE` until the word brings a generation, keeping today's behavior; (b) open fails and applications retry. Recommend (a); the same for a named namespace that does not exist yet. Eight functions are minimal.

**RD-9. T1's syscall proof.** `SECCOMP_MODE_STRICT` permits `read`, `write`, `_exit` and `sigreturn`, so a lookup that reads a file passes. Use `SECCOMP_MODE_FILTER` allowing only `write` to the result pipe and `exit`. macOS needs no seccomp: `task_info(TASK_EVENTS_INFO)` counted exactly 1,000 BSD syscalls for 1,000 `close(-1)`, 0 for a syscall-free loop, 1 Mach call per bracket (`macos_syscalls`; `getpid` is cached and does not count).

**RD-10. T1's floor should ratchet.** S3 lands D3 in `LazyPageAccess` first; hold the core to S3's numbers, and measure today's 8-thread baseline so the contention win is a number.

**RD-11. The `atomic_shared_ptr` claim is half right.** On libc++ (Apple clang 17, LLVM 23) `__cpp_lib_atomic_shared_ptr` is undefined (LLVM `c++/v1/version:404`), so lines 82-83 reach `__get_sp_mut`'s global mutex pool (`__memory/shared_ptr.h:1392`). On libstdc++ (GCC 16, C++20) the header is a plain alias and lines 82-83 are not compiled; `std::atomic<shared_ptr>` spins on the low bit of the control-block pointer per object (`bits/shared_ptr_atomic.h:399-400`, `load` at `:586`). Both lock (`is_lock_free` 0) and bump a shared count: 27.2, 30.7 and 18.5 ns per `load()` at 1 thread; 685, 793 and 1,895 ns at 8 (`asp_probe`). Hot-path uses: `Client::current_` (`client.hh:228`), per lookup in `Client::eval` and `get` (`client.cc:185,223,256,273`); `LocalReadSource::installed` (`client_source.cc:82`), per lookup through `Client` (`client.cc:166`, `client_source.cc:137,139`). The same lookups `fetch_add` shared metrics (`client.cc:28-34`). Correct the wording and cite these numbers; they are the design's strongest argument.

## Positions on the open questions

- **Q1:** (a), with RD-6's lazy attach so the paying lookup attaches one namespace.
- **Q2:** (a): one shape in every language.
- **Q3:** (a), with RD-7: verify once per page, not once per eviction.
- **Q4:** (a), conditional on RD-4 and RD-5; namespace delivery owns the file.
- **Q5:** (a), with the cap covering RD-7's tables.

## Blocking items

- L1 (RD-1): D5 states the publisher's orderings and the single freer.
- L2 (RD-3): the design picks and states the fork behavior.
- L3 (RD-4): D4 states items 1 to 5.
- L4 (RD-6): lazy per-namespace attach; T2 stated for N namespaces.

## Conditions

- C1 (RD-7, RD-10): T1 scattered records pages verified per lookup; T1 ratchets to S3's numbers.
- C2 (RD-9): filter-mode seccomp on Linux and the `task_info` count on macOS.
- C3: T3 gains injection hooks (a reader parked between load and increment across four publishes; one parked mid-lookup); T6 also runs after a fork.
- C4 (RD-2, RD-5, RD-8, RD-11): folded into the design before S5; RD-5 agreed with namespace delivery.

Not run: Detent's build and tests, the 5M corpus, Linux, any remote measurement host.

## Re-check, 2026-11-03

- Rechecked: `docs/design/reader.md` at `Detent:64f4076` against L1 to L4 (RD-1, RD-3, RD-4, RD-6) and the two new D3 mechanisms. Code cited at `Detent:d4abd4a` (unchanged on main).
- Probes in `.scratch/recheck/` (`build.sh`, `d5_rev.cc`, `granule.py`, `results-*.txt`); built and run under `quiet light`. `d5_rev.cc` is the original `d5_model` with the publisher replaced by D5's text (`reader.md:160-171`) and D10's three `pthread_atfork` handlers added.

| Finding | Verdict |
| --- | --- |
| L1 (RD-1) | CLOSED, with two gaps in D5's pseudocode (N1) |
| L2 (RD-3) | PARTIAL: the two reviewed fork cases pass; a fork during a publish does not |
| L3 (RD-4) | CLOSED |
| L4 (RD-6) | CLOSED |
| D3 verified bitmap | Sound for every page kind; verifies once per mapping, never on a capacity miss |
| D3 dropped digest compare | Sound, on a better basis than the design gives (R3's validators, not only Limit 5); two sentences to correct |

### L1 (RD-1): CLOSED

D5 states the orderings (`current.store(s + 1, seq_cst)`, `sum_seq_cst`, `reader.md:160-171`) and the single freer (`reader.md:174`: only the holder of `refreshing`; `detent_refresh` takes it too and returns without work if it is held; `detent_close` runs alone).

With the publisher written as D5 reads (reclaim after the `current` store, `free_view(next.view)` before it, plain `view` pointers, two lookups racing for `refreshing`, 8 readers):

- ASan+UBSan, 10 s: 277,987,955 lookups, 397,647 publishes, 12,216,350 refused, `max_live=4`, no freed view read, no regression. A second 10 s run: 269M lookups, 396,412 publishes, clean.
- TSan, 10 s: 21,210,840 lookups, 145,944 publishes, clean. The plain `view` read after the recheck is ordered by the `current` pair; TSan agrees.

Revert either rule and it fails:

- **Rule 1 reverted** (`current.store(release)`, acquire scans, `-DMUT_ORDER`): ASan stress read a view that was being freed in 3 of 4 runs (`bad_view=2, 0, 1, 1`; the last run also saw a sequence regression). The hardware litmus isolates the interleaving: publisher `X = 1; r1 = Y` against reader `Y += 1 (seq_cst); r2 = X`, 10M iterations, x86-64: release/acquire gives `r1 == 0 && r2 == 0` 41, 43 and 28 times (4 of 3M in an earlier run); seq_cst gives 0 of 10M, twice. This is the review's diagram, on x86. TSan did not catch the mutant in 10 s (it slows both sides); the litmus and ASan do.
- **Rule 2 reverted** (a second freer, as `detent_refresh` was before, `-DMUT_FREER`): ASan `attempting double-free` within the 10 s run; TSan reports the data race on the slot's `view`.

**N1 (D5 pseudocode, fix before S6).**

1. The retired-slot loop (`reader.md:171`) calls `free_view(ring[k].view)` and never clears the slot, and the refusal test (`:167`) frees `next.view` whenever it is non-null. Read literally, the next publish frees the dangling pointer: `-DLITERAL` aborts the ASan stress with `heap-use-after-free` in `publish_locked`. State that `free_view` clears the slot.
2. The refusal path (`:167`) does not free `built`, so each refused publish leaks the segments and lease descriptor the winner attached. It also does not say whether the view's recorded sequence advances. If it does not, a handle stalled by a parked reader (Limit 3) re-attaches on every lookup (about 0.43 ms each, on the caller's thread) until the reader resumes. If it does, nothing retries after the reader resumes until the next change. Pick one; the first with a backoff is the safer shape. D4 only says a failed attach remembers its sequence.
3. State the orderings of `refreshing`: acquire on the compare-and-swap, release on the clear. The `current.load(relaxed)` ("only the holder stores it") depends on them across holders; the model used them.

### L2 (RD-3): PARTIAL

D10 picks option (a) (`reader.md:209-213`) and T6 and Limit 8 follow. The review's two cases pass with D10's handlers (prepare takes the registry mutex, parent releases it, child zeroes every counter and clears `refreshing`, then releases it): `fork-pinned` and `fork-refreshing` both publish 10 of 10 in the child, ASan clean (they were 3 and 0 before).

The handlers do not cover a fork while a parent thread is inside a publish, and D10 says only that an unpublished `built` view leaks. A publisher parked after `free_view` and before the slot is cleared (the window includes the unmaps and the descriptor close) leaves a freed pointer in the child. The child clears `refreshing`, so its first publish frees the pointer again: `fork-midfree` aborts with `heap-use-after-free` in `publish_locked` (`results-fork.txt`). The same window exists in `free_view(next.view)` and between `next.view = built` and the `current` store.

Fix (one sentence in D10): the prepare handler also takes each live handle's `refreshing` (waiting out an attach or publish, which holds it for under a millisecond to a few), the parent handler releases it, and the child handler zeroes the counters only. Then no child ever sees a half-finished publish, and Limit 8 shrinks to nothing. With that change `fork-midfree`, `fork-refreshing` and `fork-pinned` each publish 10 of 10 in the child under ASan (`results-fork-fix.txt`). T6 should add a fork during a parked publish (the park hook after `free_view`), not only an idle `refreshing` holder.

### L3 (RD-4): CLOSED

D4 rules 1 to 5 (`reader.md:143-151`) map one to one onto RD-4 items 1 to 5:

1. Rule 1: the winner loads the store word and the namespace sequence before reading `selected` and records those.
2. Rule 2: the agent bumps the store word and every namespace sequence at startup, before ready; an unchanged generation id keeps the generation.
3. Rule 3: release `fetch_add` after the rename, `owner.lock` exclusive in S5 (`owner.lock` is namespace delivery's file, `namespace-delivery.md:90`; today only the pid lock takes `LOCK_EX`, `daemon.cc:39,63`, as the design says).
4. Rule 4: `O_RDONLY | O_NOFOLLOW | O_CLOEXEC`, owner, mode, mode-only ACL, size a multiple of 8 covering every slot, read-only mapping.
5. Rule 5: a store with `selected` and no valid `change` is refused at open; no fallback.

N2 (one sentence): "a slot past the mapping is remapped by the winner only" needs the old mapping to live as long as any view whose slot pointers point into it. Make the mapping a member of the view, freed by the D5 freer, so a remap never unmaps under a reader.

### L4 (RD-6): CLOSED

D2 attaches nothing at open (`reader.md:111-123`): directory trust once per store, `change` checked, table read; the first lookup in a namespace attaches it. T2 is gated for 1 and for 1,000 namespaces with S1's counters (directory-trust calls per open = 1, namespaces attached = 1). Today `PreparedReader::open` calls `open_publisher_directory` itself (`prepared_reader.cc:96`), so S6 must give attach a directory-descriptor parameter; the design says attach reuses the descriptor, which is the refactor. The 1,000-entry table read and parse at open is inside T2's path and unmeasured; the gate covers it.

### D3 verified bitmap: sound, verifies once per mapping

Granule argument, for every page kind:

- A reference is accepted only with `offset % 8 == 0`, `80 <= length <= 65536`, `offset + length <= segment_bytes` (`paged_layout.cc:25-30`, applied in `decode_reference` and `encode_reference`). So the 80-byte header is a length floor for all four kinds, and `floor(offset / 80)` is below the bitmap size once the existing extent check passes. `Overflow` cannot occur in a tree: both validators reject it (`paged_graph_validator.cc:275`, `paged_warm_validator.cc:378`) and the builder cannot make one (`paged_page_builder.cc:42`).
- Distinct pages need distinct granules. Pages in a validated generation are disjoint: both validators mark every 8-byte cell a reference covers and `require` it unmarked (`paged_graph_validator.cc:254-261`; the warm validator keeps the same bitmap). Two disjoint pages of at least 80 bytes start at least 80 bytes apart, so their `offset / 80` differ. `granule.py`: 0 collisions among disjoint pairs at every 8-byte offset of a 1 MiB segment with every gap 80 to 400 step 8, and 0 in 2,000 random disjoint layouts; overlapping references do collide (offsets 8 and 16), which is what the validators reject. The disjointness basis is the validators' cell mark, not the header: the design cites the header (`paged_layout.hh:7-8`), and the sentence should cite the mark.
- Size: 13,108 bits and 1,639 bytes per 1 MiB segment, 1/640 of mapped bytes; the 5M store's 1.51 GB gives 2.25 MiB (the design says about 2.4 MiB; fine).

Once per mapping: there is no table to overflow. The segment table is fixed at attach (`2 × root.segments`), segment objects are freed only by the D5 freer, and bits are only set. A page re-verifies only when its segment object is freed and mapped again. Today's cache (`lazy_page_access.cc:81,115-129`) does evict, which RD-7 priced. One thing the design leaves open: a miss at the mapped-bytes cap throws `ReadSourceFailure` (`:102-104`), and the `catch (...)` then sets `failed` for the whole namespace generation (`:173-175`). That is today's behavior and not a capacity re-verification, but D3 should say what a cap miss does now that the cap is the only limit.

### D3 dropped digest compare: sound, with two corrections

The designer's conclusion holds; the stated basis is thinner than the real one.

- The compare (`lazy_page_access.cc:155-156`) only compares the reference used now with the reference that verified the page, not the bytes. In a generation that passed full validation (R3) it cannot fail: every page is referenced once, and every reference's digests were checked against the bytes. A second digest for the same offset needs a reference the validators would have rejected (a cell marked twice) or a segment rewritten under a stable identity. Both are publisher actions, which Limit 5 already trusts.
- What is lost is narrow: a publisher that rewrites a verified parent page to carry a different child reference used to be caught on a cached child page, and is not now. That detection was partial (a consistent forgery of the child passed), so the claim "a publisher matter under R3 and Limit 5" is right.

Corrections:

1. Limit 5 says "unchanged from today"; after this it is slightly weaker. Say that a rewritten reference to a verified page is no longer observed.
2. D3 shares a segment object across generations of one namespace. Today each generation re-opens the payload and checks the name's device and inode against the mapping (`lazy_page_access.cc:108-110`); a kept object skips that. State that segment identity means immutable bytes (the agent never rewrites a payload under one identity), which is what makes sharing the bits, and the dropped compare, safe across generations. The semantic checks that depend on the root revision (`:162-165`) are monotone under A4, so a bit set under a later revision never admits a page the earlier view would reject in a validated tree.

### New findings

- **N1** (D5 pseudocode): slot clearing, the refusal path and the `refreshing` orderings, above. Fix before S6.
- **N2** (D4 rule 4): the `change` remap lifetime, above.

### Updated conditions

L1, L3 and L4 are met. L2 needs D10's prepare-handler sentence and a T6 case for a fork during a parked publish. N1 and N2 go with C4 before S6. S1 to S4 may start; S5 and S6 wait on L2 and N1.

Not run: Detent's build and tests (nothing is implemented), the 5M corpus, Linux (x86-64 macOS only, so the store-buffer litmus ran on x86 TSO, not on a weaker model), any remote measurement host, any benchmark.
