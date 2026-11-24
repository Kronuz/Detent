# journal-turn-sector-checks: journal

State: waiting on jobs 261124-054418-67e242 (turn_log_test on faultfs, every target builds), 261124-054420-8145a4 (A4 bytes at journal:4eb8165) and ci:ci at journal:4eb8165
Quest: .campaign/detent/quests/journal-turn-sector-checks.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Lead, at journal:94a043d (its design-doc fix pulled, fast-forward):
- ci/linux-tsan (run 45296503675) failed on a timeout, not a race: `journal_turn_log` hit its 120 s
  limit with no ThreadSanitizer report (TSAN_OPTIONS halt_on_error=1). The same code passed TSan at
  8bf1d1d in 63.19 s (run 45294628696) and took 0.85 s at f1ca348, before midwrite_reads. The cause
  is the case's size: 64-block segments, and MemoryIO's barrier copies the file, so the case took
  17.2 s alone in Release locally. journal:c7d15ce uses eight-block segments: 1.4 s (0.52 s user),
  same shapes (10721 checks, 0 failures). `partial_` is touched only on the loop's thread (stage,
  completed, reads); the writer thread copies `staged_->bytes`, never `partial_`.
- A4 latency 261123-160757-1330f1 (strict beside 8 light holds) did not decide: the fix had 3 slow
  rounds of 12 (p50 about 40 ms), aa 1, main 0, so turns/s/core 32.3 vs 39.0/38.8 at 256 B.
  journal:bd6cad6 makes turn_bench time the write (stage + write_at) and the barrier apart, for the
  turn log and the per-group arm that runs right after it in the same invocation (a control for the
  host's fsync regime; earlier jobs dropped its line). Two exclusive jobs, 12 rounds each, rotated
  main/fix/aa, 2.5 s per arm and size: 261123-180604-09a8dc (rounds 1-12), 261123-180608-7b9211
  (13-24).
- Bytes ede889 (strict, 8bf1d1d) equal the local figures: sectors touched -4.87% at 256 B, +0.95%
  at 4 KiB, -3.6 to -4.2% on T9; bytes passed to write() +14.5% at 256 B and +2.5% at 4 KiB, from
  rewriting the shared sector. Rerun at bd6cad6: 261123-180642-577728.
- Reruns at bd6cad6 (turn_log_test changed): F1 fails without 261123-180645-514406, F1 at the fix
  261123-180639-e460e3, A1 261123-180640-75fc0c, A2 261123-180641-82725c, A3 261123-180641-0b7099.

Before:

Durability review CHANGES at journal:f1ca348 (reviews/journal-turn-sector-checks-durability.md):
- F1 (blocking) fixed, journal:b6176f6: while a turn is staged, `Sectors` serves the sector its write
  rewrites (`offset_ / 504` of `segment_`) from `partial_`, the durable version's data, through
  `TurnLog::held()` into `read_record`; a read past `partial_` there is Corruption. Only that sector
  changes under an in-flight write (the review's invariant probe). Test `midwrite_reads` (2048 and
  32768-byte blocks, 4 seeds x 100 turns, write half-copied into the shared sector, peek every group):
  10854 checks, 0 failures locally. Jobs: fails without at f1ca348 261123-160740-42038e; at the fix
  with the reviewer's `shared_sector_probe invariant` and `midwrite` (must print "Corruption in 0
  of") 261123-160744-836752.
- F2 (non-blocking) not fixed, documented in turn-log-damage.md Limits (journal:8bf1d1d). Fixing it
  costs no bytes (a reader rule: damage past an unwritten header, or in a block 0 past an unwritten
  block 0, reads as the end), but it reads damage as the end: it changes D4/rule 1 and the quest's
  outcome ("reports damage instead of reading it as a tear"), and the review calls it the owner's.
  The design's Limits line (turn-log-cut-header.md:229, outside my file set) says such a sector is
  read only behind a torn write: the scan reads every block's first sector, spares included. For
  the lead: correct that line, file the reader rule as a follow-up if wanted.
- F3 (non-blocking) not fixed, documented in Limits: a tear inside a turn's first sector (outside
  the crash model) loses every group, the turn before's included. The only fix keeps a write out of
  the turn before's sector, i.e. each write ends at a sector boundary: the design's O2, measured at
  +27.7 to +30.7% bytes per turn (turn-log-cut-header.md Cost). Not small.
- F4: Ruling 10 reworded below. F5, F6: not taken (optional; F6 is the lead's ordering against
  journal-drop-memory-io).
- A4 latency at f1ca348, 261123-132700-4d14e4 (strict, 6 rounds, medians): 256 B p50 main 22.49,
  fix 23.04, aa 23.61 ms; p99 54.0, 57.5, 52.2 ms; 4 KiB p50 25.19, 22.67, 22.99 ms; p99 46.8, 43.6,
  44.7 ms. Fix within the main/aa spread. Rerun at 8bf1d1d: 261123-160757-1330f1.

Earlier this session:

Fresh contributor (previous context full). The A4 latency job 261123-104146-ca1f89 showed
written/appended 1.414-1.422 at 256 B at the fix against 1.231 at main and aa (+15%), 1.0466 against
1.0205 at 4 KiB, while the bytes job 261123-104143-3708f8 showed -5.80% and +0.97%. Measured, not a
defect:
- The two count different things. 3708f8's figure is the log's growth (the data position's advance,
  per sector 512 for 504); turn_bench's written/appended is bytes handed to the write
  (`stats().bytes_written`). At main a turn's write is its unaligned bytes; at the fix it covers
  whole sectors: the partial sector before it again, zeros after it, about 510 bytes more than the
  log grows. 3708f8's own "written" column shows the same ratio (2875.7/2042.7 = 1.408 at 256 B,
  34184.6/32683.5 = 1.046 at 4 KiB), and the design says it (Cost: "O3's write covers 500-600 bytes
  more than the turn"; its "Whole-sector write covers" column, 3027.5 at 256 B).
- Like for like: journal:062641c makes `cost` drive stage/write/completed and count the 512 B
  sectors and 4 KiB pages each write touches, which is what a device and the page cache write back
  under either format. Locally (light; 261123-132656-758884 repeats it strict): sectors touched T9
  1408.0 -> 1348.6 (-4.22%, 2 KiB), 1380.0 -> 1325.6 (-3.94%, 4 KiB), 1349.6 -> 1300.5 (-3.64%,
  32 KiB); turn_bench 256 B 3022.9 -> 2875.7 (-4.87%); 4 KiB 33861.3 -> 34184.6 (+0.95%). Pages
  touched -1.04 to -2.31%, +0.85% at 4 KiB. Main's sectors touched at 256 B, 3022.9, is the design's
  3027.5 column.
- No defect: the fix's "written" equals its sectors touched in every row (no sector past the turn's
  last, so no padding or trailer-only sector beyond the span), and log bytes per turn match the
  design's O3b column (2366.7 vs 2372.9 at 256 B, 33673.8 vs 33880.9 at 4 KiB): turns still pack.
- So A4's claim is the log's growth (the design's O3b column, -5.6 to -6.2%, +1.0%), and the device
  cost on equal terms is lower at the fix except +0.95% at 4 KiB records. Written up in
  docs/mechanisms/turn-log-damage.md's Cost (journal:f1ca348).
- Latency at ca1f89 (dea3757): p50 is 21 or 41 ms by host fsync regime, the same in every arm;
  medians of 6 rounds at 256 B main 21.6, fix 21.08, aa 21.66 ms; rerun at f1ca348.
- journal_turn_log wall time 261123-113618-d586c1 (strict, 8c1e8c2): main 39.1 / 49.9 s, fix 35.4 /
  40.7 s.

## Next

1. When 579a83 and 73e03d end (bytes must equal 577728: -4.87% sectors touched at 256 B, +0.95% at
   4 KiB) and ci:ci is green at journal:4eb8165, set State to "ready for review". Durability
   re-acceptance is the lead's (changed since 8bf1d1d: bench/turn_bench.cc, test/turn_log_test.cc,
   CMakeLists.txt's one-word faultfs_tests entry).

## Blocked on

Nothing.

For journal-bound-first-open (owns include/journal/host.h): host.h:30 says "a loop's RFTW0001
segment"; it is RFTW0002 now. Out of every set, left as they are: docs/design/idle-groups.md:5,362
and docs/mechanisms/group-lifetime.md:5 state "RFTW0001 unchanged" for their own quests (true
then); fuzz/fuzz_framing.cc:41 and test/framing_test.cc:304 use "RFTW0001" as an arbitrary framing
test magic.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261123-180640-75fc0c (at bd6cad6; supersedes 261123-104119-1fb4f2): main e58f977 with b335b69's probe: newest tail SILENT 36 of 201, earlier turns 4807 reported, 0 silent. Fix, its port (`turn_log_power_test probe 24 40`): 0 SILENT in the newest crossing, every changed sector, earlier turns and the clean newest turn. Counts differ with the layout (Ruling 6) |
| A2 | 261123-180641-82725c, 261123-180639-e460e3 (F1 at the fix); fails without: 261123-104123-1ecb5a (newest_turn_damage at main: 41 SILENT), 261123-160740-42038e, 261123-180645-514406 (midwrite_reads at f1ca348: 898 failures, exit 1) |
| A3 | 261123-180641-0b7099; fails without: 261123-104128-58508b |
| A4 | bytes 261123-180642-577728 (strict, bd6cad6; at the merge's port journal:4eb8165: 261124-054420-8145a4; 261124-053626-81c403 failed at 384ad20: turn_log_test.cc still included the deleted memory_io.h); latency (exclusive, A/A rotated, 24 rounds) 261123-180604-09a8dc, 261123-180608-7b9211; earlier, not cited: 261123-104143-3708f8, 261123-104146-ca1f89, 261123-132700-4d14e4, 261123-160757-1330f1 (strict beside light holds: did not decide) |
| A5 | `docs/mechanisms/turn-log-damage.md`: "A sector more than 2^32 segments older than the file's segment reads as damage, not as stale" (F6); "An all-zero sector is never written, not damage, and this is not \"verifies under neither\"" (F7); "A fragment never verifies on its own, so bytes inside a torn write's payload that once formed a valid fragment cannot forge a record" ... "reports every group lost" (journal-earlier-turn-loss F1) |
| A6 | fixtures regenerated (journal:2e17407; journal_crc_fixture_test 87 checks); every target builds and journal_turn_log_test passes at journal:4eb8165: 261124-054418-67e242; suite `ci:macos-arm64` and `ci:ci` at journal:4eb8165, gate jobs queued by `campaign verify` |

## Result

RFTW0002: the turn log's data is written in 512 B sectors, each 504 data bytes and an 8 B check
bound to the full segment and the sector's index, so a torn or rotted sector is reported as damage,
never read as the end of the log (A1 0 SILENT, A2 every verdict by value, A3 the 32-bit alias).
Mechanism and Limits: `docs/mechanisms/turn-log-damage.md` (Cost section for the numbers below);
design `docs/design/turn-log-cut-header.md`. Fixtures regenerated, fuzz replay reads them.

Cost against main e58f977 (strict, 261123-180642-577728 at bd6cad6; same figures at 8bf1d1d in
ede889):
- Log growth per turn: T9 897.3 -> 838.2 B (-6.59%) at 2 KiB blocks, 869.2 -> 816.1 (-6.10%) at
  4 KiB, 838.8 -> 790.8 (-5.72%) at 32 KiB; turn_bench 256 B records 2512.4 -> 2366.7 (-5.80%);
  4 KiB records 33350.7 -> 33673.8 (+0.97%). Design: -5.6 to -6.2%, +1.0%.
- Sectors touched per turn (what a device writes back): T9 1408.0 -> 1348.6 (-4.22%), 1380.0 ->
  1325.6 (-3.94%), 1349.6 -> 1300.5 (-3.64%); 256 B 3022.9 -> 2875.7 (-4.87%); 4 KiB 33861.3 ->
  34184.6 (+0.95%). Pages touched -1.04 to -2.31%, +0.85% at 4 KiB.
- Bytes passed to write() (whole sectors: the shared sector again, zeros after): +14.5% at 256 B
  (2512.4 -> 2875.7), +2.5% at 4 KiB, +50-55% on T9. It equals sectors touched; the log's growth is
  what falls.
- Append latency, exclusive, 24 rounds rotated main/fix/A-A (261123-180604-09a8dc rounds 1-12,
  261123-180608-7b9211 rounds 13-24; lat.py), turn_bench 16 groups, 50% active, medians of 24 runs:

  | size | arm | turns/s/core | p50 ms | p99 ms | write p50 us |
  |---|---|---|---|---|---|
  | 256 B | main | 48.4 | 20.77 | 22.7 | 32.1 |
  | 256 B | fix | 48.6 | 20.71 | 22.6 | 30.9 |
  | 256 B | A/A | 48.4 | 20.80 | 23.0 | 35.2 |
  | 4 KiB | main | 48.4 | 20.81 | 23.5 | 45.0 |
  | 4 KiB | fix | 48.0 | 20.82 | 23.1 | 42.8 |
  | 4 KiB | A/A | 48.4 | 20.76 | 23.1 | 40.3 |

  The A/A band (main vs main) is 0.03-0.05 ms on p50 and 0.3 ms on p99; the fix is inside it at both
  sizes (-0.06 and +0.01 ms against main). No run was slow (p50 > 30 ms) in any arm: p50 is the
  host's fsync interval (about 20.1 or 20.8 ms), the barrier is 99.7% of it, and the write itself
  is 25-73 us. So there is no latency loss beyond the band; the write's 31-45 us is dominated by
  noise between runs, larger than any difference between arms. Latency is bound by the barrier, so
  the +14.5% bytes handed to write() costs nothing measurable here.
- The earlier strict job beside light holds (261123-160757-1330f1) did not decide (3 slow rounds in
  the fix, a host fsync regime); the exclusive jobs replace it. The merge of main (384ad20) touches
  no code turn_bench or the log runs: CMakeLists's faultlab pin and group_io_bench, memory_io.h
  removal; the latency jobs are cited at bd6cad6, the bytes job is rerun at 4eb8165.

Suite: F1 (a read in flight sees the durable shared sector) fails at f1ca348 with 898 failures
(261123-180645-514406) and passes at the fix (261123-180639-e460e3). Open (documented in Limits):
F2 (damage past an unwritten header reads as the end), F3 (a tear inside a turn's first sector
loses the turn before's groups too; the fix is the design's O2, +27.7 to +30.7% bytes).

## Rulings I made

1. The sector layer (Stream, Emitter, RecordReader, verdict) lives in `turn_log_detail` in
   turn_log.h, not framing.h: framing.h is outside the file set and serves strata's format.
2. `record_cost` stays payload+65 and `block_cost` 126 (RFTW0002 needs +45 and 82): conservative
   upper bounds; `turn_bound`/`turn_room` scale data bytes to sectors. docs/mechanisms/turn-seal.md
   (outside the file set) still derives the bound in RFTW0001 terms: the lead's.
3. A block that does not read whole is re-read a sector at a time; only unreadable sectors the
   parse needs are damage.
4. Damage past an earlier epoch's last kept turn is reported (else-branch in recover): found by
   newest_turn_damage seeds 12 and 24 at 2048, where the newest open's record was damaged.
5. turn_log_test.cc stays format-agnostic (`sectors`, `file_at`, `block_data`), so the A2
   `--fails-without` job compiles it at journal main.
6. A1: b335b69's probe parses RFTW0001, so it cannot run at the fix; it runs at main for the
   baseline (36 of 201), and T9's `probe` mode ports its sweeps (same workload, seeds, opens) for
   the fix. Case counts differ with the layout.
7. A6 "fuzz target reads them": fuzz_turn_log.cc takes a raw segment, so RFTW0002 reaches it via
   fuzz/replay.cc's TurnLog-written segments and mutations; the fixtures are read by
   crc_fixture_test. replay.cc is outside the file set and unchanged.
8. Block sizes not a multiple of 512 are rejected (`valid_block_bytes`): T9 drops 1000, 768, 256.
9. lost_bound_test: a damaged byte costs its whole sector, so `layout.damaged` moves to X's second
   sector (X's header, and with it block 2's manifest, stays readable), and `middle()` avoids a
   block's header sector; group 6's first batch is 2,450 bytes so turn A still starts in X.
10. host_test idle_damage: the test now damages the two blocks after the first 'Z' (blocks 1-2),
   not after the first 900-'Z' run (blocks 3-4), so group 6 keeps every record under either format
   (the review's F4: the change is the placement, not RFTW0002); the check says so instead of
   "bounded at 2". The old placement on RFTW0002 lands in the newest block, whose manifest is not
   written, and loses groups 1 and 6. A later group bounded by an orphaned block's manifest is no
   longer exercised in host_test; lost_bound_test still bounds groups with later records kept.
11. earlier_turn_damage runs on MemoryIO: same checks, no full flushes.
12. F1's fix serves the shared sector from `partial_` only while a turn is staged; otherwise reads
   go to the file, so a live read still checks every sector it uses when no write is in flight.
13. The merge with main deleted journal/memory_io.h (journal-drop-memory-io); turn_log_test.cc's
   MemoryIO disks are `fl::filesystem`/`fl::io` now, copies by `snapshot()` (journal:4eb8165, cases
   unchanged). CMakeLists.txt, outside the file set, gains `turn_log` in `faultfs_tests` so the test
   links faultlab: the one change the port needs. The A4 job patches the same word into main's
   archived CMakeLists to build the new test there. `grep memory_io` over the tree: none left.
