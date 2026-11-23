# journal-turn-sector-checks: journal

State: waiting on jobs 261123-104119-1fb4f2 261123-104121-0dca59 261123-104123-1ecb5a 261123-104126-e17d15 261123-104128-58508b 261123-104143-3708f8 261123-104146-ca1f89
Quest: .campaign/detent/quests/journal-turn-sector-checks.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done at journal:dea3757 (journal:2e17407 format, tests and fixtures; journal:dea3757 doc and
CHANGELOG), pushed for ci:ci. Jobs queued for A1-A4.

## Next

1. When the jobs finish: read A1's main baseline (newest SILENT 36 of 201) and the fix's 0 SILENT;
   A4's numbers against the design (bytes -5.6 to -6.2% at <=700 B, about +1.0% at 4 KiB; latency
   within the A/A band); fill Evidence and Result.
2. After the lead widens the file set (Blocked on): fix lost_bound_test.cc and host_test.cc, run
   the suite, ci:ci green (A6).

## Blocked on

The lead: the file set. The local suite at journal:dea3757 fails in tests outside it, because they
place records at RFTW0001 offsets:
- test/lost_bound_test.cc (bounds, crash, second: "turn A's batch of group 2 is in block X", then
  "faultfs view: No such file or directory"; spans() hard-codes RFTW0001 offsets);
- test/host_test.cc (idle_damage, idle_unmanifested: "the woken group's batch is in the log").
Options: (1) widen the file set to those two tests (recommended: they need offsets mapped through
data positions, payloads resized; no behavior change); (2) a follow-up quest, accepting a red ci:ci
here. Docs outside the set still say RFTW0001 or derive its costs: docs/mechanisms/turn-seal.md
(bound +65/126), docs/mechanisms/framing.md, AGENTS.md, ARCHITECTURE.md, include/journal/host.h
and include/journal/framing.h comments, bench/turn_bench.cc's comment, docs/design/host-layout.md;
widen to them too, or leave them to the lead.

Also: journal_turn_log ran 73 s alone locally (60 s in journal-earlier-turn-loss's job
261123-072811-92707d) and timed out at its 120 s under ctest -j4 with load; most of it is
round_trip and reclamation's full flushes (PosixIO), untouched here. CMakeLists.txt is outside
the set; watch ci:ci.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

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
