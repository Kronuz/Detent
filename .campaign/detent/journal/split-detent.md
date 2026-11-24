# split-detent: journal

State: blocked on lead: widen the file set for the outside-set citations below; waiting on jobs 261124-094de0 (restart_wake x20 at main) 261124-125737-1d97f0 (x20 at head)
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

## Next

1. Read the three jobs; fix a compile error if any.
2. Header self-containment of service_internal.hh and state_internal.hh (compile each alone with a TU's flags).
3. When pushed branch has CI, `campaign verify split-detent`.

## Blocked on

Lead: widen the file set, then I edit (all are moves of a citation, no behavior):
1. tests/docs/check_docs.py:164 reads service.cc for the `names[]` result table (CI check-docs fails on `assert names`): it lives in service_runtime.cc now.
2. tests/detent_relay/mutants/{absence-trusted,history-not-gated,layouts-no-enable}.patch name src/detent_authority/service.cc: their hunks are in service_runtime.cc.
3. tests/detent_authority/history_mutant.sh: `old-fetch-bound` patches service_options.cc now (`accept-low-fetch-bound` stays service.cc).
4. tests/detent_authority/revert_followup_check.sh lines 18-24: `leader_alive`, `leader_known` are in service_runtime.cc.
5. Left alone: tests/detent_authority/revert_ready_check.sh (one-off against an old BASE, already stale: src/detentd/main.cc).
6. Docs citing service.cc/state.cc by line: docs/mechanisms/{format-refusal,authority-retained-state,authority-reply,authority-capacity-raise}.md, docs/design/{namespace-delivery,observability,http-coroutines}.md, docs/ONLINE_CAPACITY_ARCHITECTURE.md, docs/LIBRARY_ADOPTION_ARCHITECTURE.md, docs/dev/docs-site-audit.md (docs/status and docs/reviews are dated records; not edited).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/dev/splits.md |
| A2 | split_check at base a06cbf1c8c77: 261124-115837-303a96; ctest -N before/after: 261124-115848-86a98f |
| A3 | <lead> |
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
