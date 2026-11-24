# split-detent: journal

State: waiting on jobs 261124-104010-6046a9 261124-104019-b98664 261124-104033-fe6ba4
Quest: .campaign/detent/quests/split-detent.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Splits committed (service.cc, state.cc); verdicts in docs/dev/splits.md. The local build never got
a light slot (starving behind other jobs), so the first compile is job 261124-104019-b98664: if it
fails, fix includes (service.cc dropped the platform completion includes; each new .cc keeps the
original include list) and requeue. Local split_check at HEAD: 0 errors, 36 warnings (inline,
using, 1 anonymous namespace), notes only for service_runtime.cc.

## Next

1. Read the three jobs; fix a compile error if any.
2. Header self-containment of service_internal.hh and state_internal.hh (compile each alone with a TU's flags).
3. When pushed branch has CI, `campaign verify split-detent`.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/dev/splits.md |
| A2 | split_check at base a06cbf1c8c77: 261124-104010-6046a9; ctest -N before/after: 261124-104033-fe6ba4 |
| A3 | <lead> |
| A4 | build all + ctest authority/delivery/state: 261124-104019-b98664; CI after push |

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
