# observability-design: journal

State: waiting on jobs 261109-120856-787b87 261109-120857-5b7ebc 261109-120857-3fb43d 261109-120858-28cd50
Quest: .campaign/detent/quests/observability-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design drafted in `docs/design/observability.md` (D1-D9, Q1-Q6, build plan OB1-OB6); its D2 cost table,
abstract result sentence and job ids are placeholders (NUMBERS, RESULT SENTENCE, COST TABLE, JOBID) until the
confirm job lands. Chains (slot `obs-bench`, ref `s6=b1d281c`):
- `15b6978`: build 261109-103954-f7533a failed to compile (`~AtomicCounter()` on an alias); rest skipped.
- `fce36ed`: build 261109-104451-e928ee ok, smoke 261109-104452-5b4e92 ok, screen 261109-104453-f581b5 FAILED
  ("invalid knob definitions": `--knobs 100000` built 200,000 definitions, past `kDefaultMaxKnobs` 100,000;
  smoke's 40,000 fit), confirm 261109-104454-a6435c skipped. Fixed at `438191b`.
- `d1ce742` (bench fix `438191b` + journal): build 261109-120856-787b87, smoke 261109-120857-5b7ebc,
  screen 261109-120857-3fb43d, confirm 261109-120858-28cd50 (queued behind another quest's exclusive hold).

## Next

1. If build or smoke fail, read the log under `~/.scratch/campaign/jobs/<id>/a1/log`. Read the confirm job's `confirm.md` (`$JOB_OUT`, or its log); put its numbers in D2 of the design; choose the counting shape by them.
2. Commit the design, then rerun the confirm job at the head (`campaign job rerun 261109-104454-a6435c`, with the build, smoke and screen; the doc
   commit changes job identity) and cite both.
3. Fill the Evidence table, Result; set State to "ready for review".

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <job id at the current code> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. `--knobs` counts knob definitions (int and bool halves), so screen and confirm publish 100,000 knobs, the
   parser's default cap (about 8 MB of JSON, under the 16 MiB cap). The bench parses its definitions with
   `parse_defaults` before publishing and fails with the limits and the parser's detail.
2. Smoke uses the same 100,000-knob set as screen and confirm (setup about 5 s), so a definition-set failure
   shows in the smoke, not the timed runs.
