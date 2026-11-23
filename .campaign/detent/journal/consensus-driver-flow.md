# consensus-driver-flow: journal

State: waiting on jobs 261123-105043-139c3a 261123-105047-a9361e
Quest: .campaign/detent/quests/consensus-driver-flow.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Committed and pushed consensus:10eb12e (WorkerLimits::flow to all four Core builds; docs; driver_test flow_reaches_core, passes locally 830 checks). A1 job 261123-063949-57aa77; A2 mutants (local branches mutant/driver-flow-create 46e0f17, -recover 80c7c8e) jobs 261123-063953-58e218, 261123-063957-782827; CI on the pushed branch is A4.

## Next

1. When the three jobs finish: paste `campaign brief` Evidence; check CI (A4).
2. Fill Result; State ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261123-063949-57aa77 (passed at 10eb12e; the merge touched none of its files) |
| A2 | 261123-105043-139c3a (create drops the Flow), 261123-105047-a9361e (recover drops it), --fails-without on mutant branches rebuilt on f7a44f3; first round 58e218, 782827 passed at 10eb12e |
| A3 | `docs/DRIVER.md` (Replication flow) names WorkerLimits::flow and GroupOptions::scheduling.flow as the option, Flow{} its default |
| A4 | `ci:macos-arm64`, `ci:ci` (CI 45229726672 at consensus:f7a44f3, green) |

## Result

WorkerLimits::flow (default Flow{}) is the Flow of every Core a Worker builds (create, recover, the validation Core); a Driver host sets GroupOptions::scheduling.flow, e.g. Flow{4, 4 MiB, 256 KiB}. An invalid Flow (pipeline or window 0) throws invalid_argument when the group opens. Test: driver_test flow_reaches_core (three-voter leader, create and recover): Flow{4, 4 MiB, 256 KiB} keeps 4 appends in flight to a silent verified peer and sends the backlog in frames of at most 256 KiB; the default keeps 1 flight and frames above 256 KiB. Mutants dropping the option at create or at recover each fail it. Docs: docs/DRIVER.md (Replication flow), docs/mechanisms/append-pacing.md, CHANGELOG.md 2026-11-23. Opens nothing; strata::group sets the Flow after this lands.

## Rulings I made

- The option is `WorkerLimits::flow` (default Flow{}), not a new Worker constructor parameter or a second GroupOptions field: Worker takes its Limits/Timing/scheduling positionally (inserting a parameter breaks callers outside this file set), and GroupOptions already passes `scheduling` whole to Worker, so one field serves both (`GroupOptions::scheduling.flow`). Strata sets `o.scheduling.flow = Flow{4, 4 << 20, 256 << 10}`.
