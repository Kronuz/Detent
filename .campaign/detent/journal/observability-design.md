# observability-design: journal

State: working
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
  261109-131328-0ce3a4 (queued).

## Next

1. A8 (D38): add position-indexed exact arms and an interleaved on/off mode to the bench; screen (strict), then
   decide with exclusive runs at 1, 8 and 64 threads.
2. Lead: design re-review. If a rerun fails, read `~/.scratch/campaign/jobs/<id>/a1/log`; check the confirm's
   `confirm-paired.md` against D2's table.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/observability.md`: "D1. What is observed, and its bounds" (per process, per host, per fleet bounds; freshness in D5) |
| A2 | `docs/design/observability.md`: "The pick: sample one lookup in 64 per stripe." Paired D2 table and OB1's gate; confirm (exclusive) 261109-131328-0ce3a4 writes confirm-paired.md; numbers quoted from its same-binary run 261109-120858-28cd50 at d1ce742 |
| A3 | `docs/design/observability.md`: "D7. Leaving the process and the host" (usage page, agent pread, JSON document, Prometheus textfile Q3 and D9, fleet Q4) |
| A4 | `docs/design/observability.md`: "unused, its paired median at 8 threads is" (D6 item 2) |
| A5 | `docs/design/observability.md`: "D8. Privacy and limits" (identifiers and fallbacks never stored, names never labels, caps) |
| A6 | `docs/design/observability.md`: "OB1 Usage table, sampling and arms in the core" (OB1-OB6 with files, criteria, risk, needs) |
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
6. The paired probe is a file in the quest's set (`observability-paired.py`), run by every bench job, so OD-2's
   confirmation is a job artifact, not an offline script.
