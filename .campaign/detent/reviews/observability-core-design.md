# observability-core review: design

Commit: Detent:fa7e5fbad78b4adcdd2d97227f0f809b513fff05
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Round 3 at Detent:fa7e5fba (whole design)

Scope: all of `docs/design/observability.md` at `Detent:fa7e5fba`, checked against a lifecycle I
built from the landed code (`src/detent_reader/core.cc`, `src/detent_local/lazy_page_access.cc`,
`paged_read_state.cc`, `evaluation.hh`, `view.cc`; no source differs from main's `Detent:39b95217`
on the branch). R1-R8 are fixed. The mark is the right mechanism: a warm hit is one relaxed load, a
first read one `fetch_or`, and it stays exact through lock-free pins and retirement. The row claim
holds under 16 threads. What blocks is the fold and the edges around it:

1. The 1,024-key budget does not bound a refresh. Every second publish folds every knob read since
   open on the application thread that finds it: up to 3.8-7.0 s at 5M (the doc's own estimate),
   while first attaches, `fork()` and `detent_usage` wait (T1).
2. Usage changes answers. A pending fold keeps the old generation charged to the cap, and the
   fold's own lookups map, verify and can fail the successor (T2).
3. The walk, as written, loses every mark below a page that holds only branch nodes (T3).
4. A fork child reports the parent's reads through a retired generation still in the ring (T4).
5. The mark bitmap is both heap memory and an `mmap(MAP_FIXED)` target. The charge, the minimum
   and `mark_charge` depend on which (T5, T6).
6. Smaller gaps in text and tests: `detent_usage`'s wait, `statuses[DETENT_OK]`, the size rules of
   the new structs, a carried name that cannot be allocated, and close (T7-T11).

None needs a new approach, so this is not a REJECT. T1 and T2 need a choice of fold mechanism, and
T1 lists the options.

### Evidence I relied on

- Quest model job `261124-112726-6e7cf6` (light, exit 0) at `Detent:fa7e5fba`:
  `tests/detent_reader/usage_claim_model.sh` under TSan. I reused it and did not rerun it.

### What I checked myself

Jobs at `Detent:fa7e5fba`; scripts in `.scratch/detent/review-observability-core-design/`.

- `261124-115748-77e6af` (light, exit 0, TSan, no report): `run_r3_model.sh` builds
  `r3_fold_model.cc`. It models the core's ring of four slots with lock-free striped pins (the Dekker
  pair of `core.cc:350-364`, the freer of `:221-232`, the refresh of `:454-470` and `:621-639`), and
  adds D3's fold, budget, pending fold, carried names and fork clear:
  - Stress: 8 lock-free readers and 300 reordering, deleting publishes (budget 8). 524 keys read
    OK, 524 reported, 18 carried.
  - Observer: 1,500 checks that a read which returned OK before `detent_usage` began is reported,
    through folds. 0 missing.
  - A mark on a retired, pinned generation is reported meanwhile and folded after the unpin. A
    reader parked before its pin across four publishes retries and marks.
  - Budget: 100,000 knobs read once, budget 1,024, six publishes. Successor lookups per publish:
    1,024; 100,000 (96,928 unbudgeted); 2,048; 100,000; 2,048; 100,000. With `detent_usage`
    between publishes: 1,024 per publish, and 98,976 on `detent_usage`'s caller.
  - Fork: clearing only the attached generation leaves 5 of the parent's keys in the child's
    report. Clearing every generation of the ring's views leaves 0.
- `261124-120044-d2524f` (light, exit 0, TSan, no report): `run_r3_rows.sh` builds
  `r3_rows_stress.cc`. D3 Rows (the quest model's `Table` without its window hooks) ran 16
  threads, 20 rounds each at `usage_slots` 1 (4 names), 256 (300 names) and 4,096 (1,024 names).
  Each round: one row per name, every sample counted once in a row or in the overflow row, and
  `overflowed` set exactly when the overflow row counted.
- `261124-122940-4f2237` (light, exit 0): `run_r3_cap.sh` builds `r3_probe_cap.cc` in the core,
  with its test hooks, on 4 KiB pages:
  - At exactly the minimum, every store opens. Stores of 2,000 and 20,000 knobs (one pack of
    45,346 or 471,968 bytes) read every key. Stores of 60,000 and 100,000 knobs read 15,564 and
    11,128 keys and answer `DETENT_NO_MEMORY` for the rest: their near-full packs (1,046,533 and
    1,047,901 bytes) charge 1,055,304 and 1,055,312 bytes each on their own, 168 and 176 over
    the minimum.
  - Held: at a cap of one generation and a half (2,152,704 bytes, as in `retained`), with
    revision 1 kept mapped by a parked reader, revision 2 reads 15,564 of 60,000 keys and
    answers `DETENT_NO_MEMORY` for 44,436. Once revision 1 is freed, it reads all 60,000.
  - A 10-knob delta wrote one 68,952-byte pack, whose marks would be 1,078 bytes.
- `261124-115751-802aaa` (light, exit 134) aborted on a 200,000-knob store, past the writer's
  JSON limit of 100,000 knobs (`include/detent/json_schema.hh:19`). That was my error; the rerun
  above replaces it.

### The lifecycle from the code

| Event | Code | Generations | What D3 adds, and where it breaks |
| --- | --- | --- | --- |
| Open | `core.cc:735-788`: cap at least the minimum (`:747`), fork handlers | none attached | rows, stripes; the minimum (T6) |
| First lookup in a namespace | `:681-705`: one thread attaches; the others `refreshing.wait` (`:705`) | attached | they also wait out a fold (T1) |
| Lookup, sequence unchanged | `:670-671` | | mark, sample; never waits |
| Lookup, sequence moved | `:672-679`: refresh if `refreshing` is free, else answer stale | | the refresher runs the fold (T1) |
| Commit | `:621-639`: install, then `free_retired((s+1)%4)` (`:638`) | old one freed if unpinned, else retired and pinned | fold before the delete; past 1,024 keys, pending (T1, T2) |
| Next refresh | `:462`: `free_retired(s%4)` | retired views freed once unpinned | resumes the pending fold |
| Refused | `:464-470` | nothing built | |
| Kept, re-added, slot reused, dropped, attach failed | `:523-524`, `:536-538`, `:557-573` | shared, new, or kept attached | no retire, or a fold into the new one |
| `detent_refresh` | `:833-845`: `DETENT_OK` at once if another thread refreshes (`:836-837`) | | finishes pending folds; D6 cites it wrongly (T7) |
| `fork` | `:251-287`: prepare waits for every `refreshing` (`:254-255`); the child zeroes every pin of every slot and calls `child_after_fork` on every generation of every ring view | retired views stay in the child's ring | D3 clears attached ones only (T4); `fork()` waits out a fold (T1) |
| Close | `:850-855`, then `:196-201`: `free_view` of slots 0 to 3 in index order | all freed | a fold here would read a freed successor (T11) |

A retired view that a lookup pinned at the commit's free pass lingers until the next
`refresh_locked`: the next publish a lookup finds, a first attach, or `detent_refresh`. With usage
on, every generation with more than 1,024 marked keys lingers the same way, pinned or not (T2).

### The five questions

**1. Marks and the fold.** `ever_read` is exact within a generation, and across publishes,
reorders, deletes and retired, pinned generations: the model above, with lock-free pins under TSan,
reports exactly the keys read OK. It is not exact for a fork child (T4), nor, as the walk is
written, for a mark below a page of branch nodes (T3). Close drops everything, which is right, but
no test fails if a fold runs there (T11).

The fold runs on the holder of `refreshing`: the application lookup that found its namespace's
sequence moved (`core.cc:672-679`), a first attach's winner, `detent_refresh` and `detent_usage`. A
warm lookup never waits on it. Lookups whose sequence has not moved answer at `:671`, and moved ones
answer stale (`:675`). Four callers wait: the refreshing lookup itself, first attaches of other
namespaces (`:705`), `fork()` in any thread (`:254-255`), and `detent_usage`.

The budget does not bound those costs. Freer passes run only inside `refresh_locked`, so a fold past
1,024 keys is not spread out. It is finished unbudgeted at the next publish. Every second publish
therefore makes about R successor lookups (model: 100,000 of 100,000, 96,928 unbudgeted).

**Worst case:** a 5M store with every knob read, and two publishes with no `detent_usage` or
`detent_refresh` between them. The second publish's first lookup in that namespace makes about 5M
successor lookups, 3.8-7.0 s by D3's estimate. Meanwhile first attaches, `fork()` and `detent_usage`
wait, and other namespaces answer stale. At R = 4,096 the cost is 10-91 ms at every second publish.
The fold refuses no publish; it delays the handle's next view by its own length. reader.md T2 holds
a first read, from open to the first value, under 1 ms. Nothing bounds the lookup that refreshes,
and the owner has not accepted this cost (T1).

**2. Concurrency.** The mark is right. Test-before-set is idempotent, and a lookup that returns OK has
set or seen its bit. The freer sees every mark of a generation it frees: the reader's release
`fetch_sub` of its pin and the seq_cst loads in `sum()` order the mark before the walk. My model
checks this with the core's lock-free pins under TSan, including a reader parked before its pin and
one parked on a retired generation. Rows by name and the overflow row are right under 16 threads
(job `d2524f`). The quest model pins its readers under a mutex (`usage_claim_model.cc:185`), so it
cannot interleave a freer with lock-free pins. The core's `fold_concurrent` must, and my model is a
pattern for it.

**3. Mapping cap.** The verified bitmap is `malloc`ed through `StorageAllocator` and charged
`sizeof(Header)` plus its payload (`verified_pages.hh:15-16`, `allocator.hh:32`). So 16,384 bytes
per full segment, plus a header, is the right charge for a heap mark bitmap. But D3 Fork remaps the
bitmap with `MAP_FIXED`, which needs a page-aligned mapping of its own and a page-rounded charge
(T5).

Raising the minimum by 16,384 keeps its convention: payloads only, "one full pack and its bitmap"
(reader.md:130). The cap also counts every page-table allocation (reader.md:130, `core.cc:57`), so
the minimum has never admitted a full pack: even a 1,046,533-byte pack alone charges 1,055,304
bytes, 168 over it. A caller at today's exact minimum (1,055,136 bytes) opens, and reads only the
keys whose path avoids a near-full pack: every key of the 2,000- and 20,000-knob stores, but
15,564 of 60,000 and 11,128 of 100,000, the rest `DETENT_NO_MEMORY` (probe `4f2237`). After the
change its `detent_open` returns `DETENT_INVALID_ARGUMENT`, whether usage is on or off. That ABI
change is the owner's, and the project is greenfield, so I raise no compatibility question.

**4. ABI.** Adding `ever_read` between `name` and `samples`, with `DETENT_ABI_VERSION` staying 1, is
coherent: nothing has shipped, D158 accepted the shape just before it, and `reader.hh:37` checks for
equality. The growth rules are not stated. D6 has one only for `detent_open_options` (zero means
the default), and none for the out-struct `detent_usage_totals` or the callback records (T9).
`statuses[DETENT_OK]` contradicts G1 (T8).

**5. Gates.** G1 counts events with seeded stripes, so it decides at any level. G2 decides against
10 ns at 8 threads: each case's threshold is D48's ns over that case's own off median, load only
makes it stricter, and a strict run that does not pass hands over to one exclusive run (D135).

Four P4 rows have no test that can fail: `fork_child` (T4), close (T11), `mark_charge` (T6), and
the fold rows on a store too shallow for T3. Every other row's test can fail, including `cycle`
(a fixed 64 samples 16 names, below its 512) and `fresh_handle` (30-160 of 10,000, expected 78.7,
standard deviation 8.8). No row tests that answers do not depend on usage (T2).

### Round-2 findings

| ID | Status | How it is shown |
|---|---|---|
| R1 | fixed | D3 prices the fold and D10 lists the surface. The price is what shows T1. |
| R2 | fixed | Names are probed only on the sampled path, and G1 states 2.5 probes per sample at `usage_slots` 256. Job `d2524f`: 256 rows, sums exact, `overflowed` 1. |
| R3 | fixed | G2 uses D48's ns over each case's off median, with an exclusive fallback. |
| R4 | fixed | `cycle` is seeded and asserts 512 of 1,024 names within 64 cycles and all within 1,000 (round 2's probe: 631-662, and all by 478-500). |
| R5 | fixed | D3 Rows item 4 re-examines the slot. Job `d2524f` covers it at `usage_slots` 1 and 256. |
| R6 | fixed | 10,000 handles, 30-160 sampled. |
| R7 | fixed | D10's T4 paragraph. |
| R8 | fixed | `callback_reentry`. |

### Findings (round 3)

A bare "line N" is observability.md's at `Detent:fa7e5fba`.

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| T1 | yes | defect | The budget does not bound a refresh, and the worst case is not stated. Freer passes run only inside `refresh_locked`: when a lookup finds its sequence moved, at a first attach, and in `detent_refresh`. So a fold past 1,024 keys is not "spread over passes" (line 124). It pends until the next publish, which finishes it unbudgeted. In steady state, every second publish makes about R successor lookups, where R is every knob read since open, on the application thread that finds the publish. First attaches, `fork()` and `detent_usage` wait meanwhile. Worst case (question 1 above): 3.8-7.0 s at 5M. The A2 fold-timing check has no threshold and leaves out R = every knob, so it cannot fail. | Job `77e6af` budget lines; `core.cc:254-255, 462, 638, 672-679, 705`; observability.md:112, 124, 328, 344 | Choose a bound and put it to the owner with its numbers. Options: (a) at the free, add the generation's marked names to the carried set and free the generation at once, with no successor lookups. A publish then costs the walk of that generation's own marks and a name insert per mark (the walk alone about 0.3 s at 5M if every knob was read in that generation, estimate), and the heap holds one name per knob read since open: core heap, which reader.md T4 holds to 1.5 MiB (0.2-0.5 GB at 5M with every knob read, estimate). (b) Keep the fold and drop the budget, which in steady state defers only 1,024 to 3,072 keys (the model) and causes T2's retention; state the per-publish cost as Limit 10. (c) Let pending folds really spread across refreshes, which retains more generations under the cap. Then give the A2 check that bound as its threshold, at 100K and 1M with R = every knob. Make `fold_budget` publish twice with nothing between and assert the largest pass through `Stats`. It fails without the bound: the model's 100,000-lookup pass. |
| T2 | yes | defect | "The answer of a lookup never depends on usage" (line 279) is false, three ways. (i) A pending fold keeps the retiring generation's pages charged to the cap until the next publish, `detent_refresh` or `detent_usage`. At a cap of one generation and a half (the core's own `retained` test), the successor's reads then answer `DETENT_NO_MEMORY`. An off handle that no lookup pinned at the commit frees the old generation there (`core.cc:638`) and reads whole. (ii) The fold's successor lookups map and verify segments the application may never read again, under the same cap. (iii) A fold lookup that fails verification sets the generation-wide `failed` (`lazy_page_access.cc:263-265`), after which every lookup in that namespace fails (`:234-235`). With usage off, a page the application does not read again is never verified. Limit 12 also misses the names a refused fold lookup carries. | Probe `4f2237` held lines (44,436 of 60,000 `DETENT_NO_MEMORY` while revision 1 stays mapped, 0 once it is freed); `test_reader_core.cc:475-519`; `lazy_page_access.cc:168, 234-235, 263-265`; observability.md:111-112, 124, 279, 346 | Decide it with T1. Option (a) removes (i)-(iii). Under (b) or (c), fold only into successor pages already verified and carry the rest by name, which removes (ii) and (iii). Then state what remains (the bitmaps' 1/64 of the cap is itself a difference). Add `usage_answers`: one script of lookups and publishes, run on an on and an off handle at the same cap and including a corrupt successor page that only a fold would read, gives the same statuses and values. It fails without the fix on (i) and (iii). |
| T3 | yes | defect | The walk, as written, enters "only pages whose mark range has a set bit" (line 110). Marks are set only at definition bodies (D3 Marks). A leaf holds only its key's suffix; the rest is the branch path from the root, through external children in other pages (`paged_read_state.cc:69-81, 103-105`). A page of branch nodes has no marks, so the walk stops above every mark beneath one. The quest model folds over a flat map, so it cannot catch this, and neither can a store whose root page holds the definitions read. `detent_usage` uses the same walk. | `paged_read_state.cc:69-81, 103-105`; `paged_layout.hh:25`; observability.md:97, 110, 186 | Enter a page when it is verified: by line 110's own premise, every marked body's page and its ancestors are. Name the bodies whose bit is set, and price the walk per verified page. Run `publish_reorder`, `ever_read` and `fold_concurrent` on a store deep enough that the definitions read sit at least two pages below the root (100K knobs). Each fails without. |
| T4 | yes | gap | D3 Fork clears "every attached generation's mark bitmaps" and the pending folds' marks (line 126). The ring also holds retired views: pinned when `fork()` ran (as reader.md T6's parked lookup is), or unpinned but not yet freed. The child zeroes their pins (`core.cc:278-279`), so its next refresh frees them, folds their marks and reports the parent's reads. `fork_child` forks in neither state, nor with a fold pending, though line 321 claims "a fork child with one pending" is covered. | Job `77e6af` fork line (5 parent keys; 0 when every ring generation is cleared); `core.cc:275-287` | Clear every generation of every ring view; `core.cc:280-282` already visits them, and `PageTable::child_after_fork` is where the surface table puts it. Say whether the child frees a pending generation or only clears it. `fork_child` forks with a reader parked at `Window::Pinned` on a retired generation that holds marks, and again with a fold pending, and the child reports none of the parent's reads. Fails without. |
| T5 | yes | defect | The mark bitmap is specified two incompatible ways. D10 allocates it "beside `VerifiedPages` from the same account" (lines 271, 275), which is `malloc` through `StorageAllocator`. D3 Fork replaces it in the child with `mmap(MAP_FIXED)` (lines 126, 271), which needs a page-aligned mapping of its own. As heap memory, the child must clear it by writing, which copies the parent's pages: D3's "no copy" fails, at about 5 MB per child at 5M (302 segments, estimate). As a mapping, the charge is page-rounded: a 10-knob delta wrote a 68,952-byte pack whose 1,078 bytes of marks would take a whole page, 4 KiB here and 16 KiB on Apple Silicon (CI's macos-arm64), 15 times their size there. On 64 KiB pages a full segment's bitmap costs 64 KiB, so 16,384 and a compile-time minimum are wrong there. | `verified_pages.hh:15-16`; `allocator.hh:32`; `generation_mapping.hh` (`rounded_mapping_bytes`); probe `4f2237` delta line | Pick one and state its charge: heap memory with the child's copy stated, or one anonymous mapping per `PageTable`, sized at attach for `root.segments` bitmaps and charged once. Make `mark_charge` assert the on-minus-off charge exactly, for a full segment and a small one. |
| T6 | yes | defect | `mark_charge`'s last clause, "a cap of exactly the new minimum reads one segment" (line 287), cannot decide the 16,384. The minimum counts a full pack and its bitmap payloads only, while the cap also charges every page-table allocation (reader.md:130, `core.cc:57`). At exactly the minimum, near-full packs of 1,046,533 and 1,047,901 bytes charge 1,055,304 and 1,055,312 bytes on their own, so they never map, and the 60,000- and 100,000-knob stores read only 15,564 and 11,128 of their keys; only a store of one smaller pack reads every key. D10's "one full segment and its bitmaps" (line 275) says the same thing. | Probe `4f2237` minimum lines; `core.cc:59-60, 747`; `lazy_page_access.cc:101-105, 168, 271` | Either name the minimum what reader.md does, an open-time floor ("open attaches nothing"), and drop "reads one segment"; or raise it to cover a full segment's first lookup (the segment, both bitmaps, the segment table, the segment object, and the headers). Either way it is the owner's ABI. Then `mark_charge` asserts the measured on-minus-off charge per segment (T5). |
| T7 | yes | defect | D6 item 3 says `detent_usage` "takes `refreshing` (waiting as `detent_refresh` waits)". `detent_refresh` does not wait: it returns `DETENT_OK` at once when another thread holds `refreshing` (`core.cc:836-837`). Whether `detent_usage` waits (up to T1's worst case) or returns is undecided, and no test pins it. | `core.cc:833-837`; observability.md:186 | Say which: wait as a first attach does (`core.cc:705`), or return a status. `usage_during_refresh` calls `detent_usage` while a refresher is parked at `Window::Refreshing` and asserts that behavior. Fails without. |
| T8 | yes | defect | `statuses[DETENT_OK]` is undefined. D6 calls `statuses[16]` "exact lookups by detent_status", and D1 "exact totals per status" (lines 37, 174). But D2 keeps totals "only for statuses other than OK" (line 76), and G1 allows "one read-modify-write per sample and no other" (line 82), so an exact OK count cannot exist. | observability.md:37, 76, 82, 174 | Say `statuses[DETENT_OK]` is 0, or the sampled estimate (`period` × OK samples), and assert it in `fallbacks`. |
| T9 | yes | gap | The size rules of the three new structs are unstated, and ABI 1 freezes them when they first ship. D6 item 6 covers `detent_open_options` only. For the out-struct `detent_usage_totals`, the library must write no byte at or past the caller's `size`, and the result of a NULL `totals` or a `size` below the first version must be stated. For `detent_knob_usage` and `detent_view_usage`, the library sets `size`, and a caller reads only fields below it. | observability.md:166-178, 189; `core.cc:740-746` (the open-options rule) | State both rules in D6. Add `usage_abi`: one totals struct sized to end after `statuses` with canary bytes behind it, and one larger than the library's. Nothing past either size is written, and each record's `size` equals the library's. Fails without. |
| T10 | yes | gap | The freer adds carried names, and the freer is `noexcept` (`core.cc:202, 221`). An allocation failure there terminates the process or, if caught, drops a read with nothing to say so, and D6 item 2's absence rule then lies. The class sweep (line 321) has no row for it. | `core.cc:202-232`; observability.md:111, 185, 321 | Say what happens: keep the generation pending and retry, or count the loss and flag it in the totals, excepting it from item 2. Add `fold_allocation_failure` through a failing-allocation seam. Fails without. |
| T11 | yes | gap | Close's test is "the suite under ASan" (line 319). It fails only if some test closes with a fold pending or a retired generation in the ring. None is named, though line 321 claims "close with one pending" has a test. `~detent_reader` frees ring slots in index order (`core.cc:196-201`), so a fold reached from `free_view` at close would look up in a successor already freed. | `core.cc:196-215, 850-855`; observability.md:319, 321 | Add `close_pending`: close with a fold pending and a retired, unfreed generation. ASan and LSan are clean, and a counting seam shows no successor lookup at close. Fails without. |
| U1 | no | gap | Every thread on a stripe shares its countdown and its xorshift state, but D2 says "relaxed load and store" for the countdown only. Both must be relaxed atomics, or TSan reports a race. `concurrent_usage` runs at period 1, so it never reloads. | observability.md:49-51, 314 | Say both are relaxed atomics, and run `concurrent_usage` once with the real reload. |
| U2 | no | gap | `detent_usage` buffers every record before its callbacks: several hundred MB at 5M with every knob read (estimate). Its status when that allocation fails is unstated. | observability.md:186 | State the status (`DETENT_NO_MEMORY`) and the peak. |
| U3 | no | nit | The overflow record's `ever_read` is unstated. | observability.md:166-167 | 0. |
| U4 | no | gap | The surface table changes what `PagedReadState::definition` returns, but lists none of its callers (`view.cc:71, 80, 99` and the detent_local tests). The quest's file set lacks `paged_page_access.hh`, `paged_read_state.hh`, `paged_read_state.cc` and `tests/detent/test_one_knob_differential.cc`; the doc says the set gains them before any edit. | observability.md:266-273; state.json | The lead adds them. |
| U5 | no | nit | `lazy_page_access.cc:69` (line 275) is `:70`. | `lazy_page_access.cc:70` | Fix the cite. |
| U6 | no | gap | An off handle allocates no bitmap but is still refused below the new minimum. | observability.md:275 | The owner's call; the doc already says so. |
| U7 | no | gap | `campaign status` lists A2 as met by job `261124-112726-6e7cf6`, the model run. A2 is the lookup cost with the bits on, which needs the build. | `campaign status observability-core` | The lead clears it. |
| U8 | no | nit | reader.md:123 keeps a segment object across generations, but the landed core builds a new `PageTable` per `Generation` (`core.cc:110-123`). The fold and the fork clear assume the landed shape. | `core.cc:110-123` | Say so in D3. |

### Not run (round 3)

- No strict or exclusive job (as instructed), so nothing here measures a lookup or the fold. Every
  cost in T1 is D3's own estimate; the alternation between passes is the model's.
- The fold is not built. T1-T4 rest on the model and the code, not on the core.
- The host has 4 KiB pages, so T5's 16 KiB and 64 KiB figures are arithmetic, not measured.

## Re-review at Detent:0e096e0e

This round checks the rewrite of `docs/design/observability.md` into one paged-core design. The
round-1 findings are below, unchanged, as reviewed at `Detent:1fbd4cd9`. The claim protocol is now
correct: F1, F2 and F3 are fixed, and I checked each with a model I wrote from the doc's text, not
from the quest's own model. F7 and F8 are fixed as text. Three of F4-F6 are fixed only in part, and
each leaves a blocking gap, all text and test-spec fixes:

1. Index sizing makes knobs past the bound probe on every lookup, which contradicts G1 and the
   `overflow` test.
2. The `cycle` test cannot pass.
3. G2's 2% at 8 and 64 threads loosens the owner's 10 ns ceiling.
4. The (b) side of D10 omits the cost of exactness across publishes.

### Evidence

- Probe job `261124-110456-c8a8ef` (light, exit 0, TSan, no report) at `Detent:0e096e0e`, run with
  `.scratch/detent/review-observability-core-design/run_v2.sh`. It runs:
  - `claim_model_v2.cc`: D3 as the doc states it, including its index sizing
    2^⌈log2(2·min(usage_slots, records))⌉, with forced windows.
  - The quest's `tests/detent_reader/usage_claim_model.sh`: "all outcomes as designed", 10 of 10.
    This reproduces `261124-104834-6ab99d` at this commit.
- The D10 memory table, recomputed from its stated sizes (row 288 B, set 4 B, slot 16 B, 16 KB per
  segment, 63.15 B of segment per knob): 1.34, 1.34 and 1.34 MB for (a) at the default against
  1.33, 2.21 and 6.16 MB for (b); exact (a) is 34.0 MB, 329.9 MB and 1,775 MB. Every figure matches.
- Earlier job `261124-105836-c2f298` failed to compile my model (my error). It decides nothing.

### Round-1 findings

| ID | Status | How it is shown |
|---|---|---|
| F1 | fixed | The row (or the flag) is published before the address. In my model, an *unsampled* read returns while the winner is parked after its name or after its address, and the row is visible ("as expected", both windows). |
| F2 | fixed | An insert-only name set published from private rows. With a claimer parked at RowWritten, NamePublished or AddressClaimed: one row, and the second claimer progresses (both models). Equal hashes give two rows (quest model). |
| F3 | fixed | Fallbacks claim no row. Quest model `spray`: 64 missing names, then a defined knob keeps its row with no overflow. Limit 10 states the lost per-name `missing` detail. |
| F4 | partly | Both sides' arithmetic is right. The (b) side is incomplete: R1. |
| F5 | partly | The table is rebuilt from `core.cc`. The re-add, index-refused, `detent_revision`, view-state, `kept_generation` (`Stats::claims`) and `fresh_handle` rows are now failable. R2 and R4 remain. |
| F6 | partly | G1 counts events, so it decides at any level. G2 is R3. |
| F7 | fixed | One design. 0 means the default everywhere, `detent get` uses `DETENT_USAGE_OFF` (line 357), and no bitmap-position text remains. |
| F8 | fixed | D6 states the maximum (8,388,608 → INVALID_ARGUMENT), that the ABI version stays 1, `CLOCK_MONOTONIC` through `clock_gettime`, the off-handle results, and callbacks run with no pin. |
| N1-N6, N8 | fixed | D4 (`switch_forced`, `get_as<bool>`), Limits 3 and 8, D3 Fork (anonymous remap), the D6 totals, D6 item 1. |
| N7 | open | The file set in state.json still lacks `src/detent_local/view.cc`, `tests/detent_local/test_evaluation.cc` and `tests/detent/test_one_knob_differential.cc`. That is the lead's to fix. |

### Q2, the mechanism, for the owner

The (a) column is right. The (b) column is right for one generation. What follows is judged from the
code; nothing is measured, because neither mechanism is built.

- **Hot path of (b).** `PageTable::State::resolve` already has the leaf's `Segment` in hand
  (`lazy_page_access.cc:237`). If `resolve` returns it, and `PagedReadState::definition` passes it
  out with the body, a warm hit is offset = body − segment base, then one test of the bitmap word.
  That is one load after the walk, with no multiply and no probe chain. (a)'s hit is a multiply, then
  1.07 expected probes at the default's load of 1/8. Scattered knobs more than 4 KB apart touch one
  line each under either mechanism. So **(b) is no slower than (a) on a warm hit**, and probably
  slightly cheaper.
- **Concurrency of (b).** It is simpler. The mark is an idempotent test-before-set `fetch_or` that
  the reader itself performs, so there is no claim protocol, no visibility window, no spares and no
  half-full rule. The segment mapping is stable for its `PageTable`'s life. A fork child remaps the
  bitmaps as it does the indexes.
- **Surface of (b) in src/detent_local.**
  - The `PagedPageAccess::resolve` interface (`paged_page_access.hh`) and its implementers
    (`lazy_page_access`, `prepared_reader.cc`) and test users (`test_lazy_page_access`,
    `test_lazy_page_bitmap`, `test_dense_reads`, `test_dense_fullbuild`).
  - `PagedReadState::definition`, and the `Segment` constructor.
  - The mapping cap: if the bitmap is charged to it, `core.cc:59-60`'s static_assert moves the ABI
    constant `DETENT_MINIMUM_MAPPING_BYTES` (`reader.h:32`) by 16 KB. If it is not charged, it is an
    uncapped 1/64 of mapped bytes, 8 MB at the 512 MiB default.
- **Exactness of (b) across publishes.** (b) is exact for every definition within a generation. A
  new generation has a new `PageTable`, so new bitmaps. Keeping "read since open" across a publish
  needs D10's fold: name each retiring bit by walking that generation's verified pages, then look
  each name up in the successor. That costs about R cold lookups, each re-verifying its pages on
  first touch, on the lookup thread that holds `refreshing`. The alternative is to put the retiring
  names into rows, which bounds (b) by `usage_slots` across publishes, as (a) is. The doc names the
  fold but neither prices it nor says which of the two (b) takes. Its "R = every knob: 6.2 MB" is a
  single-generation figure.

Plainly: within a generation (b) is exact, 288 times cheaper than an exact (a) at 5M (6.2 MB against
1.78 GB), and no slower on a warm hit. At the shipped default it costs more than (a) (6.2 against
1.34 MB at 5M), and it reaches src/detent_local. Its exactness since open, across publishes, costs a
fold that the comparison leaves out. If the owner holds Q7, (b) is the right mechanism once the fold
is priced. If the owner accepts the bound, (a) is.

### Q4, the gate

G1 is exact counts and decides at any level. G2 decides only when a run is valid (A/A within
±0.5%). Nothing shows a strict run can be valid. The strict screens the doc cites already alternated
on and off per block of 64, and a ratio cancels only load that stretches both arms in proportion. The
validity rule makes a false pass impossible, but it can leave the gate undecided forever. At 64
threads, a light run at nice 19 on shared CPUs is unlikely to be valid. So: G1 decides at light, G2
at strict if valid, and G2 needs an exclusive fallback (R3).

### Findings (re-review)

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | gap | D10's (b) states "exact for every definition" and 6.2 MB with every knob read, but that holds per generation. Across publishes, (b) needs the fold (R cold lookups on the thread that holds `refreshing`, each re-verifying pages in the new `PageTable`), or it falls back to rows and to (a)'s bound. The fold is not priced, and (b)'s src/detent_local surface is not listed: the resolve interface and its implementers, and the mapping cap with `DETENT_MINIMUM_MAPPING_BYTES`. The owner chooses on this table. | `lazy_page_access.cc:237`, `paged_page_access.hh`, `core.cc:59-60`, `reader.h:32`; observability.md:245,257 | Price the fold (an estimate from dense-pages' first-touch cost is enough) or choose the rows fallback. List the surface. State plainly that (b) is no slower on the warm hit (Q2 above). |
| R2 | yes | defect | Knobs past the bound are never claimed. With records above `usage_slots`, the index has 2·`usage_slots` slots and stops claiming at half, which is exactly when the table fills. So every knob past the bound probes to an empty slot on every lookup and never "holds the overflow row". G1 ("the knobs past the bound take the same counts ... their index slots hold the overflow row") and the `overflow` row ("probe at most 1.25 times per hit") are false at the doc's sizing. The quest's `full_table` sizes its index at 8 for a 2-row table, which hides this. | Probe `261124-110456-c8a8ef`: `usage_slots` 256 with 1,024 knobs gives 0 of 768 past-bound knobs holding the overflow row, and 1.91 probes per past-bound lookup | Either size the index above the table, so that table-full claims get slots (4·min, or keep claiming to 3/4), or state the past-bound cost: an unsuccessful search at load 1/2, about 2 probes, plus the `used` and flag loads. Then make G1 and `overflow` assert it, with the quest model sized as D3 says. |
| R3 | yes | gap | G2's ceilings move the owner's. Two percent at 8 and 64 threads is 15 to 19 ns on the S6 core's 767 to 961 ns lookups at 8 threads (D2's `none`), against D48/D61's 10 ns. And no level is named for when strict runs keep coming back invalid. | observability.md:58,83; PLAN.md D48, D61 | Make each case's threshold D48's ns divided by that case's own off median (the ratio still cancels load), or put 2% to the owner. Add: after N invalid strict runs, one exclusive run of the finished feature decides (D135). |
| R4 | yes | defect | `cycle` cannot pass. At period 64 with reloads in [1, 127], a name goes unsampled through 64 cycles with probability (63/64)^64 = 0.36. | Probe `261124-110456-c8a8ef`: 362 to 393 of 1,024 names unsampled after 64 cycles; every name sampled after 478 to 500 cycles | Seed it through the seam and assert coverage within 1,000 cycles. Or assert the distinct names sampled in k cycles against the fixed reload's 16. |
| R5 | no | defect | At the table's last row, a second claimer of the same name finds no row to take. It sets the flag and keeps the overflow row in its index slot, though the knob ends with a row. Absence is then reported as unproven, and that generation's samples land in overflow. This errs on the conservative side, but costs precision. | Probe `261124-110456-c8a8ef` `boundary`: 1 row, overflowed 1, 2 overflow samples | When `take` fails, re-examine the slot before setting the flag. Or state it under Limit 3. |
| R6 | no | nit | `fresh_handle` fails falsely with probability (126/127)^1000 = 3.7e-4 per run (0 of 1,000 sampled). | arithmetic | Use 10,000 handles, or the seeding seam. |
| R7 | no | nit | T4's "0.23 MB to spare" treats the existing core heap as zero, and T4 already exempts per-segment bitmaps, which (b)'s are. | observability.md:255; reader.md T4 | Restate it with the current core heap, and say whether (b)'s bitmap counts under T4's exemption. |
| R8 | no | gap | No lifecycle row tests a callback that looks up knobs or calls `detent_usage`, which D6 item 4 allows. | observability.md:175 | Add a `callback_reentry` row. |

### Not run (re-review)

- No strict or exclusive run (instructed), so G2's validity under strict is not shown.
- I did not measure the cost of (b) or of the fold; Q2 above is from the code.

## Round 1 (Detent:1fbd4cd9)

Scope: `docs/design/observability.md` "In the paged core" P1-P6 at `Detent:1fbd4cd9`, a design before
code. The claim index is a sound, simpler shape than segment bitmaps (core.cc only, names in hand, no
fold), so this is not a REJECT. But its claim protocol leaves a returned read invisible and its row
claim unspecified. The bound it offers the owner is misstated, and so is the memory comparison the
owner rules on. The lifecycle table misses events and names tests that cannot fail, and the gate has
no run level it can be decided at. Every fix below is text in P1-P6 plus the tests it names. No
blocking finding needs a different mechanism.

## Evidence I relied on

- No job at this commit establishes a criterion (`campaign status observability-core`: A1-A3
  pending). Nothing is built; nothing was reused.
- D10's exclusive and strict runs as recorded in the doc (`261109-141658-03e1a9`,
  `261110-021130-368d1b`, `261109-224833-fa1352`, `261110-021130-06137b`, `261110-021130-445051`)
  for what a strict run can resolve (F6). I read their recorded numbers; I did not rerun them.

## What I checked myself

Code read at `Detent:1fbd4cd9`: `src/detent_reader/core.cc` (whole), `src/detent_local/evaluation.hh`,
`paged_read_state.cc`, `lazy_page_access.{hh,cc}`, `borrowed_definition.hh`, `include/detent/reader.h`,
`reader.hh`, `testing.hh`, `read_counters.hh`; `client-local-source.md` D7; `reader.md` D10, T4;
`dense-pages.md` (reuse rule, page spacing); PLAN.md D47, D48, D61, D65, D68, D135, D143, D155.

Probe job `261124-102347-9ea29b` (light, exit 0, TSan build, no report): `claim_model.cc`, an
executable model of P2 steps 1-2 as written, with forced interleavings. Rerun with
`campaign job submit --quest observability-core -- .scratch/detent/review-observability-core-design/run.sh`.
Output:

```
sizeof index slot {atomic<uintptr_t>, atomic<uint32_t>} = 16 bytes (P2 says 12)
visibility(as written): after a returned lookup, usage shows 0 rows, overflow 0 -> READ INVISIBLE
visibility(fixed): after a returned lookup, usage shows 1 rows, overflow 0 -> read is visible
row claim (skip on a pending slot): 2 rows for ns:k
row claim (wait on a pending slot): 1 rows for ns:k; second claimer BLOCKED 200 ms while the first was parked
spray: 4 sampled missing names then one defined knob: defined has a row 0, overflow 1
```

Answers to the five questions:

1. **Claim index under concurrency.** It is not correct as written. Two rows for one knob happen if a
   claimer skips a pending row (F2). A reader can find the index slot claimed and the row not yet
   stored, and P2 lets it return with nothing recorded (F1). No row index is seen before its name
   once the orderings are stated (F2), but P2 states none. Generation swaps are sound: the index
   lives in its `Generation`. A reader pinned on the old view claims in the old index, and the
   new generation's first reader claims again. Both converge on one row by name, provided F2's
   protocol. ABA on a reused address cannot happen within an index. Segments are unmapped only by
   `~State` (`lazy_page_access.cc:110-126`; `release_one` has no other caller in the core). A
   losing mapping is destroyed before any reader sees it (`:197-231`). A freed generation's
   addresses that a later `mmap` reuses meet a new index. That holds only if the index is never
   keyed by namespace or carried across `Generation` objects; P2 should say so. Uniqueness of the
   address per knob rests on dense-pages' disjointness (N2).
2. **The bound.** "Absent means not read unless the overflow row is present" holds under the
   half-full rule: every path that does not record sets the flag. It fails transiently in F1's
   window, and only for names that hold a row; undefined names take rows too (F3). A caller can
   conclude:
   - A row means the handle looked the name up since open (or since fork, in a child), with any
     status. A row whose cells are all `missing` or `fallback_other` is not a definition read.
   - With no overflow row, a defined knob without a row was never read OK since open (after F1's
     fix).
   - A caller cannot conclude anything from absence once the overflow row appears. The flag is never
     cleared, so this lasts for the handle's life. Rows are never evicted, so publish churn alone
     reaches it (N4).
   - A row does not say which revision, which generation, or that the knob still exists.

   The comparison with segment bitmaps is unbalanced in both directions (F4).
3. **Lifecycle.** I derived the states and events from `core.cc`: open, first attach, publish (a new
   or kept `Generation`, core.cc:522-539), attach failure (kept), first-attach failure, dropped
   (watches the store word, :564), re-added (a new `Generation` unless at the same slot, :523), slot
   reused, refused publish and stale answers, view freed, fork prepare/parent/child, close,
   `detent_revision`, NOT_OPENED and the empty store. P4 misses several of them and gets one wrong
   (F5).
4. **Cost gate.** A hit is one load of the index slot plus the index base and mask, which sit next to
   the `Generation` fields the lookup already reads. That is `ever_bit`'s shape. The footprint is
   not: 8,192 slots × 16 B = 128 KB per generation, against D10's 12.5 KB bitmap at 100K. So D10's
   cost is not evidence for it. A knob the index cannot claim (past half full) probes on every lookup,
   and no gate row covers that. The gate cannot be decided at 5 ns without an exclusive run (F6).
5. **C ABI.** `usage_slots = 0` meaning the default matches `mapping_bytes` (core.cc:745-746) and
   `Reader`'s aggregate init (reader.hh:43, which zero-fills a grown struct). I recommend keeping it.
   `DETENT_USAGE_OFF = UINT32_MAX` is fine. `switch_forced` keeps the count at 9, but it sits at index
   4, not `wrong_type`'s 7, so "takes `wrong_type`'s cell" means the count only. Nothing is frozen
   yet, so the order is free. The doc still says 0 means off in five places (F7), and the header
   leaves frozen semantics open (F8).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A returned read can be invisible. P2 step 2 lets "a loser, or a reader that finds the row not yet stored" record nothing unless sampled. While the winner is between its address CAS and its row store, another thread's lookup of the knob returns OK. A `detent_usage` after that return shows no row and no overflow, which breaks "absent means not read". The window lasts as long as the winner is descheduled. D10's bit had no such window, because each reader set or saw its own bit. | Probe `261124-102347-9ea29b`, `visibility(as written)`: 0 rows, overflow 0 after the lookup returned. `visibility(fixed)`: 1 row. | A reader that finds the slot claimed and the row unstored finds or claims the row by name itself. This is unsampled and happens only inside the window. Alternatively, claim the row first and publish the index slot second. Test: `test_reader_usage` gains a `testing::Window` between the address CAS and the row store. Park the winner there; the second thread's lookup returns, then its `detent_usage` shows the row. It fails without the fix. |
| F2 | yes | gap | The row claim protocol is unspecified. "Hash, probe, compare names" does not say what a claimer does on a row that is claimed but not yet named, which happens when two winners (two generations, or a sampled `missing` beside a winner) take one name at once. If it skips, one knob gets two rows. If it waits, a parked claimer blocks other first reads. P2 also states no orderings: name before ready (release), the index's row stored after ready (release), acquire loads. It does not say what a winner that finds the table full leaves in its slot. That should be the overflow row, so the knob's hits stay at one load. The slot is 16 bytes, not 12: an 8-byte atomic plus a 4-byte atomic pads to 16. That makes the index 128 KB at the default, not 96 KB. | Probe `261124-102347-9ea29b`: skip gives 2 rows for `ns:k`; wait gives 1 row with the second claimer BLOCKED 200 ms; sizeof 16. | Specify wait on a pending row of the same hash. It is bounded by a copy of at most 200 bytes, happens on first reads only, and a fork child clears it. Then specify the orderings and the overflow slot, and correct the sizes. Tests: two claimers of one name, one parked mid-claim, end with one row. A test seam that forces equal hashes gives two names two rows. |
| F3 | yes | defect | The bound offered to the owner is wrong. A sampled `missing`, NOT_OPENED or other fallback lookup "finds or claims its row by name" (P2 step 3). So undefined names use up `usage_slots`, and a caller that looks up undefined names (built from input, or after a deletion) pushes defined knobs into overflow. "Exact up to `usage_slots` knobs" is really "up to `usage_slots` names looked up, defined or not". D2's "a sprayed name cannot fill the table" holds only for invalid names. Under D10 the table filling did not touch ever-read; here it does. | Probe `261124-102347-9ea29b`, `spray`: with `usage_slots` 4, four sampled missing names and then a defined knob, the defined knob gets no row and overflow is set. | Owner's choice of mechanism. The statement must be fixed whichever is chosen. Options: (a) keep the mechanism and state the bound as names; (b) fallback samples claim no row: they add to an existing row, or to the overflow row and the handle's counters, which narrows D1's per-knob `missing` cell; (c) a separate budget for rows without a definition. Test: `overflow` with missing names first. |
| F4 | yes | defect | P2's comparison, which the owner rules on, misstates memory on both sides. The claim index is not "in proportion to the knobs read". Rows are allocated at open: `usage_slots` × about 288 B, so 1.18 MB at the default whether a process reads 1 knob or 4,096. That alone is most of reader.md T4's 1.5 MiB core heap budget. The index is allocated at attach at 2 × min(`usage_slots`, records) slots, 128 KB per attached generation whatever is read. "An application wanting every knob raises `usage_slots`" costs, at 5M, 2^24 × 16 B = 268 MB of index per generation plus about 1.44 GB of rows, against the bitmap's 4.9 MB at most. The bitmap side is overstated: a per-segment bitmap can sit next to `VerifiedPages` in each lazily mapped `Segment` (lazy_page_access.cc:63-71), so it costs about mapped bytes / 64, in proportion to segments touched, not the 315 MB store. Its real costs are a naming walk over verified pages in `detent_usage` and D10's fold across generations. | `lazy_page_access.cc:63-71,110-126`; P2's own sizes; reader.md T4. | Restate both sides with these numbers, and add T4's new heap figure. The recommendation may still be (a): it changes core.cc only, carries names, and needs no fold. |
| F5 | yes | gap | P4's table does not match the code. (i) "Re-added with the same generation" at a new slot builds a new `Generation` (core.cc:523 keeps the previous one only at the same slot), so it gets a new index, not "keeps its index". (ii) Missing events: index allocation failure at attach (it must not fail the attach, or "the lookup's answer never changes with usage" breaks), `detent_revision` (it shares `lookup()`, core.cc:825: counted or not?), refused publish and stale answers, `detent_usage` on an OFF handle, and `detent_usage` beside a refresh that frees a view (it must pin). For the view records this quest takes in (P3): first attach, publish, and attach failure. After a failed attach, `seen` advances to the failed sequence while the old generation answers (core.cc:567-573), so `current − seen` reads 0 while stale. A dropped namespace watches the store word (core.cc:564), so its `seen` and `current` are store words. (iii) Two tests cannot fail without their property. `kept_generation` cannot, because at period 1 counts equal lookups whether or not a second claim happens. `fresh_handle` cannot, because "the first 64 lookups are not all sampled" holds with a fixed start of 1 or 64. (iv) F1's and F2's interleavings have only the `concurrent_usage` stress, not deterministic tests (review.md, concurrency claims). | core.cc lines cited; client-local-source.md D7. | Add the rows. Give `testing::Stats` a claim count for `kept_generation`. Make `fresh_handle` check the first lookup across many handles: sampled about N/64 times and never always. Add `testing::Window` values for the claim windows, with deterministic tests. |
| F6 | yes | gap | P5 names no run level. D2 says exclusive; quest A2 says strict. A strict run cannot decide a 5 ns ceiling. Strict screen `261109-141658-03e1a9` fails ten 1- and 8-thread rows that the exclusive confirm passes, and strict `261110-021130-368d1b` reads +11.8 where the exclusive runs read at most +10.4. The two handles are placed differently, so the gate needs its own A/A. No row covers knobs past the bound, which probe the half-full index on every lookup instead of taking one load. | observability.md D10 lines 217, 240; quest A2. | One exclusive run of the finished feature decides (D135 allows it). Strict runs are screens whose misses decide nothing. Add an off-vs-off pair per repetition, and a row with `usage_slots` below the scattered set (256 slots against 1,024 names). The lead rules on A2's level. |
| F7 | yes | gap | The doc holds two designs. P3 makes 0 the default, but D2's gate (line 87), D10 item 3 (line 223), D10 item 4 (line 242), Limit 8 and the build plan's last line ("`detent get` opens a handle with `usage_slots = 0`") say 0 is off. A contributor following the build plan gets the table on. P1-P2 also supersede D1's ever-read row, D3 items 1-2 (hash keys), D4's cells, D7 item 5, D8's bitsets, D10's shape, Limits 1, 2, 11 and 12, the OB1 and OB4 criteria (the fold, "past the cap the bitsets still hold every knob"), and the abstract. All of these still stand. | observability.md lines cited. | Once the owner rules on P2 and P3, rewrite those sections in place and fold P1-P6 into them (one design, present tense). |
| F8 | yes | gap | The header P3 freezes leaves its semantics open. It has no upper bound on `usage_slots`: at UINT32_MAX−1 the open allocates about 1.2 TB, so it should return INVALID_ARGUMENT above a stated maximum. It does not say whether `DETENT_ABI_VERSION` changes; reader.hh:37 throws on any difference. It does not name `attached_ns`'s clock: `now_ns` is `mach_absolute_time` on macOS (core.cc:71-84), not the `CLOCK_MONOTONIC` that D5 compares across processes. It does not say what `period` and view records are on an OFF handle. It sets no callback rules: whether a pin is held during callbacks (a slow callback holding one refuses publishes once the ring fills), whether a callback may look up, and that it must not close. | reader.h, reader.hh:37, core.cc:71-84,454-470. | State each in the header comment before OB1 freezes it. |
| N1 | no | gap | `switch_forced` breaks D4's "the value is a function of the arm and the revision". Int64 forced overrides can force different values per group, and D8 forbids recording which group matched, so OB4's exporter cannot derive the value. | evaluation.hh:27-33; D4, D8. | State it in P1; OB4 decides. |
| N2 | no | gap | One address per knob rests on dense-pages' physical disjointness ("no shared-page DAG", dense-pages.md:167), which the lazy reader does not check. A page aliased by a buggy trusted publisher would silently merge two knobs' rows and counts. | dense-pages.md:105,167. | State it as a Limit. |
| N3 | no | gap | The fork row clears 1.18 MB of rows and every index inside the child handler. That touches copy-on-write pages on every fork, which callers that fork and exec repeatedly pay each time. D7 item 4's anonymous remap (one `mmap` per region, zero pages on demand) avoids the copy. | P4 fork row; D7 item 4. | Measure it, or allocate rows and indexes with `mmap` and remap them in the child. |
| N4 | no | gap | Rows are never evicted, so deleted and renamed knobs accumulate. A long-lived handle reaches overflow through publish churn, not size, and then absence means nothing for the rest of its life. | P2 "Rows ... live for the handle". | State it as a Limit. |
| N5 | no | gap | D1's exact per-handle status counters and its refused, stale and failed-attach counters have no home in P3. Their sink, `Metrics` in `include/detent/client.hh`, is deleted. | D1; `include/detent/` listing. | Lead: in scope or not. |
| N6 | no | nit | `get_as<bool>` returns the fallback for values other than 0 and 1 (reader.hh:70-71), while the core records the answer as OK under its arm. | reader.hh:70-71. | State it in P3. |
| N7 | no | nit | The quest's file set (state.json, `feb065df`) lacks `src/detent_local/view.cc`, `tests/detent_local/test_evaluation.cc` and `tests/detent/test_one_knob_differential.cc`, which P1's signature change touches and P6 names. | state.json files. | Lead adds them. |
| N8 | no | nit | "A consistent row per knob" overclaims: a row's nine cells are independent relaxed counters, not a snapshot. | P4 `concurrent_usage` row. | Reword it. |

## Not run

- No strict or exclusive job (instructed), so no cost was measured. Q4's answer is from reading the
  code, plus D10's recorded runs for what strict can resolve.
- The model probe covers P2 steps 1-2 and the spray only. It does not model the half-full counter's
  races, fork, or `detent_usage` pinning.
- I did not check the bindings (OB5), the agent page (OB4) or the hook (out of the quest).
