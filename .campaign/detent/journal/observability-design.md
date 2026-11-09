# observability-design: journal

State: ready for review
Quest: .campaign/detent/quests/observability-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design complete in `docs/design/observability.md` at `Detent:6f718fa` (main `0bd2aba` merged in at `6598f42`).
Chains (slot `obs-bench`, ref `s6=b1d281c`):
- `15b6978`: build 261109-103954-f7533a failed to compile (`~AtomicCounter()` on an alias); rest skipped.
- `fce36ed`: build 261109-104451-e928ee ok, smoke 261109-104452-5b4e92 ok, screen 261109-104453-f581b5 FAILED
  (`--knobs 100000` built 200,000 definitions, past `parse_defaults`' cap), confirm skipped. Fixed at `438191b`.
- `d1ce742`: build 261109-120856-787b87, smoke 261109-120857-5b7ebc, screen 261109-120857-3fb43d, confirm
  261109-120858-28cd50, all exit 0; the doc's numbers are from these.
- `6f718fa` (rerun, same bench digest `0d8211c2`): build 261109-125827-d3567b, smoke 261109-125827-7b2e64,
  screen 261109-125827-bd1c57, confirm 261109-125827-bc28dd (queued).

## Next

1. Lead: design review (lens `design`). If the reruns fail, read `~/.scratch/campaign/jobs/<id>/a1/log`.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/observability.md`: "D1. What is observed, and its bounds" (per process, per host, per fleet bounds; freshness in D5) |
| A2 | `docs/design/observability.md`: "The pick: sample one lookup in 64 per stripe." D2 cost table from confirm (exclusive) 261109-125827-bc28dd, numbers quoted from its same-binary run at `d1ce742`: knob_sampled +6.0/+4.1/+2.6/+7.4 ns at 8 threads vs A/A bands 6.5/11.4/22.0/24.6; one thread unresolvable (bands 30.3-180.2) |
| A3 | `docs/design/observability.md`: "D7. Leaving the process and the host" (usage page, agent `pread`, JSON document, Prometheus textfile totals Q3, fleet Q4) |
| A4 | `docs/design/observability.md`: "unused, it measures +0.9 to +5.5 ns at 8 threads, inside the A/A bands" |
| A5 | `docs/design/observability.md`: "D8. Privacy and limits" (identifiers and fallbacks never stored, names never labels, caps) |
| A6 | `docs/design/observability.md`: "OB1 Usage table, sampling and arms in the core" (OB1-OB6 with files, criteria, risk, needs) |
| A7 | Build 261109-125827-d3567b, smoke 261109-125827-7b2e64, screen 261109-125827-bd1c57, confirm (exclusive) 261109-125827-bc28dd at `6f718fa`; the doc cites the same-binary chain at `d1ce742` listed under Doing |

## Result

`docs/design/observability.md` (D1-D9, Q1-Q7, OB1-OB6). Pick: sample one lookup in 64 per stripe into a
per-handle per-knob table of 8 arm cells; +2.6 to +7.4 ns per warm lookup at 8 threads (0.3-0.8%), inside
every A/A band. Exact per-knob counting +24 to +69 ns; shared `fetch_add` +8.5 to +32.3; kronuz::metrics
counters +46 to +66. Exactness for applications through the hook (+0.9 to +5.5 unused). Open: the S6 core's
own baseline here (889 ns hot int at 1 thread) is 3.3x reader.md D3's 268.5 ns pinned-view number, for S6's
T1 gate; Linux page writeback faults (Limit 5) unmeasured.

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
