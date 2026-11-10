# observability-design: journal

State: blocked on lead: ever_sampled misses OB1 at bool/scattered/1 thread (+7.7 vs 5.0); see Blocked on
Quest: .campaign/detent/quests/observability-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design round 2 at `Detent:51923f8`: review CHANGES at `6139e4c` (`.campaign/detent/reviews/observability-design-design.md`)
folded in, OD-1 to OD-9. D2 now quotes paired per-repetition medians from `docs/design/observability-paired.py`
(the reviewer's probe generalized to any repetition count), which every bench run now writes as `<mode>-paired.md`.
Chains (slot `obs-bench`, ref `s6=b1d281c`):
- `15b6978`: build 261109-103954-f7533a failed to compile; rest skipped.
- `fce36ed`: screen 261109-104453-f581b5 FAILED (knob set past `parse_defaults`' cap). Fixed at `438191b`.
- `d1ce742`: build 261109-120856-787b87, smoke 261109-120857-5b7ebc, screen 261109-120857-3fb43d, confirm
  261109-120858-28cd50, all exit 0; the doc's numbers are from these (same bench binary digest `0d8211c2`).
- `6f718fa`: build 261109-125827-d3567b, smoke 261109-125827-7b2e64 passed; screen bd1c57, confirm bc28dd cancelled (stale).
- `51923f8`: build 261109-131328-ffcdb2, smoke 261109-131328-cbe2c7, screen 261109-131328-647533, confirm
  261109-131328-0ce3a4 (all exit 0).

A8 (D38) chain, same slot and ref, arms `none2,knob_sampled,knob_rows64,pos_shared,pos_stripe_rmw,pos_tls,
ever_bit,ever_sampled,dm_cache` with `--interleave 1 --threads 1,8,64`:
- `beccf27`: smoke 261109-140348-9e0d9e failed (`all` ran A8 arms unpaired; registry push allocated). Fixed `29dcab5`.
- `29dcab5`: build 261109-140527-973349, smoke 261109-140528-6e2a88, exact-screen 261109-140528-498e45, exit 0.
  Screen showed a scattered A/A bias (+1.5 to +5.9 ns): the pair's blocks walked different name segments.
  Hot names: `pos_tls` −1.5 to +1.3, `ever_bit` +0.5 to +4.0 at all thread counts; `pos_shared`, `dm_cache`,
  `knob_rows64` out; `pos_stripe_rmw` +6.1 at 1 thread (locked RMW, bare +6.9). Superseded by `4871285`.
- `4871285` (same-name pairs, both orders, delta = mean of the order medians; sampled slow path out of line):
  build 261109-141656-5650f8, smoke 261109-141657-d683a8, exact-screen (strict) 261109-141658-03e1a9, exit 0.
  A/A (`none2`) within ±1.9 ns in every row: the method fix holds. Realistic table: `ever_bit` passes every
  row (worst +5.1 int/scattered/64); `knob_sampled` fails bool/scattered/1 (+7.8); every exact count fails
  scattered (`pos_tls` +46.6/+57.6 at 64, `pos_stripe_rmw` +59.6, `dm_cache` +36.7, `pos_shared` +31.5 hot/64).
- Merged main at `09f038e` (bench binary unchanged: it builds the S6 ref, bench source digest unchanged).
  Exact-confirm (exclusive, all 9 arms, reps 7, calls 262144) 261109-202834-bfb3cf at `09f038e`, slot obs-bench;
  queued behind another exclusive hold at 20:40.
- Lead message (2026-11-09): owner accepted Q1-Q7 (PLAN.md D47, main `b16115e`); merged main; Q section rewritten
  as "Owner decisions (PLAN.md D47)"; D10 drafted with the ever-bit shape (per-generation bitmaps, D5 fold).

- Exact-confirm 261109-202834-bfb3cf (exit 0) read: `ever_sampled` misses OB1 at 64 threads (int hot +11.9,
  int scattered +10.5, bool hot +11.3, bool scattered +14.4); parts alone pass (`ever_bit` +0.2 to +4.6,
  `knob_sampled` +4.3 to +6.9). Superadditive only with the lookup: bare screen 03e1a9 at 64 threads is
  `ever_bit` 1.6 + `knob_sampled` 2.0 vs pair 4.1.
- Diagnosis so far (disassembly of `block<ever_sampled>` in the slot binary at 09f038e): the bit is already
  test-before-set (load, test, `lock or` only on a clear bit); the countdown is per stripe
  (`stripe_lines[stripe].cell[7]`, its own line), not thread-local, so 64 threads on 54 distinct stripes share
  10 countdown lines; the S6 lookup pins with a `lock xadd` (core.cc:301, seq_cst) that drains the store buffer,
  so the countdown store of the previous lookup must own its line before the next pin. Two branches per lookup.
- Candidates at `4ee258e`: `knob_tls` (countdown thread-local), `es_tls` (bit + thread-local countdown),
  `es_one` (bit + stripe countdown, one branch, cold slow half), `es_tls_one` (both). Build 261109-222812-b203c8,
  strict pair-screen 261109-222819-1a0f6c (arms none2, ever_bit, knob_sampled, ever_sampled + the four; bare too).
- Build b203c8 done (bench digest 15b89164). Disassembly at 4ee258e: `es_tls`/`es_tls_one` hold the TLV address
  in a register (no per-lookup TLV call); `es_one`/`es_tls_one` compile to load, load, test/je, and/je to one
  cold `ever_slow` call, with no per-lookup stack store of `frame`; `ever_sampled` keeps two duplicated tails,
  a `frame` byte store per lookup and a countdown pointer reloaded from the stack on one path.
- D10 table filled from bfb3cf at `ea0afdb` (A8TABLE, worst, pos_tls miss, A/A, screen agreement, Q7 range);
  the gate item (A8EVERSAMPLED) waits for the pair results.
- Pair-screen 261109-222819-1a0f6c (strict, exit 0, binary 15b89164). Max(int, bool) delta, hot / scattered, ns:

  | arm | 1 | 8 | 64 | bare 64 |
  |---|---|---|---|---|
  | none2 | -1.2 / +0.1 | -0.7 / +0.0 | -0.0 / +0.8 | -0.1 |
  | ever_bit | +2.9 / +2.1 | +2.4 / +2.1 | +2.6 / +6.6 | +1.3 |
  | knob_sampled | +3.2 / +3.9 | +3.0 / +4.4 | +4.0 / +4.4 | +2.0 |
  | ever_sampled | +2.7 / +6.8 | +3.4 / +6.6 | +6.0 / +8.9 | +4.0 |
  | knob_tls | +3.8 / +6.7 | +4.4 / +5.9 | +10.7 / +15.0 | +1.9 |
  | es_tls | +4.4 / +6.7 | +5.1 / +6.9 | +13.0 / +17.6 | +3.4 |
  | es_one | +2.6 / +4.9 | +2.9 / +4.7 | +6.8 / +11.1 | +3.7 |
  | es_tls_one | +8.6 / +7.3 | +8.9 / +7.2 | +11.7 / +16.9 | +3.4 |

  Reading: (1) sharing refuted: a thread-local countdown is worse than the stripe line at every thread count
  (knob_tls vs knob_sampled, es_tls vs ever_sampled, es_tls_one vs es_one, whose fast paths are the same
  instructions); (2) bare costs add (64 threads: 1.3 + 2.0 vs 4.0); (3) `ever_sampled` itself fell from
  +10.5..+14.4 (bfb3cf, and 03e1a9 agrees, binary 904da3e) to +5.9..+8.9 at 64 threads in this binary: the
  `cell_of` refactor turned its status-split loop with duplicated tails into one path with the cell computed in
  the cold branch, and moved the code. The bench cannot split codegen from placement; it shows the 64-thread
  excess is the compiled loop around the lookup, not the pair's instructions. `knob_sampled` int scattered 64 at
  -28.8 in this run is the same kind of effect in the other direction.
- Winner for the confirm: `es_one` (best at 1 and 8 threads) beside the new `ever_sampled`; TLS arms rejected.
  Exclusive pair-confirm 261109-224833-fa1352 at f476cf7 (arms none2, ever_bit, knob_sampled, ever_sampled,
  es_one; reps 7, calls 262144).

- Exclusive pair-confirm 261109-224833-fa1352 at f476cf7 (exit 0): see D10 item 4's table. `ever_sampled` passes
  8 and 64 threads (worst +9.3) but misses bool/scattered/1 at +7.7 (int +4.4 passes); `knob_sampled` alone misses
  the same row (+6.9); `es_one` misses it (+6.1) and bool/scattered/64 (+12.2); `ever_bit` passes every row.
  D10 item 4 rewritten at `aee1c7c`.

## Next

0. Lead: rule on Blocked on. Then: Q7 and D10 follow the ruling; rerun the D2 chain (build, smoke, screen,
   confirm) and the pair-confirm at the head for A2/A7/A8 evidence; ready for review.
1. Abstract, D2 lede and Checks still name only the D2 chain; add the A8 and pair jobs to Checks.
2. Lead: design re-review. If a rerun fails, read `~/.scratch/campaign/jobs/<id>/a1/log`; check the confirm's
   `confirm-paired.md` against D2's table.

## Blocked on

Lead (owner rule: unified shape cheaper first, mixed only on a measured gap). After the fixes, the owner's pair
(`ever_sampled`, D47 Q7) misses OB1 in one row: bool scattered, 1 thread. Exclusive jobs, max(int, bool), hot / scattered, ns:

| arm | 1 thread | 8 threads | 64 threads |
|---|---|---|---|
| ever_sampled before (bfb3cf, 09f038e) | +3.7 / **+7.4** | +3.9 / +6.4 | **+11.9 / +14.4** |
| ever_sampled after (fa1352, f476cf7) | +2.5 / **+7.7** | +2.7 / +6.3 | +6.6 / +9.3 |
| knob_sampled (fa1352) | +2.6 / **+6.9** | +2.8 / +4.9 | +4.5 / +4.0 |
| ever_bit (fa1352) | +2.5 / +2.3 | +2.3 / +1.9 | +3.4 / +6.3 |
| es_one (fa1352) | +2.2 / **+6.1** | +2.7 / +5.0 | +7.7 / **+12.2** |

The miss belongs to the sampled countdown, not the bit: sampling alone misses the same row (+6.9 here, +4.5 at
09f038e, +7.8 in strict 03e1a9), and the pair adds +0.8 on top. The row moves about 3 ns with the compiled layout.
Options:
1. Keep the pair (recommended): it is the owner's shape, passes 8 and 64 threads, and its 1-thread bool miss is the
   countdown's, which every sampled option carries; OB1 re-measures it in the core (stripe in a register, S6
   lookup at about 900 ns, Limit 7), and the countdown's cost there is OB1's to cut.
2. Exact usage only (`ever_bit`): passes every row of both exclusive confirms (worst +6.3 at 64 threads, +2.5 at 1);
   loses sampled counts and arm splits (D2, D4), so "how often" and "which arm" go.
3. Sampled only (`knob_sampled`): does not close the gap (+6.9 in the same row) and loses exact "was it read" (Q7:
   0.98 miss odds for a knob read once).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/observability.md`: "D1. What is observed, and its bounds" (per process, per host, per fleet bounds; freshness in D5) |
| A2 | `docs/design/observability.md`: "The pick: sample one lookup in 64 per stripe." Paired D2 table and OB1's gate; confirm (exclusive) 261109-131328-0ce3a4 writes confirm-paired.md; numbers quoted from its same-binary run 261109-120858-28cd50 at d1ce742 |
| A3 | `docs/design/observability.md`: "D7. Leaving the process and the host" (usage page, agent pread, JSON document, Prometheus textfile Q3 and D9, fleet Q4) |
| A4 | `docs/design/observability.md`: "unused, its paired median at 8 threads is" (D6 item 2) |
| A5 | `docs/design/observability.md`: "D8. Privacy and limits" (identifiers and fallbacks never stored, names never labels, caps) |
| A6 | `docs/design/observability.md`: "OB1 Usage table, sampling and arms in the core" (OB1-OB6 with files, criteria, risk, needs) |
| A8 | `docs/design/observability.md`: "No exact count passes on scattered names." and "The pair now passes at 8 and 64 threads." D10 table: exclusive exact-confirm 261109-202834-bfb3cf at 09f038e; pair: strict screen 261109-222819-1a0f6c at 4ee258e, exclusive pair-confirm 261109-224833-fa1352 at f476cf7 (bench source unchanged since); decision pending, Blocked on |
| A7 | Build 261109-131328-ffcdb2, smoke 261109-131328-cbe2c7, screen 261109-131328-647533, confirm (exclusive) 261109-131328-0ce3a4 at 51923f8; the doc cites the same-binary chain at d1ce742 listed under Doing |

Review findings (round 1, `6139e4c`), all fixed in `51923f8`:
- OD-1: Q7 and Limits 2-3 state the four losses (0.98 / 0.85 miss odds, OB6 deferral, hook adoption, ramp ±4 / ±16 pts); options (a)-(d) for the owner, recommendation (a) kept, not decided.
- OD-2: D2 table now paired medians; OB1 gate: two handles in one binary, interleaved per 64-lookup block, ≤ 5 ns at 1 thread, ≤ 10 ns at 8 and 64, exclusive. Probe: confirm 261109-131328-0ce3a4 `confirm-paired.md`.
- OD-3: `usage/` mode 3733, agent's group; O_EXCL failure falls back to a private table; OB4 adds pre-created page and Linux readability cases.
- OD-4: ninth cell `fallback_other` (not_opened, unavailable, no_memory, source_failure); `samples[9]`.
- OD-5: D4 and Limit 5: attribution to the installed revision at scan, per-revision host rows, error bounded to one straddling scan interval.
- OD-6: D2 items 1 and 4: separate 64-line handle array, seeded at open from the clock, handle address and stripe; random initial countdown; fork child reseeds at its first refresh; OB1 adds a 64-thread case.
- OD-7: D9: Prometheus text renderer in Kronuz/metrics; OB4 criteria for the document (schema, 65,536 cap, `truncated`, revision rows) and the textfile.
- OD-8: D2 item 7 reworded: deltas measured at the S6 baseline; OB1 re-establishes them in the core.
- OD-9: OB1 builds generations with `publish(Snapshot)`; the `publish_json` call of `parse_defaults` is stated.

## Result

`docs/design/observability.md` (D1-D9, Q1-Q7, OB1-OB6). Pick: sample one lookup in 64 per stripe into a
per-handle per-knob table of 9 cells; paired median −4.4 to +6.7 ns per warm lookup at 8 threads. Exact
per-knob counting +23 to +64 ns; shared `fetch_add` +21.7 to +33.6; kronuz::metrics counters +50 to +65.
One thread unresolvable in this bench (`none` spread 221-287 ns); OB1's paired gate resolves it. Open: Q7
(sampled vs dead-knob exactness), the S6 core's own baseline (889 ns hot int at 1 thread, 3.3x D3's 268.5 ns),
Linux page faults (Limit 7).

## Rulings I made

1. `--knobs` counts knob definitions (int and bool halves), so screen and confirm publish 100,000 definitions,
   within the bench's setup check (`parse_defaults`, which goes with `Client`, D13/D14; not a reader property).
2. Smoke uses the same knob set as screen and confirm (setup about 5 s), so a definition-set failure shows in
   the smoke, not the timed runs.
3. Owner correction 2026-11-09 folded in: Reader API only (R1, R2); arms renamed `published`, `no_hash`,
   `ramp_on/off`, `switch_on/off`, `missing`, `wrong_type`; the switch rules are published definition data,
   not R2's override layer. Bench not rerun for setup (lookup cost unchanged); doc says OB1 uses the reader
   tests' generation writer.
4. Counting pick: sampled (D2) over exact rows, on the 8-thread numbers; added Q7 so the owner can take exact.
   The random countdown reload is unmeasured and OB1's to measure.
5. Fallback statuses get one ninth cell (`fallback_other`), not one per status: the handle's exact status
   counters split it, and nine cells cost nothing extra under sampling (D3 item 5).
7. Countdown stays per stripe line: a thread-local countdown measured worse at every thread count (strict screen
   261109-222819-1a0f6c), so neither TLS candidate went to the exclusive confirm; `es_one` did, as the best at 1
   and 8 threads.
6. The paired probe is a file in the quest's set (`observability-paired.py`), run by every bench job, so OD-2's
   confirmation is a job artifact, not an offline script.
