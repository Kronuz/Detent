# split-detent: journal

State: working
Quest: .campaign/detent/quests/split-detent.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Splits committed (service.cc, state.cc); verdicts in docs/dev/splits.md. Compiling the touched
libraries locally (light hold) before queueing the suite and split_check jobs.

## Next

1. Local build of detent_authority, detent_authority_service, test_authority_state, test_authority_service; fix includes.
2. Header self-containment check for service_internal.hh and state_internal.hh.
3. Commit docs/dev/splits.md; queue `campaign verify split-detent --for A4` and the split_check and ctest -N jobs (A2); push.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/dev/splits.md |
| A2 | <job> |
| A3 | <lead> |
| A4 | <job> |

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
