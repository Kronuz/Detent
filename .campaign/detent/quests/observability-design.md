# observability-design: Design knob observability: usage, resolved values, export

Risk: low (the quest lands a design document; the hooks and exporters it designs are built by
later quests, the reader's hot path among them at high risk). The lead writes this quest;
contributors read it and never edit it. Its machine state is in `state.json`:
`campaign status observability-design`.

## Outcome

`docs/design/observability.md` decides how an operator learns which knobs each service reads,
what values they resolve to (including which arm of a ramp or rollout a caller lands in), whether
they fell back to a default, and how fresh each process's view is, from one host up to the fleet,
without breaking the reader's warm-lookup promise (`docs/design/reader.md` D3: no lock,
allocation or syscall; p50 268.5 ns integer, 475 ns bool). Every cost claim is measured.

## Acceptance criteria

- A1 [artifact] What is observed, with its cardinality bound per process and per fleet: reads per knob, resolved value or arm per knob (ramps and rollouts), default or fallback taken, the generation each process reads and its age, refresh lag: `docs/design/observability.md`
- A2 [artifact] Where it is counted on the read path, and the measured cost on the warm lookup (single thread and 8 threads, against D3's numbers): `docs/design/observability.md`
- A3 [artifact] How counts leave the process and the host: the path from the client to the managed agent (or another collector), the export format(s), and what aggregates fleet-wide, if anything goes upstream: `docs/design/observability.md`
- A4 [artifact] The application hook: what an embedding service can subscribe to (an on-resolve callback or similar), its cost when unused, and how it composes with the built-in counters: `docs/design/observability.md`
- A5 [artifact] Value privacy and limits: what is exported as a value, a hash or an arm label, and how unbounded value sets are capped: `docs/design/observability.md`
- A6 [artifact] Build plan: candidate quests with files, criteria and risk, and what each needs from reader S5-S7 and the agent: `docs/design/observability.md`
- A7 The warm-path cost measurements are campaign jobs (`exclusive` for numbers that decide) the document cites by id
- A8 [artifact] Exact counting (D38): at least three exact per-knob mechanisms beyond D2's rows (for example a counter in the cache line the lookup already reads, per-thread plain counters merged on scan, an exact "ever read" bit with test-before-set), each measured paired against `none` at 1, 8 and 64 threads in exclusive jobs, and a verdict against OB1's gate (≤ 5 ns at 1 thread, ≤ 10 ns at 8 and 64): the cheapest exact mechanism, or the measured reason none fits: `docs/design/observability.md`

## Contracts

Consumes the reader's warm-lookup contract (`docs/design/reader.md` D3, D5) and the managed
agent's existing status surfaces. May propose an application-facing hook API; that API is
reserved to the owner before any build quest freezes it.

## Decisions

1. Owner direction (2026-11-09): observability of knob usage and resolved values (for example
   ramps) is needed; likely counted in the client with hooks, published through a metrics engine.
2. Consider `Kronuz/metrics` first (`kronuz::metrics::Counter`, `AtomicCounter`, duration
   histograms; header-only, no allocation; already a Detent dependency in the authority). It has
   no exporter, so the export path is part of this design. Any other dependency needs a measured
   case and `docs/SHARED_LIBRARY_DECISIONS.md` in the Xapiand repo read first.
3. The warm lookup's cost is the constraint: a shared `fetch_add` per lookup was removed from the
   reader for cost (`docs/design/reader.md:39`); prefer per-thread or striped counters, sampling,
   or counting off the hot path, and measure each option.
4. Keys stay generic (PLAN.md, Candidate uses).
5. The design is reviewed once (lens `design`) before any implementation quest starts.

## May land early

None.
