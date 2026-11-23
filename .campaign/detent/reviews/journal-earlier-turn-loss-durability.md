# journal-earlier-turn-loss review: durability

Commit: journal:921301d6f55362fd17d8718ae3e058d2287f926f
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The evidence jobs ran at journal:e0a47a3, which is 921301d plus docs and CHANGELOG only: the code
reviewed is the code they ran.

## Evidence I relied on

- A1 261123-072804-625133 (e0a47a3, exit 0, 317 checks, 0 failures) and 261123-072807-0eb04a
  (fca82bb, the test on main's `turn_log.h`, exit 1 as required). Its 30 failures are all the new
  checks ("damage to an earlier turn is reported", "a group recovery keeps holds every
  acknowledged record"), at U1 and U3 shapes in the same block and in later blocks for seeds 4
  and 6 (4096 B) and 22 (2048 B), plus the large header. `large_header_tear` passes at main, so it
  is a guard, not a fix check. The test checks values: each group not lost must equal the model.
- A2 261123-072809-40f146: at main `blocks all: SILENT: 23`, at the fix `reported: 4807` (0
  silent); `newest all: SILENT: 36 / reported: 165` and `blocks-newest-turn SILENT: 29` identical
  at both; `newest tail: 427 tears, 0 read as damage or loss` at both; 4619 checks, 0 failures.
  This answers risk 3 at probe scale: earlier-turn damage is now always reported, each kept group
  holds every acknowledged record ("reported" requires that), and the newest-turn Limit and the
  tear behavior are unchanged.

## What I checked myself

Scripts in `.scratch/detent/review-journal-earlier-turn-loss-durability/` (Detent home):
`run.sh forge_probe` and `cost_build.sh` + `cost_probe`.

1. **Premise (risk 1), by reading.** One write is in flight per log: `stage` refuses a second
   staged turn and refuses everything after a failure (`turn_log.h:446-447`); `failed()` and the
   open-record `write()` set `failed_` (`turn_log.h:537,727`), so no write follows a failed
   barrier. `append`, `TurnDriver::commit` (`turn_driver.h:315`) and `TurnDriver::run`
   (`turn_driver.h:362`) call `completed()` only after `TurnBarrier` has seen the write complete
   and then its `SyncData` step (`turn_driver.h:84`). Segment rollover happens inside `encode`
   for the same single write; a new epoch opens a fresh segment number; preallocation and
   recycling write other files. Each loop's log is its own prefixed file set (`host.h:321`). The
   premise holds for every writer in the repository; a later fragment of the same segment and
   epoch can only come from a later write.
2. **Search completeness (risk 2), by reading.** Every verifying fragment carries the full
   segment number at `at+8`, so the anchor (its bytes from the first nonzero one, little-endian)
   occurs at `at+8+zeros`; the search starts at `from+8+zeros`, steps by one (overlaps found),
   and stops when a 24-byte header no longer fits. It misses no fragment starting at or after
   `from`. Stale bytes of a recycled file name an older segment and cannot verify. A short First
   fragment (under 9 payload bytes) is skipped, which can only miss a proof, never invent one.
3. **Forged payload (risk 2), probe jobs 261123-075026-43644c (fix) and 261123-075027-b5cdac
   (main).** A torn newest write (block 0's second sector left old) whose record payload holds a
   CRC-valid fragment of this segment and epoch naming turn 1000. Plain tear: both read a tear,
   group 50 kept. Forged in the failing fragment's sector: both report damage and lose group 50
   (pre-existing, `damaged_fragment`). Forged in a later sector of the block: main reads a tear,
   the fix reports damage and loses group 50. See F1.
4. **Cost (risk 4), strict job 261123-074635-bf37ff.** `later_turn` per block: zeros 0.1-1.4 us,
   random bytes 0.4-6 us, at segments 1, 256 and 2^40 alike (the anchor removes the 600 ms
   zero-byte case). Adversarial block (a header naming the segment every 24 bytes, each claiming
   the rest of the block): 0.03 ms at 4 KiB, 1.0 ms at 32 KiB, 4 ms at 64 KiB: quadratic in the
   block, bounded per block. See F2.
5. **The recover change, by reading.** `at_least` and the backward `later` pass push and pop one
   entry per gap in the same range, so they stay balanced. Within one write every record names
   the same turn (copies and end-of-turn use `turn.turn`), so a torn write's own surviving
   sectors never exceed `at_least`. A tail gap before the next segment's first turn compares
   T+1 with T+1 and stays a tail. Marking adds only `gap_damage` and naming; kept records are
   unchanged, so the change can over-report but never drop more.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | A CRC-valid fragment of this segment and epoch naming a later turn, inside a payload of the torn newest write, anywhere later in the failing block, turns a plain crash into reported damage: every group with no later manifest is lost (group 50, acknowledged and untouched, in the probe). Main already did this within the failing sector; rule 6 widens it to the block. Never a silent loss, and it needs a deliberately forged checksum (or raw turn-log bytes of another log with equal segment and epoch numbers carried as a payload) plus a crash during that write. `turn-log-damage.md` Limits does not state it for rule 6. | 261123-075026-43644c vs 261123-075027-b5cdac; `turn_log.h:235-249` | Owner's call: keep it and add one Limit line (recommended; O3b's per-sector check removes it), or accept as is. Narrowing the search would bring back silent losses. |
| F2 | no | nit | `later_turn` is quadratic in the block for adversarial bytes: 1 ms per 32 KiB block, 4 ms per 64 KiB. A crash reads one block per failure; only a crafted file with a failing fragment in every block reaches about 2 s per 64 MiB segment. | 261123-074635-bf37ff | None for durability; a security review may cap candidates per block. |
| F3 | no | nit | The Limit quotes the newest turn's 36 of 201 but not the probe's other newest-turn bucket, `blocks-newest-turn` 29 of 108 silent, unchanged by the fix. | 261123-072809-40f146 log lines 35, 77 | Name both counts in the Limit, or say 36 is the newest-tail layout only. |

## Not run

- A3 (261123-072811-92707d) was still running and CI run 45205524348 was partly green (tsan,
  freebsd) when I wrote this; A5 is the lead's landing gate.
- No new crash-model run of my own: the format and the writer's ordering are unchanged, and A2's
  probe already crashes the workload; I did not rerun it at other seeds or block sizes.
- No Debug or sanitizer build of my probes; no Linux run of them.
- Session: run as a subagent of the lead, not a session of its own titled
  `detent · reviewer journal-earlier-turn-loss durability`.
