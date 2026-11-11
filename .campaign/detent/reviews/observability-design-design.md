# observability-design review: design (re-review)

Commit: 5282545286b326adb3154d06448a79e664691c39
Verdict: APPROVE
Date: 2026-11-10 (round 1 at `c30abc4`: CHANGES; round 2 at `5282545`: APPROVE, see "Fix check")

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

Scope: `Detent:486b57b..c30abc4`, files `docs/design/observability*`. That covers A8 (D10), the
owner rulings D47 and D48, and the `ever_bit`/`knob_sampled` pair. The code `c30abc4` reviews
is the same as `71e7c76`, where the reruns ran: `71e7c76..c30abc4` touches only the journal.

The shape stands. An exact ever-read bit beside sampled counts is the right answer to D38. A8's
search is real: five exact mechanisms, interleaved, exclusive, at 1, 8 and 64 threads. The
reruns at the head reproduce its verdict: only `ever_bit` passes every row, and every exact count
fails scattered names at 64 threads. D10's tables match the jobs they cite to the decimal.

Four things block, and all four are text fixes:

1. The document's headline cost comes from one D2 run that three later runs do not reproduce
   (OR1).
2. "The pair passes at 64 threads", a premise of D48, fails in one of the three exclusive runs
   of the same binary (OR2).
3. The max(int, bool) tables hide the bench's real resolution (OR3).
4. The host document's row cap breaks the per-host exactness that Q7 promises (OR5).

## Evidence I relied on

- **Reruns at `71e7c76`, all exit 0, one binary.** All ran on slot `obs-bench`, ref
  `s6=b1d281c`, bench source digest `15b89164…`, the same digest as `fa1352`'s binary.
  - Build `261110-021130-cf5075` and smoke `f4abe5`.
  - Strict screen `e7b6b4`.
  - Exclusive confirms `d5103d` and `1bad17` (the D2 method).
  - Exclusive exact-confirm `06137b` (A8).
  - Strict pair-screen `368d1b`.
  - Exclusive pair-confirm `445051` (`es_one`).
- **The original jobs the document cites.**
  - Exclusive `261109-120858-28cd50`, the source of D2's table.
  - Exclusive `261109-202834-bfb3cf` (D10 table 1, digest `904da3e…`).
  - Strict `261109-222819-1a0f6c` and exclusive `261109-224833-fa1352` (D10 item 4, digest
    `15b89164…`).
  - Exclusive `261109-131328-0ce3a4` (the first paired confirm, at `51923f8`).

## What I checked myself

Scripts are under `.scratch/review-observability-design-design/` in the home checkout.

- **`d10table.py <*-paired.md>`** rebuilds D10's max(int, bool) hot/scattered table from a
  job's paired output. It also lists every per-op cell that the max hides below -5 ns.
  - On `bfb3cf` and `fa1352` it reproduces D10 table 1 and item 4's table exactly.
  - On the head's `06137b`, `445051` and `368d1b` it gives the numbers quoted in OR2 and OR3.
- **D2 against its reruns.** `observability-paired.py` run offline on `28cd50`'s jsonl gives
  `28cd50-paired.md`, which reproduces D2's table exactly. I then compared it with the
  `confirm-paired.md` files of `0ce3a4`, `d5103d` and `1bad17` (OR1).
- **The after binary's code.** I disassembled `block<ever_sampled>` (Arm 21) and
  `block<knob_sampled>` (Arm 13) of the slot binary built by `cf5075` with `otool -tV`. Each
  has one indirect lookup call and one loop. In `ever_sampled`:
  - The hot path is load word, `bt`, frame-byte store, countdown load and test, decrement and
    store.
  - The countdown pointer stays in `%r13`.
  - The bit-clear path (`lock or`) duplicates the frame store and the countdown test, then
    rejoins at the decrement.
  - The cell is computed only before `sample`.
- **Probe job `261110-053432-d2ad94`** (`disasm.sh`). It builds the bench from `09f038e`'s
  source and from the head's source, exactly as `observability-bench.sh` does, and counts the
  indirect lookup calls, `lock` operations, `sample` calls and frame stores in `block<0|13|20|21>`
  of each. It was still queued behind the exclusive queue when I wrote this; see OR4.
- **Consistency of `ever_sampled` with A1-A6.** I read the D1 table, D7 items 1-6, D8's caps,
  Limits 2, 8 and 11, the OB1, OB4 and OB6 rows, and Q4 and Q7 against D10's shape (OR5 to OR7).
- **D48 against PLAN.md** (`Detent:71e7c76`), row D48 (OR9).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| OR1 | no | gap | (fixed at 5282545) The headline cost claims come from one run of D2's sequential method, and three later runs do not reproduce them. The claims are the lead sentence ("a paired median of 7 ns or less … under 8 threads"), the Abstract ("−4.4 to +6.7 ns"), D2 item 1, D2 item 2 ("every repetition of `knob_rows64` is above +15 ns") and D6 item 2 (`hook_off` "−5.3 to +7.0 ns", A4's unused cost). D2's table quotes `28cd50` faithfully. In the later runs, `knob_sampled` at 8 threads ranges from −16.2 to +17.9 ns. `none2` (A/A) reaches +63.9 (`0ce3a4`, int scattered) and +14.1 (`d5103d`). `knob_rows64` repetitions fall to −548 ns, and one median is +490.1 (`d5103d`, bool scattered). `none`'s 8-thread spread is up to 757.5 ns (`1bad17`). `hook_off` runs from −16.9 to +22.2. The shape has also changed: the pick is `ever_sampled` (Q7), not `knob_sampled` alone. | `d5103d`, `1bad17`, `0ce3a4` `confirm-paired.md`; `28cd50-paired.md`; `observability.md` lines 5, 15, D2 items 1-2, D6 item 2 | Quote the interleaved numbers (D10) in the lead and the Abstract. Across `fa1352`, `06137b` and `445051` at 8 threads, `ever_sampled` is +2.3 to +6.4 and `knob_sampled` +0.4 to +5.0. Label D2's table a one-run sequential screen whose A/A moves by tens of ns, cite the three reruns, and keep it only for the large separations (+20 to +60 ns). For A4, either put `hook_off` in a pair run now or say that OB3's interleaved gate is the first resolving measure of it. |
| OR2 | no | gap | (fixed at 5282545) The claims "The pair now passes at 8 and 64 threads (worst +9.3 against 10)" in D10 item 4, and D48's premise "it passes at 8 and 64 threads", do not hold in every run at the head. Same binary (`15b89164`), `ever_sampled` bool scattered at 64 threads: +8.3 (`fa1352`), **+10.4 FAIL** (`06137b`, exclusive), +8.6 (`445051`), and in the strict `368d1b` +10.3 (bool scattered) and **+11.8** (int scattered; the first version of this review attributed +11.8 to bool). The 64-thread row sits at the ceiling, just as the 1-thread row sits over it: +7.0 to +7.7 in all three runs. The owner accepted one miss; the evidence at the head shows that row plus a coin flip at 64. | `06137b`, `445051`, `fa1352`, `368d1b` paired outputs; `d10table.py` | State the 64-thread result as measured: +8.3 to +10.4 across three exclusive runs, one over the ceiling. The lead tells the owner that D48's "passes at 64" holds in 2 of 3 runs, so the owner can confirm D48 or amend it. Keeping `ever_sampled` is still a reasonable call; this is a correction of the premise, not a reversal. |
| OR3 | no | gap | (fixed at 5282545) The tables overstate how finely the bench resolves. On and off are two compiled loops (`block<A>` and `block<none>`), and the A/A compares two identical loops, so it bounds placement but not code shape. The max(int, bool) cell hides both kinds of excursion. **A/A:** `none2` bool hot at 1 thread is −3.9 in all three exclusive runs of the after binary (−3.2 to −3.7 at 8 threads), yet D10 item 4's table prints −0.9 and item 4 says "within ±1.7 ns in every row but one". **Code shape:** `knob_sampled` int scattered at 64 threads, an arm that adds work, measures −25.2, −29.3 and −25.5 ns in the three exclusive runs (−28.8 and −27.3 strict). The journal names this effect ("the same kind of effect in the other direction"); the document does not. Against a 5 ns ceiling at 1 thread and a 10 ns ceiling at 64, both D48 rows are inside the bench's demonstrated error, in either direction. | `d10table.py` "hidden negatives" on `fa1352`, `06137b`, `445051`, `1a0f6c`, `368d1b`; D10 item 4 and its table; D10 table 1's note | Report per-op worst cells, or footnote the hidden ones. State the bench's resolution: about ±4 ns of placement at 1 thread, and up to ~29 ns of code shape at 64. Say that this is why OB1's re-measure decides: OB1's method (two handles, one compiled path, a runtime switch) removes the code-shape confound. It does so only if the off handle also skips the ever-bit (OR6). |
| OR4 | no | gap | (settled by probe `261110-064037-1b01c2`, see Fix check) D10 item 4.3 asserts a cause: "At `09f038e` clang split the loop … recompiled … the same source is one path … and the 64-thread excess goes". The evidence is a before/after across two builds with different arm sets. Three things weaken it. (1) The journal itself says "The bench cannot split codegen from placement". (2) `es_one`, a one-path shape by construction, is worse at 64 threads (+12.2, +12.7). (3) The same rebuild made `ever_sampled` int scattered at 1 thread worse, from −0.4 to +4.4. The journal also describes the *after* binary (`15b89164`) as keeping "two duplicated tails … a countdown pointer reloaded from the stack on one path". My `otool` read of that binary shows one loop with the pointer in `%r13`, and only the cold bit-clear path duplicates a tail. No disassembly is a job artifact. Probe `d2ad94` captures both binaries. If it shows the before binary as one loop as well, or with no duplicated status split, this becomes blocking. PLAN D48's "single-loop fix" names a code change that does not exist: the source is the same. | Probe `261110-053432-d2ad94` (`disasm.sh`); `otool -tV` of the `cf5075` binary; journal lines 46-57; D10 item 4.3 | Reword item 4.3 as an observation: the same source, rebuilt beside new arms, moved the 64-thread rows by 4 to 6 ns and a 1-thread row by +5 ns. The bench cannot attribute this. Cite `d2ad94`'s `summary.txt` for the shapes, and stop calling it a fix. |
| OR5 | no | defect | (fixed at 5282545) The host document caps at 65,536 knob rows "ordered by reads" (D8, OB4). The knobs the ever-read bit exists for, those read once or a few times, have zero or few sampled reads, so they sort last and are the first to be truncated. A host that reads more than 65,536 knob-revision rows then reports exactly those knobs as unread. That contradicts Q7 ("'was this knob read' is exact per handle, per host") and D7 item 6. | D8 last paragraph; OB4 criteria; Q7; D1 "Knob ever read" row | Export the ever-read set uncapped and apart from the count rows: per revision, either a bitmap (625 KB at 5M) or a list of positions. Alternatively, cap it and add a Silent Limit saying that past the cap a zero is not evidence. Add the case to OB4's criteria. |
| OR6 | no | gap | Two ever-bit lifecycle points that OB1 needs are unspecified. (1) Page growth: each attached generation gets a bitmap in the page (D10 item 2; 625 KB per generation at 5M), and views hold at least two generations until the D5 freer retires one. Nothing says the retired bitmap's range is reused, so the page could grow by one bitmap per publish. (2) What `usage_slots = 0` turns off. Limit 8 says the table, but not the bitmaps. If the bitmaps stay on, OB1's off handle carries the bit and its gate measures only the countdown, and `detent get` allocates bitmaps for nothing. | D10 shape items 1-3; Limit 8; OB1 gate in D2 | State the page's bitmap bound and reuse, which is D1's per-process bound. Say that `usage_slots = 0` disables bits and countdown alike, and that OB1's off handle is that handle. |
| OR7 | no | gap | A fork child inherits its parent's bits. D7 item 4 copies names into the child's page and reseeds, but says nothing about bits. A prefork server's children would then report knobs only the parent read. The host union is still right; per-process attribution is not. | D7 item 4; D10 item 2 | Say whether the child clears its bitmaps at its first refresh, or that per-process ever-read includes inherited reads. |
| OR8 | no | nit | D10 table 1's note says the A/A cell is "the larger of its two deltas by size", but the cells show the larger by value (−1.7 where bool hot at 64 is −5.0). | `bfb3cf` `exact-confirm-paired.md`; D10 note under table 1 | Make the note match the cells, or show by-size cells. |
| OR9 | no | nit | D48's terms mostly match. D10 item 4's Decision and Q7 agree with PLAN D48 on +7.7 against 5.0, `knob_sampled` +6.9, "about 3 ns between builds", 8 and 64 passing, and OB1 re-measuring. There are three differences. (1) PLAN says "misses OB1 by +7.7 ns"; the row measures +7.7, which is 2.7 over the ceiling. (2) PLAN's "single-loop fix" (OR4). (3) The document adds terms D48 does not state: "if the row still misses there, cutting the countdown's cost is OB1's work, and the bit stays". | PLAN.md D48 (`71e7c76`); D10 item 4 Decision | The lead corrects PLAN's wording, and either records the added term in D48 or marks it in the document as the design's proposal. |
| OR10 | no | nit | D7 item 5 (Formats) lists the host document's fields without the ever-read set, although OB4's criteria and D7 item 6 rely on it. | D7 item 5; OB4 row | Add the ever-read field, which OR5 shapes, to D7 item 5. |

Answers to the risk questions:

1. **Do the D10 numbers follow from their jobs?** Yes, exactly, and A8's verdict reproduces at
   the head. The comparisons are interleaved, exclusive and run at 1, 8 and 64 threads, in
   both orders. Two things are not honest yet:
   - The tables hide per-op excursions, and the A/A understates the error (OR3).
   - D2's table does not survive its reruns (OR1).
2. **Is `ever_sampled` consistent with A1-A6?** Mostly. The D1 table, D7 item 6, Limits 2 and
   11, OB4 and OB6 carry the bit. OB1's row carries the gate with the pair together, which is
   D48's re-measurement in the core quest. The exceptions:
   - D8's cap contradicts per-host exactness (OR5).
   - The page bound and the off switch are unspecified (OR6).
   - Fork attribution is unspecified (OR7).
3. **Is the codegen explanation shown?** Not yet. It is asserted from a before/after across
   builds, and es_one contradicts it (OR4). The probe is pending.
4. **Do D48's terms match PLAN.md?** Yes, apart from wording and one added term (OR9). Its
   64-thread premise, though, fails in one of three exclusive runs at the head (OR2).

## Fix check (round 2, `Detent:c30abc4..5282545`)

The fix round (`56dd0ab` design, `b875057` and `5282545` journal) is text only: the bench source,
driver and paired script are unchanged, so the jobs above still stand for the code. I checked
each new number against the paired outputs with `d10table.py` and the per-op rows.

| ID | Status | Check |
|---|---|---|
| OR1 | fixed | The lead and Abstract now quote D10's interleaved runs: `ever_sampled` +2.3 to +6.4 at 8 threads, +6.1 to +10.4 at 64, +1.5 to +7.7 at 1 (`fa1352` int hot 1 is +1.5). These match `fa1352`, `06137b` and `445051`. D2's table is labelled a one-run sequential screen, with the three reruns cited (`none2` −10.2 to +63.9). D2 items 1-2, D6 item 2 and D9 carry the rerun ranges. A4's `hook_off` is marked unresolved, and OB3's gate is interleaved. |
| OR2 | fixed | D10 item 4 gives the 64-thread row as +8.3 / +10.4 / +8.6 (bool scattered) and +9.2 to +9.5 (int), with one exclusive run of three over, and the strict `368d1b` +11.8 int scattered, which is correct. The status line, Q7 and the Decision say the lead has put this to the owner. |
| OR3 | fixed | "The bench's error." paragraph: ±4 ns placement at 1 thread, up to 29 ns of code shape at 64, both D48 rows inside it; the off handle with `usage_slots = 0` makes OB1 one compiled path. The after table's footnote names the hidden −3.9 and −25.2/−29.3/−25.5 cells. |
| OR4 | fixed; settled | Item 4.3 is now an observation with no cause claimed. The 64-thread cells moved down by 1.2 to 6.1 ns per op from `bfb3cf` to `fa1352`, and int scattered at 1 thread moved up from −0.4 to +2.4 to +4.4. Both claims check out. Probe `d2ad94` failed (exit 141, a SIGPIPE in my script, not the bench). The fixed probe, `261110-064037-1b01c2` (exit 0, at `5282545`), rebuilt both binaries from source digests `904da3e` (= `bfb3cf`) and `15b89164` (= `fa1352` and the reruns). Before, `block<ever_sampled>` has 126 instructions. After the one indirect lookup call it branches on the status (`testl %eax; je`) into two tails, each with its own bit test, `lock or`, frame store, countdown test and loop back-edge. After, it has 99 instructions and one tail, and the cell is computed only before `sample`. `block<knob_sampled>` is the same in both builds (86 instructions). So the shapes D10 first described are real. That the shape change caused the timing change is still not shown, which the observation wording now respects. |
| OR5 | fixed | D8, D7 item 5, Q7, the D1 row, Limit 12 and OB4: the ever-read set travels as per-namespace, per-revision bitsets outside the capped rows. That is 625 KB per revision at 5M knobs (834 KB in base64, arithmetic checked), against 20 MB for a list of 32-bit positions. OB4 adds a past-the-cap case. |
| OR6 | fixed | D10 item 2: a retired generation's bitmap range returns to the page, so the bound is one bitmap per generation a live view holds. Item 3 and Limit 8: `usage_slots = 0` allocates no bitmaps and runs neither the bit nor the countdown, and it is OB1's off handle. |
| OR7 | fixed | D7 item 4: a child's bitmaps start clear. |
| OR8 | fixed | Table 1's note says "by value" and names the −5.0 hidden behind −1.7. |
| OR9 | fixed in the document; PLAN open | D10 marks "if the row still misses … the bit stays" as the design's proposal beyond D48. PLAN.md D48 still reads "misses OB1 by +7.7 ns" and "single-loop fix". That is the lead's text, outside this quest's files. |
| OR10 | fixed | D7 item 5 lists the ever-read bitsets. |

New, non-blocking:

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| OR11 | no | nit | D10 item 4.2 and 4.3 cite probe `261110-053432-d2ad94`, which failed (exit 141, my script). The run that records both binaries is `261110-064037-1b01c2`. It confirms 4.2: in both binaries the bit is a load and a test, with `lock or` only on the clear-bit path. | `261110-064037-1b01c2` `summary.txt`, `before-block21.s`, `after-block21.s` | Change the citation to `261110-064037-1b01c2`. |
| OR12 | no | nit | PLAN.md D48's wording (OR9) is the lead's to correct. | PLAN.md D48 | The lead edits D48. |

Verdict: APPROVE. Every blocking finding (OR1, OR2, OR3, OR5) is fixed in the text and checked
against the existing jobs. No new measurement was needed, because the fixes quote the reruns
already on record.

## Not run

- The disassembly probe ran as `261110-064037-1b01c2` (exit 0) after `d2ad94` failed on a
  script bug. Rerun: `campaign job submit --quest observability-design --ref s6=b1d281c -- bash
  .scratch/review-observability-design-design/disasm.sh` from the home checkout, then read
  `summary.txt` in its output.
- No new timing job. The four exclusive runs of the after binary already show the spread that
  OR2 and OR3 report. Another exclusive run would add one more draw, not a resolution.
- No Linux check of the page or bitmap layout (Limit 7). OR6 and OR7 are reasoned from the
  text.
- I did not recheck round-1 findings OD-3 to OD-9 beyond what the diff since `486b57b` touches.
