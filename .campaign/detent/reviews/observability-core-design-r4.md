# observability-core review: design (round 4)

Commit: Detent:8ab994c7714f9abb86a9ff0257bedb073bd56a28
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Round 4 at Detent:8ab994c7 (whole design)

Scope: all of `docs/design/observability.md` and `tests/detent_reader/usage_claim_model.{cc,sh}` at
`Detent:8ab994c7`, checked against a lifecycle I built from the code before reading the doc's
(`src/detent_reader/core.cc`, `src/detent_local/lazy_page_access.cc`, `paged_layout.{hh,cc}`,
`generation_mapping.hh`, `paged_overlay_assembler.cc`). The branch changes only the doc, the model
and the journal, so every code cite is the branch's base.

D161 removes the fold, and with it T1, T2's fold paths, T4's leak of the parent's reads and T10.
T3, T6-T9 and T11 are fixed in the text, and the model shows T1, T3, T4, T7 and T11 with checks that
can fail. T5's one mapping per generation has no index (W1). A mark per generation, set by the
reader, is the right mechanism, so this is not a REJECT. Four things block:

1. The mark mapping has no index for a segment. The doc puts segment `s` at `s` × 16,384 in a
   mapping of `root.segments` × 16,384 and says `resolve` returns the index. A segment's only index
   is its slot in a table of `bit_ceil(2 × root.segments)` slots. Indexed by slot, marks land past
   the mapping on every real store I placed. Sized for the slots, the charge is 2 to 4 times the
   stated one. The stated "1/64 of segment bytes at most" is a floor, not a ceiling (W1).
2. A union of per-generation reports is a lower bound on reads since start, not equal to them. A
   read a generation answers after its last report is in no report. Limit 12 and D7 item 6 say
   "exactly" (W2).
3. The cost argument quotes D10's 8-thread column, where it passes. G2 names no store and no
   publish, so it can pass without measuring the footprint the segment-offset layout adds (about 8
   times D10's bitmap per store) or the re-marking and page faults after each publish (W3).
4. At a publish the new generation's marks are charged while the old one's still are. At a cap
   that holds one generation's marks but not two, every publish fails to attach: the on handle
   keeps answering the old revision, where the off handle follows. The doc says the on handle
   answers `DETENT_NO_MEMORY` (W4).

## Evidence I relied on

- Round-3 job `261124-115748-77e6af` (light, exit 0, TSan): lock-free striped pins (`core.cc:350-364`),
  the freer (`:221-232`) and the refresh (`:454-470`, `:621-639`), modeled from the code. Round 4
  changes neither the pins nor the freer, which "does nothing it did not do before"
  (observability.md:97), so that result stands for them. The quest model pins under a mutex
  (`usage_claim_model.cc:226, 231-235`), so on its own it would not.
- `campaign status observability-core` shows A2 met by `261124-144641-8cf85b`, my run of the model.
  It inherits a `--for A2` tag from an earlier identical job (round 3's U7). It is not A2's evidence
  (W5).

## What I checked myself

Jobs at `Detent:8ab994c7`; scripts in `.scratch/detent/review-observability-core-design/` (`run_r4_*.sh`,
named in its `.purpose`).

- `261124-144641-8cf85b` (light, exit 0, TSan): the quest's `usage_claim_model.sh`. Every check
  passes. They cover:
  - D161's empty record after a reordering, deleting publish.
  - T1-T4: a late mark on a pinned, retired generation, never reported.
  - Stress: 8 readers, a reporter and 50 publishes.
  - T3's walk and its negative (entering marked pages names none).
  - T4's fork clear and its negative.
  - T7 and T11.
  - The row claims.
  - R4's cycle and R6's fresh handles (85 of 10,000 first lookups sampled, 78.7 expected).
- `261124-145021-951f31` (light, exit 1, the defect shown): `run_r4_index.sh` builds
  `r4_segment_index.cc`. It copies `slot_count` and `identity_hash` verbatim and places the real
  segment identities of the dense stores in `.scratch/dense-cb/coldboot` as `publish` does. It reads
  their names, and `root.segments` at offset 40 of each root. Each store places at least one
  segment at or past slot `root.segments`:
  - 10k (both copies): its 1 segment sits at slot 1 of 2. The bitmap ends at 32 KiB; the mapping
    at 16 KiB.
  - The three 100K stores: 4, 4 and 3 of 6 segments sit at slot 6 or above, up to slot 14. The
    bitmap ends at 240 KiB; the mapping at 96 KiB.
  - 100k-after-c: 12 of 29 segments sit at slot 29 or above, up to slot 57. The bitmap ends at
    928 KiB; the mapping at 464 KiB.
  - Random identities: 49.7% of 1-segment tables, 81.4% of 2-segment ones and over 99.8% from 6
    segments up place a segment past `root.segments`. That is 49.7 to 70.5% of all segments.
- `261124-151617-bd1c3d` (none, exit 1, the bound refuted): `run_r4_store_sizes.sh` sets the stated
  charge, `root.segments` × 16,384, against each store's pack bytes. The ratios are 1/32.4 for 10k
  (one 530,991-byte pack), 1/63.2 for each 100K store (6 packs, 6,217,662 bytes) and 1/63.4 for
  100k-after-c (29 packs). Every store is above 1/64. The dense 100K stores have 6 segments, not
  the doc's 7.
- `261124-145024-b495aa` (light, exit 0, TSan): `run_r4_union.sh` builds `r4_union_model.cc`, the
  quest model's `Generation` and `Handle` with three scenarios:
  1. One report per generation, with k1 read on G1 after G1's report: reports `{k0}` and `{}`,
     whose union is `{k0}`. Keys read OK: `{k0, k1}`.
  2. G2 installed and retired between two reports: its read of k2 is in neither.
  3. The quest's fork check passes when the child clears only the installed generation.
- `261124-145026-52e63c` (light, exit 0): `run_r4_faults.sh` builds `r4_mark_faults.cc`. On 1,024
  fresh pages of an anonymous mapping (4 KiB pages here), the test load takes 1,024 minor faults
  and the `fetch_or` after it 0. That is 1.00 fault per first mark in an untouched page, and 0 on a
  warm pass. This is a count, not a timing.

## My lifecycle table

I built this from the code and the doc's mechanism before reading the doc's table. The last column
is what the doc's table (observability.md:269-306) gives for each row.

| Event | Code | What usage does | Doc row and test |
| --- | --- | --- | --- |
| Open, on or off | `core.cc:735-788` | rows, name set and stripes; nothing when off | `fresh_handle`, `off_handle`, `open_bounds` |
| First attach | `:575-584` | mark mapping made and charged in the `PageTable` constructor | `mark_charge`; the index (W1), an empty generation (W6) and a failed `mmap` (W7) are missing |
| First attach refused by the mark charge | `:579-582` | `DETENT_NO_MEMORY`; off attaches | `usage_answers` |
| First OK read in a generation | `resolve`, then `definition` | `fetch_or` at `s` × 2,048 + … | `ever_read`; `s` undefined (W1); first touch of a mark page faults (W3) |
| Later OK read | | one load | `gate_events`; footprint (W3) |
| Sampled read | | row found or claimed | `arms`, `claim_windows`, `claim_boundary`, `equal_hashes`, `cycle` |
| Table full | | overflow row | `overflow` |
| Publish, new generation | `:536-538, 567-573, 621-639` | new marks charged while the old ones still are | `publish_resets`; the peak is unstated (W4) |
| Publish whose new marks the cap refuses | `:547-548, 567-573` | on keeps the old revision with state failed; off follows | missing (W4) |
| Publish keeping the generation | `:523-524` | marks kept | `kept_generation` |
| Re-added, same id, new slot | | new `Generation` | `readd_same_generation_usage`; W4 applies |
| Slot reused, dropped, attach failed | `:557-573` | old generation kept | `slot_reused_views`, `dropped_view`, `entry_failure_view` |
| Refused publish (ring full) | `:464-470` | `refused` + 1 | `refused_counted` |
| Stale answer | `:673-675` | marks the installed generation | `stale_view` |
| Retired while pinned, late mark | | mark kept, never reported | `retired_marks`; lost by design (W2) |
| Freed | `:221-232, 638` | unmapped with its page table | `retired_marks` |
| Reports across publishes | | union of reports | missing; Limit 12 claims exactness (W2) |
| `detent_usage` beside lookups | | pin, walk, unpin | `concurrent_usage`, a subset check only (W14) |
| `detent_usage` during refresh, out of memory, re-entered, sized | | | `usage_during_refresh`, `usage_no_memory`, `callback_reentry`, `usage_abi` |
| Fork prepare | `:251-272` | nothing | `fork_child` |
| Fork child | `:275-287` | every ring generation's marks remapped zeroed; rows remapped; reseed | `fork_child`; remap failure, retired remap unobservable (W8); counters (W9) |
| Close | `:850-855, 196-215` | every mapping unmapped | `close_retired` |

## The five questions

1. **Lifecycle.** The doc's table covers every event of mine but four: the segment index (W1), a
   publish whose new marks the cap refuses (W4), an empty generation (W6) and a failed mark
   `mmap` (W7). Each listed row names a test that fails without its property, with two
   exceptions. `concurrent_usage` checks only that reports are subsets (W14). `fork_child` cannot
   observe the retired-generation clear (W8, job `b495aa` scenario 3). The model shows the
   per-generation record, the walk, fork, close and the no-wait path, each with a negative
   (`8cf85b`). It has one flat bitmap and no segments (`usage_claim_model.cc:141-221`), so it
   cannot show W1.
2. **T1-T11.**

   | T | Round 3 | Round 4 | Shown in the model |
   | --- | --- | --- | --- |
   | T1 | fold unbounded per refresh | no fold (D161); the freer does nothing new | yes, the D161 line; its `freer_work` clause is vacuous (W11) |
   | T2 | usage changes answers | the fold's paths are gone; the charge remains (Limit 11) | no; the publish case is new (W4) |
   | T3 | walk stops above branch pages | enters verified pages (:99) | yes, with the negative |
   | T4 | child reports the parent's reads | child remaps every ring generation's marks | yes for the installed one; the retired part is unobservable (W8) |
   | T5 | bitmap specified two ways | one mapping per `PageTable`, charged at attach | no: no segments in the model; the index is undefined (W1) |
   | T6 | minimum undecidable | 1,055,136 stays, as the open-time floor | not a model property; arithmetic holds (question 5) |
   | T7 | `detent_usage` wait undecided | never takes `refreshing` (D6 item 4) | yes |
   | T8 | `statuses[DETENT_OK]` | 0 (:158, :170), tested in `fallbacks` | text |
   | T9 | struct size rules | D6 item 7, `usage_abi` | text |
   | T10 | freer allocation failure | the freer allocates nothing | by removal |
   | T11 | close | `close_retired` | yes |

   Round 3's T5 offered "one anonymous mapping per `PageTable`, sized at attach for `root.segments`
   bitmaps" without naming the index. That shape needs an index the format does not carry, which
   is my miss as much as the doc's.
3. **Hot path.** The argument holds for the instructions, a relaxed load and a test with a
   `fetch_or` on a first read, over D10's dense 12.5 KB bitmap at 8 threads. It does not carry over
   to this layout, in three ways (W3):
   - It quotes only the 8-thread column. D10's `ever_sampled` is over the ceiling at 1 thread
     (+7.4; +7.0 to +7.7 confirming) and at or over it at 64 (+14.4; +9.2 to +10.4 confirming).
   - Marks at 1 bit per 8 segment bytes are about 8 times the dense bitmap per store. D10 measured
     a 12.5 KB bitmap; here the marks are 96 KiB at 100K and 4.72 MiB at 5M.
   - G1 and G2 run with no publish, while each publish starts a fresh mapping. Every knob's next
     read pays a `fetch_or`, and each untouched 4 KiB mark page takes a fault on the lookup's
     thread (`52e63c`).

   False sharing on warm hits is absent: after the first read nothing writes a mark line. It
   exists only in the burst after a publish: neighbors within 4 KiB of segment share a mark line,
   and many threads on one hot knob all `fetch_or` it. The code round's measurement must cover:
   - All six G2 cells.
   - G2 on the 100K store and on a 1M or 5M store.
   - A G1 and a G2 case that straddle publishes, with marks and faults counted.
   - first-read-1ms's first-read gate with usage on, the default.
   - The address arithmetic, as the doc already says.
4. **Per generation.** A read that a retiring generation answers, by a lookup that pinned it before
   the publish, marks that generation and is lost. That is by design (D161, Limit 12; the model's
   T1-T4 line). It is never reported against another generation: the mark goes into the answering
   `PageTable`'s mapping, and the walk names only the installed generation's marks. The exception
   is W1: a mark past the mapping's end writes whatever is mapped there. A union of successive
   reports does not give "read since start" (`b495aa` scenarios 1 and 2; W2): it loses every read
   between a generation's last report and its retirement.
5. **Memory.** 16,384 bytes is 1 MiB / 64, and the table's arithmetic holds: 7, 61 and 302
   segments give 112 KiB, 976 KiB and 4.72 MiB, and the dense 100K stores here have 6 segments
   and 96 KiB. `DETENT_MINIMUM_MAPPING_BYTES` stays 1,055,136 as the open-time floor:
   `core.cc:58-59` asserts 1 MiB + (13,107 + 16) / 16 × 8, `:747` refuses below it, open attaches
   nothing, and the marks are charged at attach. Three things fail: the index (W1); the 1/64 bound,
   which is a floor (W1, `bd1c3d`); and the publish-time need of two generations' marks (W4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| W1 | yes | defect | **The mark mapping has no index for a segment, and the stated bound is a floor.** D3 places segment `s`'s bitmap at `s` × 16,384 in one mapping of `root.segments` × 16,384, and says `resolve` "returns its index" (observability.md:88-89, 95, 263). A reference names its segment by a 16-byte identity only (`paged_layout.hh:30-37`; dense-pages.md:251 rejected a segment index in placement), and the reader meets segments lazily on misses. A `Segment`'s only index is its slot in a table of `bit_ceil(max(2, 2 × root.segments))` slots, chosen by identity hash and linear probe (`lazy_page_access.cc:30-32, 38-43, 105, 182-196, 213-217`). Indexed by slot, first reads write past the mapping on every real store: 1 of 1 segments on 10k, 3 or 4 of 6 on each 100K store and 12 of 29 on 100k-after-c. Past the end lies whatever the process mapped there (another generation's marks, the rows) or nothing. Sized for the slots, the charge is 2 to 4 times the stated one: 256 KiB for 6 segments, 2 MiB for 61 and 16 MiB for 302. A dense ordinal does no better: `published.fetch_add` runs after the CAS that makes the `Segment` visible (`:216-220`), and one taken before the CAS is wasted by every loser. The bound in :263 and Limit 11 ("1/64 of its segment bytes at most") is backwards. A segment holds at most 1 MiB, so the charge is at least 1/64: 1/32.4 on 10k, 1/63.2 on 100K, and a whole 16 KiB slot for round 3's 68,952-byte delta pack (1/4.2). | Jobs `951f31`, `bd1c3d`; `lazy_page_access.cc:30-43, 105, 182-232`; `paged_layout.hh:30-37`; observability.md:95, 263, 332 | State the index and the charge. Options: (a) Index by slot: reserve `slot_count(root.segments)` × 16 KiB of address space and charge what the touched slots can make resident, `root.segments` regions. Say what a 64 KiB page and a faulty store's extra segment cost: a segment past the pack count stays published after `excess` refuses it, `:216-222`. (b) Give each `Segment` its own mark mapping, made and charged in `publish` (sized `rounded_mapping_bytes(length / 64)`). That needs no index and marks only segments read; a small pack costs a page, not 16 KiB; the charge moves from attach to the miss. Recommendation: (a), the smaller change. State the 1/64 floor and the small-pack cost in :263 and Limit 11. Test `mark_index_bounds`: read every knob of the 100K store and of a store of 29 or more segments. A testing seam's largest mark word index is below the mapping's words, and `ever_read` equals the knobs read. It fails with slot indexes into `root.segments` × 16 KiB. `mark_charge` takes `root.segments` from the store. |
| W2 | yes | defect | **A union of per-generation reports is a lower bound on reads since start.** Limit 12 (:333) says "Read since start" is the union of reports taken at least once per generation. D7 item 6 (:195) says the union across hosts answers "which knobs are dead" exactly. A read a generation answers after its last report, before the publish that retires it, is in no report (`b495aa` scenario 1: union `{k0}`, read `{k0, k1}`). So is every read of a generation installed and retired between two reports (scenario 2). D7 item 5 (:194) and Limit 12's own first sentence say so, so the text contradicts itself. A knob read only in such windows, for example by a daily job in a process whose generation turns over between scans, is reported dead. That is the case D7's read set exists for. D161's "tooling that wants 'read since start' unions successive reports" rests on the union being exact. Blocking is the text; the mechanism is the owner's call. | Job `b495aa`; observability.md:1, 97, 194-195, 333; PLAN.md D161 | Say "lower bound" in Limit 12, D7 item 6 and the status line, and take it to the owner, whose D161 rests on it. Options: (a) Keep D161: the union is a lower bound, and tooling reads "dead" as "unread over a window". (b) Keep a retired generation's marks until one report has named them. That is the fold's cost class (generations kept mapped and charged, round-3 T2), smaller. Recommendation: (a). OB4's "the union of its per-generation reports" (:367) stays as a definition. |
| W3 | yes | gap | **The cost argument and G2 leave out what the segment-offset layout changes.** (i) :242 quotes D10's 8-thread column ("under G2's 10 ns by 3.6 ns"). D10's `ever_sampled` is over the ceiling at 1 thread (+7.4; +7.0 to +7.7 confirming, :230, 238) and at or over it at 64 (+14.4; +9.2 to +10.4 confirming). D48 and D61 left that to the in-core gate, and the doc does not say what a miss leads to. (ii) D10's `ever_bit` indexed a dense position: 100,000 bits, 12.5 KB, so 1,024 scattered names touched about 195 lines, inside the 32 KiB L1d. These marks are about 8 times that per store. At 100K they are 96 KiB, and 1,024 names spread over the segments touch about 745 lines (48 KB). At 5M they are 4.72 MiB: about 1,020 lines on about 690 distinct 4 KiB pages beside the definitions' own pages. Both figures are uniform-spread estimates, and D10 never ran 5M. G2's cases (:83) name no store. (iii) G1 and G2 run after a warm-up with no publish (:82-83). A publish starts a fresh mapping. Every knob's next read then pays a `fetch_or`, the first mark in each untouched page takes a fault on the lookup's thread (1.00 per page here, `52e63c`), and up to 64 threads on one hot knob `fetch_or` one line. (iv) The first read with usage on adds the attach's `mmap` and the first mark's fault to first-read-1ms's gate, which predates usage. | observability.md:82-83, 228-242; PLAN.md D48, D61; job `52e63c` | Say what a G2 miss leads to: an owner decision under D61 between keeping it, marking less and defaulting off. Run G2 on the 100K store and on a 1M or 5M store, all six cells. Add a publish case to G1: after a publish, the first lookup of each of 1,024 knobs makes one mark, the second none, with faults counted by `getrusage`. Add one to G2: blocks that straddle publishes. Rerun first-read-1ms's first-read gate with usage on, the default. |
| W4 | yes | defect | **At a publish both generations' marks are charged, and a refused attach keeps the old revision.** A publish builds the new view before it retires the old one. The new generation's mapping is charged at attach while the old one's still is (:252: a retired generation keeps its mapping until freed). An attach that throws keeps its namespace on the old generation, with state failed, and retries when the sequence moves (`core.cc:547-548, 567-573`). Take a cap that holds one generation's working set and marks but not two generations' marks. The on handle's first attach succeeds, and every later publish fails to attach: it answers the old revision with `DETENT_OK` and view state failed, where an off handle follows. :263 and Limit 11 (:332) say the two "answer alike" at "a cap that holds a generation with marks", and that the on handle answers `DETENT_NO_MEMORY`. :263's "depends on usage only through this charge" also leaves out Limit 10's walk, whose pin (up to 0.29 s at 5M) can refuse publishes on an on handle only (:331). The lifecycle table has no row for any of it. | `core.cc:547-548, 567-573, 621-639`; observability.md:252, 263, 277, 331-332 | State the publish-time need: the installed generation's marks, the attaching one's, and those of any retired generation a lookup pins. State what the on handle does past it. Options: (a) State it as is: the on handle stays on the old revision. (b) When the mark charge is refused, attach without marks and flag that generation's record incomplete in the totals. Answers then never depend on the charge, and the first-attach `DETENT_NO_MEMORY` difference goes too. Recommendation: (b); the owner rules if it narrows D161. Add a lifecycle row and test `usage_publish_cap`: at a cap between one and two generations' marks, publish. The off handle answers the new revision, and the on handle does what the option says. |
| W5 | no | gap | `campaign status` shows A2 met by `261124-144641-8cf85b`, my TSan run of the model. It inherited a `--for A2` tag from an earlier identical job (round 3's U7). A2 is the lookup cost with marks on, measured strict. | `/Users/kronuz/.scratch/campaign/jobs/261124-144641-8cf85b/for.json` | The lead removes the tag. |
| W6 | no | gap | An empty generation has `root.segments` 0 (`paged_layout.cc:45-46`), and `rounded_mapping_bytes(0)` throws (`generation_mapping.hh:63-69`). | as cited | Map nothing for an empty generation; add a row and a test. |
| W7 | no | gap | A failed mark `mmap` at attach has no stated status. `ENOMEM` and `EAGAIN` are transient for segment maps and retried (`lazy_page_access.cc:33-37`); the doc names only the cap's `DETENT_NO_MEMORY`. | `lazy_page_access.cc:33-37`; observability.md:263, 277 | State the status and the retry, and test them with a failing-`mmap` seam. |
| W8 | no | gap | The fork child remaps each mark mapping with `MAP_FIXED` in the atfork handler, and nothing says what happens if that fails (the parent's bits would stay). The remap of retired generations cannot be observed: no report reads a retired generation, and `fork_child`'s check passes when the child clears only the installed one (`b495aa` scenario 3). | `core.cc:275-287`; observability.md:110, 305; job `b495aa` | State the failure path, for example zeroing the installed marks in place and flagging the totals. Drop the retired remap, or keep it as hygiene no test observes. |
| W9 | no | gap | The child zeroes `next`, the spares, the flag and the stripe totals. It does not zero the overflow row's nine cells, which D3's diagram keeps apart from the rows, or `publishes`, `refused` and `attached`, so its totals include the parent's. | observability.md:92, 110, 168 | Say which counters restart at the fork; zero the overflow cells with the rows. |
| W10 | no | gap | The walk keeps no visited set. Under Limit 8's aliased page, a page reachable by two paths is walked once per path, so the walk's time is bounded by paths, not `root.pages`. | observability.md:99, 329 | Extend Limit 8, or cap the walk at `root.pages` page visits. |
| W11 | no | gap | The model never increments `freer_work` (`usage_claim_model.cc:229`), so the `freer_work == 0` clauses at :315, :335 and :376 cannot fail. | as cited | Count the freer's lookups and allocations, or drop the clause. |
| W12 | no | nit | D2's stripe line holds the countdown, a xorshift state and the stripe's 10 status totals (D1). At 8 bytes each that is 96 bytes, past one 64-byte line. | observability.md:37, 49 | Say two lines per stripe, or narrower cells. |
| W13 | no | nit | The dense 100K stores have 6 segments (96 KiB of marks), not 7 (112 KiB). | observability.md:248, 276; job `bd1c3d` | Take the count from the store's root (W1's `mark_charge`). |
| W14 | no | gap | `concurrent_usage` checks that each report's `ever_read` set is a subset of the knobs read in its generation. Nothing checks that a read is reported. | observability.md:299 | After join, with no publish since the last read, assert the set equals the knobs read OK in the final generation, as the model's stress check does. |
| W15 | no | nit | D6 items 1 and 2 define `ever_read` as read OK "since its namespace's installed generation attached". A lookup that pinned the previous view and returns after the attach is answered by the retired generation, and its read is not in the record. | observability.md:168-169, 333 | Say "answered OK by the installed generation". |
| W16 | no | nit | `reader.h`'s `mapping_bytes` comment covers "mapped packs and their verified bitmaps", not mark mappings. | `include/detent/reader.h:28-29` | Update it with D6. |
| W17 | no | gap | Round 3's U1 is still open: D2 says "relaxed load and store" for the countdown only, not for the shared xorshift state, and `concurrent_usage` runs at period 1, so it never reloads. | observability.md:49-51, 299 | Say both are relaxed atomics; run `concurrent_usage` once with the real reload. |
| W18 | no | gap | Round 3's U2 is half open: `detent_usage`'s status on a failed allocation is stated (D6 item 4), but its buffer's peak is not. | observability.md:171 | State the peak at 5M with every knob read. |
| W19 | no | nit | Round 3's U3 is still open: the overflow record's `ever_read` is unstated. | observability.md:150, 168 | 0. |

## Not run

- No strict or exclusive jobs. Nothing timed the mark, the footprint or the faults. The 745-line,
  48 KB and 690-page figures are estimates for uniformly spread names, and the 16 and 64 KiB
  page figures are arithmetic.
- Fault counts are from this host (macOS, 4 KiB pages). On Linux a read fault may map the zero page
  and the `fetch_or` take a second fault; that was not measured.
- No code exists for the design, so nothing ran in the core. The model and probes copy core code
  where they say so. The code cited is the branch's base. first-read-1ms is changing
  `lazy_page_access.cc` and `core.cc` (its file set, `Detent:1ca4c6e2`), so the code round
  re-checks these cites, W1's table among them.
- The quest model's pins are a mutex. The lock-free pins rest on round 3's `77e6af`, not rerun.
- Session: I run as a subagent of the lead, not as a session of my own, so no
  `detent · reviewer observability-core design` title applies.
