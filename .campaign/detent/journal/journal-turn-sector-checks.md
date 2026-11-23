# journal-turn-sector-checks: journal

State: waiting on jobs 261123-104146-ca1f89 261123-113618-d586c1; ci:ci at journal:8c1e8c2
Quest: .campaign/detent/quests/journal-turn-sector-checks.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

The lead widened the file set (lost_bound_test, host_test, AGENTS, ARCHITECTURE, turn_bench,
host-layout, framing.md, turn-seal.md, framing.h). Done at journal:8c1e8c2, pushed:
- journal:64747e8: lost_bound_test finds records in data positions and damages mapped file
  offsets (offsets only, plus group 6's first batch 2,290 -> 2,450 bytes so turn A starts in X);
  host_test idle_damage and idle_unmanifested likewise.
- journal:a5670ba: earlier_turn_damage on MemoryIO (it did 2 full-flushed flips per case on
  PosixIO); journal_turn_log 37 s locally alone, timing job queued against main.
- journal:8c1e8c2: docs and comments to RFTW0002.

## Next

1. 261123-104146-ca1f89 (A4 latency, strict), 261123-113618-d586c1 (journal_turn_log time, main vs
   fix, strict), ci:ci at journal:8c1e8c2, local suite: fill A4, A6 and the Result.

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
| A1 | 261123-104119-1fb4f2: main e58f977 with b335b69's probe: newest tail SILENT 36 of 201, earlier turns 4807 reported, 0 silent (blocks-newest-turn SILENT 29 of 108). Fix, its port (`turn_log_power_test probe 24 40`): newest crossing 258 reported, every changed sector 436 reported + 5 harmless, earlier turns 5525 reported + 2 harmless, newest turn of the clean log 103 reported: 0 SILENT; 6473 checks, 0 failures. Counts differ with the layout (Ruling 6) |
| A2 | 261123-104121-0dca59 (sector_verdicts 17 checks, stale_alias 5, newest_turn_damage 447 reported and 487 tears, T9 at 8 seeds 31268 checks, 0 failures); fails without: 261123-104123-1ecb5a (newest_turn_damage at main: 41 SILENT, exit 1) |
| A3 | 261123-104126-e17d15; fails without: 261123-104128-58508b (sector_check binding 32 bits: "a sector of segment S - 2^32 reads as stale, never as current: W is torn", exit 1) |
| A4 | bytes 261123-104143-3708f8 (main e58f977 vs fix, same workloads): T9's 897.3 -> 838.2 (-6.59%) at 2 KiB, 869.2 -> 816.1 (-6.10%) at 4 KiB, 838.8 -> 790.8 (-5.72%) at 32 KiB; turn_bench 256 B 2512.4 -> 2366.7 (-5.80%); 4 KiB records 33350.7 -> 33673.8 (+0.97%). Design: -5.6 to -6.2%, +1.0%; 2 KiB is 0.4 points better than its estimate. Bytes written per write (whole sectors) +50-55% T9, +14.5% 256 B, +2.5% 4 KiB. Latency: pending 261123-104146-ca1f89 |
| A5 | `docs/mechanisms/turn-log-damage.md`: "A sector more than 2^32 segments older than the file's segment reads as damage, not as stale" (F6); "An all-zero sector is never written, not damage, and this is not \"verifies under neither\"" (F7); "A fragment never verifies on its own, so bytes inside a torn write's payload that once formed a valid fragment cannot forge a record" ... "reports every group lost" (journal-earlier-turn-loss F1) |
| A6 | fixtures regenerated (journal:2e17407; journal_crc_fixture_test 87 checks, 0 failures locally); lost_bound 116 checks, host idle_damage 11 and idle_unmanifested 8, 0 failures locally at journal:8c1e8c2; ci:ci pending at journal:8c1e8c2 |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

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
10. host_test idle_damage: under RFTW0002 group 6's later records sit after the batch's end in a
   block no damaged manifest names, so it keeps every record; the check says so instead of
   "bounded at 2". The rule that an orphaned block's manifest bounds a later group is no longer
   exercised there.
11. earlier_turn_damage runs on MemoryIO: same checks, no full flushes.
