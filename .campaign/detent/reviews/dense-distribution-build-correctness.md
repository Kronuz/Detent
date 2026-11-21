# dense-distribution-build review: correctness (round 2, with the security questions, D135's one review)

Commit: Detent:eb3b443e53a2bb17f6cb0d479d48bbf516d84a3d
Verdict: APPROVE
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 1 (CHANGES) was at Detent:a0a435b4. This round covers `git log a0a435b4..eb3b443e`: the review
fixes, the relay page tier (D7.2/D7.4), history ranges, the encoder's take() fit, the merges of main
(loop-idle-waits' owner wakes) and the chain test's slow syncs. The branch head Detent:06d2a5d3 adds
journal text only.

## Evidence I relied on

Each job's log read, not its journal line.

- F1: `261121-095153-93fd64` at the merge Detent:95eab96b, round 1's own probe (the honest transfer
  of namespace `bad` served as `big`): "received 0, refused 1 ... fell back to records", exit 0
  (round 1: exit 3, published).
- F2: `261121-095212-4c9253`, test_relay_pages under ASan+UBSan at the merge, status 0 (answer_owns
  included); `261120-141032-1ff8f3`, the answer-borrows mutant under ASan: ASan abort in
  test_relay_pages, killed. answer_owns is round 1's probe ported to the new build path (two
  namespaces in one answer, the second's build lands a relocated copy that replaces the first's).
- F3: `261121-095235-fe4798`, f3-cut-prefix-dropped (the agent never holds a cut prefix) killed by
  test_pages_coordinator at the merge.
- F4: `261121-095326-f5ccfd`, the 5M relay window bench, after arm, at the merge (numbers below).
- F10/D139: `261121-095237-6685ff`, d139-agent-publishes-below killed by test_namespace_coordinator.
- Also read: d1-root-after-write `261121-095239-fbb807` (killed, 8); stopped-relay-unwatched
  `261121-095309-093a8f` (exit 1 wraps "status 124", the harness hangs under 3.14 without the shim:
  a kill); relay pages under 3.14 `261121-095306-d80db9` and test_pages_coordinator x10
  `261121-095301-c6fa2f` passed; 5M row `261121-095330-92f248` passed; forced root
  `261121-095217-c9cac6` passed; F11 fuzz hour `261120-131414-94ce66` exit 0.
- At the head: mirror-answer-unwoken `261121-120500-bb7f6a` killed (status 8, the chain's one-reader
  fetch ends "relay upstream: deadline"); test_http_upstream unmutated `261121-120503-fef5bb` and
  x5 `261121-120536-71ecd1` passed (2.6 s each).
- CI 44933674727 green at Detent:be35e33e. CI 44944355224 at the head was queued when I wrote this
  (only linux-benchmarks done, green): A2 is the lead's gate, not established here.

## What I checked myself

Scripts in `.scratch/detent/review-dense-distribution-build-r2/` (home checkout); each runs at the
head as a job.

PROBES

Code read, per area the lead named:

- F1 at the head: `Receiver::bound` (paged_transfer.cc:148-159) accepts a definition leaf only as
  'K' name 0 local 0 (longer than n+2 bytes) and a watermark only as exactly 'W' name 0, the shapes
  `encode_definition_key` and `encode_namespace_key` write (paged_layout.cc:82-101; names hold no
  NUL, `is_name_char`). A named full copy needs exactly one watermark (:385-386); a delta keeps one
  (paged_transfer_plan.cc:110-112); base pages a delta reuses are the agent's own. `receive_image`
  refuses an empty name (:419). The coordinator passes the frame's name on both paths
  (coordinator.cc:452, :534), and a delta that does not apply (`invalid_argument` after
  `TransferRefused`, which is caught first) asks a repair (:676-681), so round 1's stuck namespace
  cannot recur.
- F2 at the head: `answer()` (namespace_relay.cc:380-438) moves every copy, delta transfer and head
  a `PagesAnswer` span names into `owned` until `encoder.step` ends; `MirrorHistory` references the
  ring map and table entry, which `build()`/`collect()` never touch (`MirrorStore::read`,
  mirror.cc:294, does not change the table).
- F3: the agent's held prefix (coordinator.cc Response, `~Frame`, `pages(offset)`) resumes only a
  frame with the same name, after, target and root, and the whole transfer is still checked against
  the root; the relay resumes only at a chunk boundary of the same root and after
  (namespace_relay.cc:17-21).
- Relay page tier: `build()`/`collect()`/`copy()`/`delta()` (namespace_relay.cc:449-641),
  `build_generation`/`relocate`/`full_copy` (page_copy.cc), `PageWindow` (page_window.cc). A window
  segment is counted once by id; `retained_bytes` is what older generations alone reach. A wrong
  diff could only cost a refusal: the agent's receiver checks every page against its own base and
  the root.
- Encoder take() (frame_encoder.cc:198-220): a fragment at most half its buffer is copied into a
  buffer of its own size from the same account; the copy runs while the full buffer is still
  charged, and a refused copy (StoragePressure, bad_alloc) keeps the full one, so the answer's
  charge never exceeds round 1's. `view()` is the sealed prefix (buffer.hh:36-56).
- Coordinator after the merge: every worker job posts its event to the owner with outstanding work
  tracked (coordinator.cc:277-282); publish()'s admission wait is the candidate timer, which the disk
  loop cancels after each `advance()` (:560, :930-931). The test race fix (Detent:65458af3) is the test
  server's: it now writes and sets a frame's root under one lock; the product sends nil roots only
  when it holds no build at the frame's head (round 1's F5 ruling).
- Mirror answer wake: `finish()` stores `done` (release) before `answered()` (mirror_service.cc:74-80),
  and every answer path goes through it or through submit's refusal (:464-477). `handle()` submits
  the job and fills the slot under `p.mutex` (service.cc:403-411), and `step()` reads slots under the
  same mutex, so an early answer's wake cannot outrun its slot. `OwnerWake` latches a raise between
  `begin_turn()` and `arm()` (owner_loop.hh:22-55).

The 20 ms pause (question 3). It sleeps in the test Voter's `layouts()`, the sync relay A's mirror
thread runs every 10 ms, so a range request often queues behind a sync and is answered after A's
owner went idle (1 h sleep in this harness, no health turn). It adds delay to a thread that already
serializes every job; it reorders nothing, and the only interleaving it makes rarer, a raise landing
between the owner's `begin_turn()` and `arm()`, is closed by `OwnerWake`'s flag by construction.
WAKE_PROBE

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Closed at the head (round 1: yes). A transfer is bound to its frame's namespace on both paths, and an unappliable delta repairs. | `261121-095153-93fd64`; paged_transfer.cc:148-159, :384-386; paged_transfer_plan.cc:110-112; coordinator.cc:676-681 | Done (Detent:7c82da19, Detent:dec05c43). |
| F2 | no | defect | Closed at the head (round 1: yes). A pages answer owns what its frames name until encoded. | `261121-095212-4c9253`, `261120-141032-1ff8f3`; namespace_relay.cc:385, :408, :417-418 | Done (Detent:7dc5c1f3). |
| F3 | no | gap | Closed at the head (round 1: yes). A cut response resumes from its whole chunks, on the agent and the relay. | `261121-095235-fe4798`; test_pages_coordinator (resumes from 1,376,256 of 2,888,084 B), test_relay_pages resume_and_roots | Done (Detent:dec05c43, Detent:6680f762). |
| F4 | no | gap | Closed as built (round 1: yes, owner's call). The relay builds each head over the last (D7.2), serves DD2's canonical copy (D7.4), and its window counts shared segments once. Rerun at the merge beside light holds: cold 10 x 500K 18.6 s CPU, moves 95-1,483 ms, peak resident 1,410 MB, about 1.5x the CPU docs/perf/relay-window.md records (11.4 s cold, 63-912 ms) with no before arm beside it, so the doc's ratios rest on one pair of runs. | `261121-095326-f5ccfd`; docs/perf/relay-window.md | None needed; the lead's 5M round after landing. |
| F10 | no | defect | Closed (owner's D139). A repair names its published floor; the relay sends no copy below it and the agent skips a frame below it. | `261121-095237-6685ff`; coordinator.cc:656-663; namespace_relay.cc:400-403 | Done (Detent:11c155ce, Detent:e7893553). |
DELTA_ROW
| R3 | no | nit | A pages reader parked for a build waits up to one sync interval (250 ms) more when the build lands while another job's answer runs: `build()` calls `collect()` (namespace_relay.cc:524) and takes the landing, so the loop's `take_built() && relay->collect()` is false and `revisit` waits for the next sync. It converges. | mirror_service.cc:390, :415; namespace_relay.cc:524 | Optional: have `collect()` report a landing to the mirror loop (or revisit after any `collect()` that took a build). |

Round 1's F5, F7, F8, F9 and F11 stand as recorded there: F5 and F7 are built and their mutants killed
(`261120-130729-9a8e6e`, `261120-130731-c646f9`), F11's hour ran (`261120-131414-94ce66`), F8 and F9
are rulings in the journal.

## Not run

- CI 44944355224 at the head (A2, and A1's gate): queued when I wrote this.
- TSan for the coordinator's worker events and the relay's builder thread: CI's linux-tsan job at the
  head, queued.
- History ranges (Detent:b064e7b6, Detent:72429b7c) beyond the ranged fetch's digest check
  (http_upstream.cc:284-329: a prefix is kept only for the same file and the whole file's SHA-256 is
  checked before it is returned); their mutants are the contributor's, not rerun here.
- The 5M rounds and the cold-boot measurement against records: the lead's, after landing.
