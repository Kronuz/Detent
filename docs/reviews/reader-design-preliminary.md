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
