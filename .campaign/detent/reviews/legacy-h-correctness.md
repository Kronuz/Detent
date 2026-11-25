# legacy-h review: correctness

Commit: Detent:5cc302f8e6ecef7dede6722d09d2282185994372 (code head of `quest/detent/legacy-h`; Detent:dceeb890
adds only the journal). Fix round 3 is Detent:021dfa5e (F5's fix and D7's lifecycle table) and
Detent:41f67db8 (D155's pages), merged with main at Detent:5cc302f8. Rounds 1 and 2 are in this file's
earlier versions (Detent:2d6b6349, Detent:d4bd4f6e).
Verdict: APPROVE
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

F5 is fixed, and I found no other hole in the entry's lifecycle. A namespace the table no longer
lists watches the store word, so the publish that re-adds it, at any slot, makes its next lookup
attach the new entry. I listed the entry's states and events from the code before reading D7, then
drove every transition through the agent store (the test writer), with hand-made tables only where
the agent cannot go. All 26 checks pass at this commit:

- A sequential model checks every lookup by value through 3,600 random deltas, drops, re-adds (at
  the old slot, at a new one, into another name's old slot), agent restarts, collections,
  `detent_refresh` calls and reopened handles: 0 wrong of 12,880.
- Six reader threads on two handles, with no `detent_refresh` anywhere, through 199 drops and 199
  re-adds in 60 s: 406 million lookups, no value backwards or past what was published, and every
  namespace converges by lookups alone.
- The model, the threaded run and round 2's `regrow` all fail the F5 mutant (Detent:6ea8a1f2).

The fix costs one refresh per handle per publish, at the first lookup of a dropped namespace after
it: what a never-listed namespace already pays. What is left is documentation and one dead check: D7
misses four transitions and states two rules the code does not follow (F6); three design pages still
say a slot is never reused (F7); D155's pages call `refresh()` the one call that reports a failing
namespace without saying when it returns OK anyway (F8); and F3's slot check no longer guards
anything (N2). None blocks.

## Status of every finding

| ID | Round | Status | Evidence |
|---|---|---|---|
| Old F1-F3 (e30799b8) | 0 | Fixed (round 1) | As recorded at Detent:2d6b6349 |
| F1 open, refresh and lookups fail while a namespace is added | 1 | Fixed (round 2) | At Detent:5cc302f8, `window` passes, and 120 s of `stress` (adds and drops under 8 reader threads and a refresh thread): 1,007 publisher steps, 371 million lookups, 29,184 `detent_refresh` calls, none failed, 0 stale |
| F2 one namespace's failed attach freezes the others | 1 | Fixed (round 2) | At Detent:5cc302f8, `couple`, `retry` and `faildrop` pass |
| F3 slot ignored when the generation id repeats | 1 | Fixed; the check is now redundant (N2) | Round 1's `readd` passes at Detent:5cc302f8 (5, as a new handle reads), and still passes with the slot check alone removed |
| F4 a dropped namespace's lease blocks a revisited-cursor re-add | 1 | Open, non-blocking, pre-existing | Unchanged; D7's "Dropped" row names the follow-up |
| F5 a re-added namespace is not followed | 2 | Fixed | `core.cc:561-566`. Suite `regrow`; fails without: 261124-075309-3e8f55 (Detent:6ea8a1f2 stops at `test_reader_core.cc:209`, the read after the re-add). My model, `readdstress` and `regrow` fail the same mutant |
| N1 a failing namespace answers OK; only `detent_refresh` reports | 2 | Owner's ruling D155 keeps reader.md D4 | Stated in the guide and the three references; gaps in F8 |

## Fix round 3: the four questions

1. **Is D7 complete?** No, though the code is right in each case it leaves out. From
   `agent_store.cc`'s publish and `core.cc`'s `refresh_locked`, `lookup` and `detent_refresh`, a
   namespace is, per handle, absent, attached, attached with its newest attach failed, or dropped
   (kept, watching the store word), with a first-attach failure remembered at the store word beside
   these. The events are a publish of its entry, another namespace's publish, a drop, a re-add (at its
   old slot, at a new slot, with the same generation id, at an older position), another name taking
   its old slot, an agent restart (`bump_all`, `agent_store.cc:246`), a refresh started by its own
   lookup, by another namespace's or by `detent_refresh`, a refresh lost to another thread or refused
   for a pinned view, and a whole failed refresh. D7 misses:
   1. A re-add at the old slot. The agent re-adds a name at its old slot whenever that slot was the
      highest (`sameslot`: c at slot 2, back at slot 2); D7's rows cover a re-add at a new slot and
      another name taking the slot. All four handles in the probe, listed and unlisted, with and
      without a lookup while it was dropped, read the re-add and the delta after it.
   2. An agent restart. Every word moves once; each handle refreshes once at its next lookup, keeps
      every unchanged generation, keeps a dropped namespace dropped and attaches only what is new
      (`restart`: one refresh per handle, one more for `v`'s first attach, nothing re-attached; a
      delta and a re-add after it are followed).
   3. A re-add the A4 check refuses, at an older position of the same incarnation. The handle keeps
      the dropped generation with OK and `detent_refresh` returns `source_failure`, while a new handle
      reads the re-added one (`regress`, a hand-made table: 3 against 2). A monotonic authority never
      sends it; it is A4's case, but the table should name it.
   4. A drop or a re-add while the namespace's attach fails. It keeps its last good generation through
      the drop and the re-add, then follows its next publish (`faildrop`: 1, 1, 4, 4, 4, then 7).

   Two of D7's rules are wrong. "A new namespace takes the slot past the highest slot of the entries
   the cycle keeps": the agent takes the highest before it removes the cycle's drops
   (`agent_store.cc:437-446`), so a cycle that drops the highest slot and adds a name gives the name
   the slot after it; a dropped slot returns only in a later cycle, and only if it was the highest.
   "It retries when its slot moves or at `detent_refresh`": every refresh of the handle retries it,
   whatever started it (`core.cc:559-573`). In `retry`, `u` made 3 attempts during 3 publishes of `t`
   alone, and recovered on `t`'s next publish, with its own slot unmoved and no `detent_refresh` since
   its record came back.

2. **Rows against the code and their tests.** Every row matches the code but for that retry clause,
   and my probes reproduce each row but "First attach failed", which rests on the suite
   (`refresh_memo`, `values`). The tests check by value: `per_namespace`, `adding_namespace`,
   `entry_failure`, `refresh_memo`, `values`, and `regrow`, which kills the F5 mutant. Two cited tests
   do not prove their row's claim. `slot_reused` passes at the F5 mutant too (its replica `slotreuse`
   passes in `local/f5mut.log`: the new name's add bumps the old slot the mutant watches), so the row's
   "the dropped name, the store word" rests on `regrow`. `readd_same_generation`'s fails-without
   (Detent:84c40203) reverts two mechanisms, the slot check and `add(generation, listed->slot)`; with
   the slot check alone removed it stays green (N2). Its row is reached only through hand-made tables:
   through the agent, a handle that still holds the dropped generation makes the re-adding cycle throw
   `LeaseHeld` (F4; `held_lease` in `test_agent_store`). And `entry_failure`'s comment "its last
   generation, without retrying" (`test_reader_core.cc:172`) is wrong: each `t` lookup's refresh
   retries `u`.

3. **The cost of watching the store word.** No lookup pays a refresh on every publish. A dropped
   namespace's entry records the store word its refresh read: the first lookup after a publish
   refreshes, the rest answer from the new view. In `cost`, per unrelated publish and per handle, a
   dropped namespace costs 1.00 refresh, 1.00 `selected` read and 1.00 view (100 publishes at 16
   namespaces, 40 at 512), exactly what a never-listed one costs, and an attached one costs 0. The
   first lookup after a publish takes 116 µs median (p90 222 µs) at 16 namespaces and 890 µs (p90
   1.37 ms) at 512, against 86 µs and 937 µs never-listed: the table read grows with the store. The
   lookups after it take 0.7-0.8 µs, as an attached namespace's do. It is bounded by the agent's publish
   rate: at one publish a second in a 512-namespace store, a process that reads a dropped namespace
   spends 0.09% of a core and one lookup of about 1 ms per publish. If that latency matters later, an
   agent that gives a re-added name its old slot, and that slot to no other name, lets a dropped
   namespace watch its slot again for nothing (F7's second option).

   No missed wakeup: the store word and every sequence are release `fetch_add`s on a file never
   replaced (Limit 2 aside), the agent bumps after the rename, and the refresh loads the word before
   it reads `selected` (D4 rule 1), so a re-add the read missed has moved the word past what the entry
   recorded. No ABA: words only grow, a reused slot is bumped at the drop and again at the add, and the
   dropped name no longer watches it. The model (133 slots taken by another name, 52 re-adds at the old
   slot) and `readdstress` (6 at the old slot, 193 at a new one, under six threads) find none.

4. **D155 in the guides and references.** Consistent where it is stated: the Python and Rust guide
   (`guides/python-and-rust.md:77-84`) and the Python, Rust and C++ references say a namespace that
   cannot attach answers its last good publish with OK while the others move on, and only `refresh()`
   reports it; `errors.md:80` ("Keep answering from the installed view") agrees, and both bindings pass
   `detent_refresh`'s status through. What they leave out is F8.

## Evidence I relied on

- The lead's jobs at Detent:5cc302f8: native 261124-075318-9b7397 (220 of 220, `test_reader_core`
  among them), bindings 261124-075331-f4ad0a (`ctest --repeat until-fail:3`, exit 0), and the
  fails-without mutants 261124-075309-3e8f55 (F5: `reader core invariant at line 209`) and
  261124-075314-c9ce1c (F3's pair of mechanisms: line 278). I accept them as records; my probes
  reproduce what each carries.
- CI 45430641919 at Detent:5cc302f8: success. Every job that ran passed (the plan skipped two), Linux
  ASan/UBSan and TSan, both macOS jobs and both bindings jobs included.

## What I checked myself

The scripts are in `.scratch/detent/review-legacy-h-correctness/` in the home: `round3_probe.cc` (this
round's modes), `review_probe.cc` (round 2's) and `local3.sh`, which builds one `git archive` of
Detent:5cc302f8 (FetchContent through the campaign's git mirrors) and runs three phases: the commit,
`slot-check-only.patch` (the slot check alone removed), and Detent:6ea8a1f2's `core.cc` (the F5
mutant). I ran it under `quiet light` (hold `r3-local`, 08:47-09:11). The campaign queue held light
jobs behind a drain, so I cancelled the same runs as jobs (261124-081437-fc6a3f, -081440-f5ea76,
-081443-e4c6ea, -082257-088a0e); `run3.sh`, `mutant_slot.sh` and `mutant_f5.sh` rerun them as jobs.
Logs: `local/base.log`, `local/slotmut.log`, `local/f5mut.log`.

At Detent:5cc302f8 (`local/base.log`, 26 checks, 0 failed):

- `sameslot`, `slotreuse`, `restart`, `retry`, `faildrop` and `regress`: the cases in questions 1
  and 2, by value.
- `readdstress:60`: 324 publisher steps (199 drops, 6 re-adds at the old slot, 193 at a new one, 23
  collections), four reader threads on a handle of all and two on `{q1, q4}`, no `detent_refresh`:
  406,235,796 lookups, 269,099,433 OK, no status outside the handle's rules, no value backwards or
  past what was published, no `unavailable` after OK; then 0 stale by lookups alone, after the race
  and after a drop, lookup, re-add and delta of `q1` and `q4`.
- `cost:16:100:200` and `cost:512:40:200`: question 3.
- `model` seeds 1-3: 3,600 operations, 12,880 lookups checked, 0 wrong. Summed over the seeds: 1,339
  drops, 1,281 re-adds at a new slot and 52 at the old one (1,110 to a handle that looked the name up
  while dropped, 802 to one that did not), 133 slots taken by another name, 189 agent restarts, 384
  collections, 113 reopened handles, 2,451 lookups of a dropped namespace, every `detent_refresh` OK.
- Round 2's `lists`, `window`, `couple`, `readd` and `regrow` pass, and the 120 s `stress` in the
  status table.

With the slot check alone removed (`local/slotmut.log`, 7 checks, 0 failed): `test_reader_core`
(every case, `readd_same_generation` included), `sameslot`, the model at seeds 1-2 (0 of 2,859 and 0
of 2,917 wrong), `readd` and `regrow` pass.

At the F5 mutant (`local/f5mut.log`, 8 checks, 4 failed): the model fails at both seeds (128 of
2,141 and 106 of 2,190 lookups wrong; the first, a `{c,e}` handle reading c at 6 where the publish at
12 lists it at 12), `readdstress` leaves 3 stale reads on `{q1, q4}` (q1 at 15 where 253 is published), and
`regrow` reads 1 after the re-add and the delta where a new handle reads 6. `sameslot`, `slotreuse`
and `readdstress`'s invariants pass there: a re-add at the old slot and another name taking the slot
both bump the slot the mutant watches, and a stale value is never a backward one.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F6 | no | gap | D7's table misses four transitions the code handles: a re-add at the old slot (the agent's case whenever the dropped slot was the highest), an agent restart, a re-add A4 refuses, and a drop or re-add while the attach fails. Two of its rules are wrong: the new-slot rule (the agent takes the highest slot before removing the cycle's drops) and the failed attach's retry (every refresh of the handle retries it, not only its slot moving or `detent_refresh`). `entry_failure`'s comment repeats the second | Question 1; `agent_store.cc:437-446`, `core.cc:559-573`, `test_reader_core.cc:172`; `sameslot`, `restart`, `regress`, `faildrop`, `retry` in `local/base.log` | Add the four rows, with `sameslot`, `restart` and `faildrop` ported to `test_reader_core` as their tests (they use only the writer) and `regress` cited; correct the two rules and the comment |
| F7 | no | gap | Three pages say a slot is never reused: reader.md D4 (`docs/design/reader.md:141`), namespace-delivery D8 (`docs/design/namespace-delivery.md:231`) and `docs/mechanisms/change-file.md:17`. The agent reuses the highest dropped slot for the next name added (`agent_store.cc:437-457`, unchanged from main), as D7 now says. After F5 no reader rule depends on it, but the pages contradict D7 and the code. All three are outside the quest's file set | `sameslot` (c back at slot 2); the model's 133 slots taken by another name | Pre-existing; the lead's to place. Options: amend the three pages to the agent's rule, or make the agent never reuse a slot and give a re-added name its old one (it must keep the slots of dropped names; it would also let a dropped namespace watch its slot again, question 3) |
| F8 | no | gap | D155's pages call `refresh()` the one call that reports a failing namespace (`reference/bindings/python.md:58-61`, `rust.md:66-68`, `cpp-api/reader.md:106-109`, and `guides/python-and-rust.md:81-84`: "A process that must know calls `refresh()` from a thread of its own"). `detent_refresh` returns OK without trying while another thread holds `refreshing` (`core.cc:836-837`), which a lookup's refresh after any publish does, and when a pinned view refuses the publish (`entry_failure` is reset at `core.cc:456`, before the refusal at `:464-469`). The guide's "tries again at its next publish or `refresh()`" understates the retry (any refresh). `observability.md` lists only lookup statuses as read-health signals and alerts, and a failing namespace no longer returns one; only its staleness alert catches it. No page says what a dropped namespace answers. reader.md D4 (`:144`, outside the file set) still says a failed attach "keeps the installed view", which F2 narrowed to that namespace | The lines cited | Say in the three references and the guide that `refresh()` returns OK while another refresh runs or a pinned view refuses it, so a periodic call reports a lasting failure; or have `detent_refresh` wait for the running refresh and run its own. Add `refresh()`'s status to observability.md's signals. One sentence on a dropped namespace (its last values with OK in a process that read it; `unavailable` in a new one). Reword D4's sentence to F2's behavior |
| N2 | no | nit | F3's slot check (`core.cc:523`, `previous->slot == entry.slot`) no longer guards anything: `add(generation, listed->slot)` already makes a kept generation watch its entry's slot. Where it fires, it attaches the same generation a second time, and only hand-made tables reach it (F4) | `local/slotmut.log`: with the check alone removed, `test_reader_core`, `sameslot`, the model, `readd` and `regrow` pass | Both are correct; the owner's call. Drop the check and `Generation::slot` (read nowhere else), or keep it and record that it has no fails-without of its own |

## Not run

- Sanitizers locally: TSan and ASan at this head rest on CI 45430641919.
- F4 end to end: the test writer cannot revisit a cursor.
- Linux probes: macOS only.

This review ran as a subagent of the lead's session, not as a session titled
`detent · reviewer legacy-h correctness`.
