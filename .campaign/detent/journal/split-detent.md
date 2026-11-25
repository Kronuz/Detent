# split-detent: journal

State: waiting on jobs 261124-163504-d828ec 261124-163509-8a377b 261124-163515-7a21e0 261124-163520-fb800b 261124-163525-786dd8 261124-163531-c6ce31, and CI
Quest: .campaign/detent/quests/split-detent.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round: state_internal.hh lacked #include "detent/validation.hh" (is_valid_group_key); the first compile (job b0dc9b) failed on it. Class swept: every new TU built (detent_authority, detent_authority_service, test_authority_state/service built locally) and each _internal.hh compiled alone twice (-fsyntax-only, only an unrelated OpenSSL deprecation warning). Jobs below are the final-head evidence.

Splits committed (service.cc, state.cc); verdicts in docs/dev/splits.md. The local build never got
a light slot (starving behind other jobs), so the first compile is job 261124-115843-c31888: if it
fails, fix includes (service.cc dropped the platform completion includes; each new .cc keeps the
original include list) and requeue. Local split_check at HEAD: 0 errors, 36 warnings (inline,
using, 1 anonymous namespace), notes only for service_runtime.cc.

## Fix round 3 (a7d28d73)

1. a63f81: history-not-gated and layouts-no-enable did not apply. Main had already changed that code (history_reply takes held_file; the leader proposes EnablePositions as it leads, so history_wanted is gone): they were stale on main too, not only after the move. Repointed: history-not-gated puts `true ||` before `serving()` at the history route; layouts-no-enable (its premise is gone) stops the leader from proposing the enabling entry (`&& false`). Both pass `git apply --check` at the head.
2. 8e3551: its three mutants were killed; the exit 1 came from the job's own pattern check: `return leader_known &&$` (also in revert_followup_check.sh no-leader) no longer matches since leader_alive continues with `trusting()`; the mode had been broken on main too. Pattern now `return leader_known && trusting()`; the no-leader mode runs as its own job fb800b.
3. restart_wake: 1d97f0 discarded ctest's output, and the machine's load was 140-215 on 16 CPUs while I tried to rerun it, so no local run started (quiet holds light runs above load 32). The test asserts stop under 200 ms and start under 1 s, wall-clock; its code is untouched by the split (moved verbatim, same loop). The x100 jobs print the failing iteration's output and the load at that moment; main's tree is source-identical to 31d9d0e (only campaign files since). Counts below when they end; a failure whose message is a stop/start time bound under load is load, not the split.

## Jobs at a7d28d73

suite d828ec; relay mutants 8a377b; history mutants 7a21e0; no-leader check fb800b; restart_wake x100 head 786dd8, main (31d9d0e4) c6ce31.

## Next

1. Read the six jobs above; on a restart_wake failure, compare the counts and the printed iteration output.
2. Header self-containment of service_internal.hh and state_internal.hh (compile each alone with a TU's flags).
3. When pushed branch has CI, `campaign verify split-detent`.

## Blocked on

Nothing. The lead widened the file set; citations repointed at 0793c894 (check_docs.py, the relay and history mutants and revert checks, four mechanism pages). Dated design records (docs/design/*, LIBRARY_ADOPTION_ARCHITECTURE, ONLINE_CAPACITY_ARCHITECTURE, docs-site-audit) left alone; `check_docs.py` passes locally ("33 gaps, 33 listed, 0 unlisted, 0 stale"), so none of them breaks it. revert_ready_check.sh drops the split pieces after restoring BASE's one-file service (untested: BASE is an old commit).

Jobs at the final head (0793c894 code): build + full ctest b44ae2; split_check 2b5161; ctest -N 82572f; check_docs 82971a; relay mutants a63f81; history mutants 8e3551. restart_wake x20: main 094de0, head 1d97f0 (counts to add when they end).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/dev/splits.md |
| A2 | split_check at base a06cbf1c8c77: 261124-115837-303a96; ctest -N before/after: 261124-115848-86a98f |
| A3 | Lead read the split (2026-11-24): service.cc -> options parsing, Runtime, internal header; state.cc -> codec, application, administrative, image, internal header; moves only (76 non-move lines: CMake lists, includes, inline on helpers lifted from anonymous namespaces into the internal headers). service_runtime.cc stays 2,364 lines: Runtime::run is one ~2,050-line function, a restructure, not a move (follow-up). |
| A4 | build all + ctest authority/delivery/state: 261124-115843-c31888; CI after push |

## Result

service.cc (3,613) split into service.cc, service_options.cc, service_runtime.cc, service_internal.hh;
state.cc (2,447) into state.cc, state_codec.cc, state_application.cc, state_administrative.cc,
state_image.cc, state_internal.hh. Kept whole: coordinator.cc (an 825-line class body, own quest,
and an open branch edits it), the two design models. Follow-ups for the lead: restructure
Coordinator::Impl (SPLITTING.md "Restructuring a large class"); tests/detent_authority/test_state.cc
(1,084 lines) is outside this file set.

## Rulings I made

1. Base for split_check is a06cbf1c8c77 (the quest's start).
2. Shared helpers go to `detail` in service_internal.hh / state_internal.hh with `using namespace detail;`, as SPLITTING.md says; helpers one piece uses stay in that piece's anonymous namespace.
3. test_coordinator.cc (1,346 in the quest) no longer exists on main.
4. The lead widened the file set with docs/dev/splits.md and test_state.cc (msg). test_state.cc is kept whole: one test program, topic functions of 40-115 lines over shared builders, 84 lines over the threshold; a split would only externalize anonymous-namespace functions (verdict in splits.md). splits.md notes test_coordinator.cc is gone. The relay-mirror-idle-wake quest will edit the loop in service_runtime.cc; moved verbatim, behavior untouched.
5. coordinator.cc and the two design models are kept whole (docs/dev/splits.md has the reasons); the open branch managed-failover-isolated-leader edits coordinator.cc.
