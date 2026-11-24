# legacy-h review: correctness

Commit: Detent:1417f2ada7e2ba23ffa3ae164da4078e6f9ec38b (code head of `quest/detent/legacy-h`; Detent:f38aff9c
adds only the journal). Fix round 2 is `git diff a9eb4f77 1417f2ad` less main's merge Detent:c4f30e5c:
Detent:df6a2dc1 (agent order, F1) and Detent:1417f2ad (per-entry attach failures and the slot check,
F2 and F3). Round 1, at Detent:a9eb4f77, is recorded in the previous version of this file (Detent:2d6b6349).
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

F1 and F2 are fixed, and their tests fail without the fixes. One defect remains, and it blocks. A
namespace that leaves the table keeps watching its old `change` slot. The agent re-adds a namespace at a
new slot, so the re-added namespace's later publishes never reach that handle's lookups: they answer
the dropped generation with OK until `detent_refresh` runs or another namespace's refresh happens to
re-read the table (F5). The test writer reproduces it with no hand-made tables. Round 1's "readd"
probe failure, which the lead asked about, comes from the same cause, not from F3.

## Status of every finding

| ID | Round | Status | Evidence |
|---|---|---|---|
| Old F1-F3 (e30799b8) | 0 | Fixed (round 1) | As recorded at Detent:2d6b6349; Docs green again at the round-2 head (lead: CI 45359417762's Docs) |
| F1 open, refresh and lookups fail while a namespace is added | 1 | Fixed | `agent_store.cc:463` grows `change` before the table is written and renamed (`:477`), so `change` covers every slot `selected` names (D4 rule 4). Probe 261124-020141-9d7252 `window`: open, refresh and first lookup at `rename table` read OK (b:k=3). `stress`, 150 s with 55 adds: 0 of 35,810 refreshes fail, no `source_failure` among 520 million lookups (round 1: 414,516 and 358). Lead's rerun 261124-004932-574229 matches (76 adds, 0 failed). Suite test `adding_namespace`; fails without: 261124-004900-19d2cc. A crash between the growth and the rename leaves zero words past the table, which nothing reads |
| F2 one namespace's failed attach freezes the others | 1 | Fixed | `core.cc:549-579`: a failed attach keeps that namespace's generation and watches its entry's slot from the pre-read sequence; the others publish; `detent_refresh` returns the status (`:840`). A failed first attach is still remembered at the store word and returned to its lookups. Probe `couple`: `t` reads 3, then 4, on the handle that attached the broken `u`; `detent_refresh` returns `unavailable`; both recover. Suite test `entry_failure`; fails without: 261124-004904-f88615 |
| F3 slot ignored when the generation id repeats | 1 | Fixed in the code; the probe fails for F5's reason | `core.cc:523` keeps `previous` only at the entry's slot. The `readd` probe still reads 1 because the dropped `a` never refreshes, so the check never runs (F5) |
| F4 a dropped namespace's lease blocks a revisited-cursor re-add | 1 | Open, non-blocking, pre-existing | Unchanged; main's client kept its view the same way. It belongs in a follow-up |
| F5 a re-added namespace is not followed | 2 | Open, **blocking** | Below |

## Evidence I relied on

- The lead's jobs at Detent:1417f2ad: native gate 261124-005004-b6dde8 (220 of 220), targeted
  261124-004922-fda728, bindings 261124-004912-db1945, and the fails-without mutants named above. I
  accept them as records. The two new suite cases (`adding_namespace`, `entry_failure`) are read
  against my probes, which show the same behavior.
- CI 45359417762 at the head (macOS rerun after a lost runner, per the lead). I did not wait for its
  final state, so the sanitizer runs at this head rest on it. At Detent:dec0d978, CI's TSan and
  ASan jobs ran every reader test.

## What I checked myself

Probe job 261124-020141-9d7252 (strict, at Detent:1417f2ad). The scripts are in
`.scratch/detent/review-legacy-h-correctness/` in the home: `build.sh`, `run.sh` and
`review_probe.cc`, with a new `regrow` mode. `lists`, `window`, `couple` and `stress` pass,
no testing symbol is in the shipped libraries, and `readd` and `regrow` fail.

- `regrow`, through `GenerationWriter` (`PerNamespace`) and the agent store alone:
  - Publish `{a, b, c}` (slots 0-2) and read `a:k` = 1 on a handle with no list and on one opened
    with `{a}`.
  - Publish `{b, c}`, which drops `a`. Both handles keep reading 1, as reader-core.md says.
  - Publish `{a:k=5, b, c}`. The agent re-adds `a` at slot 3. Both handles read 1.
  - Publish a delta of `a` to 6. Both still read 1. A new handle reads 6.
- On the `couple` question of whether a failing namespace reports: `u:k` reads OK with its last
  value while its attach fails, and `detent_refresh` returns `unavailable`. That is D4's rule (a
  failed attach "keeps the installed view", and the next attempt waits for its sequence or for
  `detent_refresh`), and round 1 did the same. It is not a new silent path. The one change is that
  `u` retries when its own sequence moves, not at every store-word move. A transient failure
  therefore stays stale until `u` publishes again, `detent_refresh` runs, or another namespace's
  refresh re-reads the table. Making lookups return the error instead would change D4's
  user-visible behavior: that is the owner's call (N1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F5 | yes | defect | A namespace that leaves the table keeps its generation and keeps watching its old slot (`core.cc:561-562`, `add(had.generation, had.generation->slot)`). The agent bumps that slot once, at the drop (`agent_store.cc:444`). It re-adds the name at a new slot (`:456`, the next slot past the current entries) and bumps only that slot and the store word. A lookup checks only its entry's sequence (`lookup`, `entry->sequence`), so it never refreshes. The re-added namespace answers its dropped generation with OK through every later publish of its own. Only `detent_refresh`, or a refresh another namespace's lookup triggers, re-reads the table. A process that reads one namespace (any `{a}` handle, or `detent get` on a long-lived handle) stays stale indefinitely. Main's per-namespace source polled its entry on a refresh thread and would follow | Probe 261124-020141-9d7252 `regrow`: after the re-add and a delta, both handles read `a:k` = 1 while a new handle reads 6. `readd` (round 1's probe, same cause) reads 1 where a new handle reads 5, and 5 after `detent_refresh` | A dropped namespace watches the store word, not its old slot. Build its entry with `sequence = &change->mapping.store()` and `seen = store_seen`, so the next publish of any namespace makes its lookup refresh and re-attach it from its new entry. Test: `regrow` as a `test_reader_core` case (drop, re-add through the writer, a delta), reading the new values on a listed and an unlisted handle without `detent_refresh` |
| N1 | no | nit | A namespace whose attach fails answers its last generation with OK; only `detent_refresh` reports the status. This is D4's stale-on-failure rule, kept from round 1 | Probe `couple`; `entry_failure` in the suite | Owner's call. Options: keep it (D4) and document it in the bindings' guides, or have lookups of such a namespace return the status with the fallback |
| F4 | no | gap | Pre-existing; see the round-1 text at Detent:2d6b6349 | `agent_store.cc` `LeaseHeld`; main's `client_source.cc:132-147` | Follow-up. F5's fix does not change it: a dropped namespace still holds its lease |

## Not run

- Sanitizers locally. TSan and ASan at this head rest on CI 45359417762.
- F4 end to end: the writer cannot revisit a cursor.
- Linux probes: macOS only.

This review ran as a subagent of the lead's session, not as a session titled
`detent · reviewer legacy-h correctness`.
