# journal-earlier-turn-loss: Turn log recovery never drops acknowledged turns silently after earlier-turn damage

Risk: high (durability). The lead writes this quest; contributors read it and never edit it. Its
machine state (status, branch, checks, reviews) is in `state.json`: `campaign status
journal-earlier-turn-loss`.

## Outcome

Found by journal-tail-cut-header-design (job 261123-061004-47129e, design doc
`docs/design/turn-log-cut-header.md` at journal:e69ce03, Q3): damage to a turn that is not the
newest, in the newest segment, with 1-5 later writes after it, is recovered silently in 23 of 4807
cases at layout 0 (seed 22 at 2048 B, 11 seeds at 4096 B). Recovery reports nothing and 2-6 groups
come back as of an earlier turn, so several acknowledged turns drop, not only the newest one the
documented Limit allows. Cause as read from the code: the unproven failure ends its block's scan
(`include/journal/turn_log.h:263-267`), the later turns in that block go unread, and the damaged
turn becomes the torn tail (`turn_log.h:833`).

The fix, code only, no format change (design Q3): a fragment of a later write that verifies past
the failing one, anywhere in its segment, proves that write durable, so the failure is damage, not
a tear, and recovery reports it as damage naming what it lost. The newest turn's 36 of 201 silent
cases are the design's O3b, a format change, out of scope here.

## Acceptance criteria

- A1 A deterministic test in `test/turn_log_test.cc` builds the earlier-turn shapes (damage to a non-newest turn of the
  newest segment, later acknowledged writes after it in the same block and in later blocks) and
  checks recovery by value: every later turn that verifies is recovered, or the damage is reported
  with the groups it lost named; recovery is never silent with fewer records than were acknowledged.
  A job runs it at the fix, and `--fails-without` at journal main.
- A2 The design's crash-model probe (journal:e69ce03 as an extra `--ref`) at the fix reports 0
  silent earlier-turn cases of 4807 at layout 0 (23 at main), and the newest-tail counts are
  unchanged (36 of 201): a job.
- A3 No format change: the turn-log fixtures (`test/fixtures/crc32c/turn-log/`) and the existing
  turn log, seal and power tests pass unchanged: the suite job.
- A4 [artifact] `docs/mechanisms/turn-log-damage.md` states the Limit as it is after the fix: what
  recovery reports for earlier-turn damage, and that only the newest turn can still be lost
  silently until O3b.
- A5 The suite passes and `ci:ci` is green at the landed commit (sanitizers and platforms are CI's).

## Contracts

None changed: the on-disk format stays `RFTW0001`; recovery's report gains damage cases it
previously missed.

## Decisions

Lead, 2026-11-23: its own quest, ahead of the design's O3b format change, because it is a code-only
fix to a durability defect in landed code; one durability review before landing.

## May land early

A1's test with the shapes marked expected-to-fail is not worth an increment: one landing.

## Tasks

None.
