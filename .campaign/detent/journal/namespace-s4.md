# namespace-s4: journal

State: working (implementing; first build and baseline pending)
Quest: .campaign/detent/quests/namespace-s4.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

S4.1: the recovery that follows a tier's own publication takes the trusted path (`.read`/`.live` bound to the root,
no pack read, no page hash). `TrustedRestart::open` now fills the plan's admission registry (cold) or carries the
prior's mappings (warm), and leaves the packs it just built ungated; `generation.cc` selects it whenever
`trusted_start && !progress`. S4.2: counters (`delivery::HashStats`, `CoordinatorStatus::hash`, relay status JSON,
agent log) from `paged::hash_counts()`, new `built_hash_counts()` and `distribution::frame_hash_counts()`.
Test: `tests/detent_delivery/test_hash_once.cc` (modes agent, relay, agent-delta, relay-delta, restart, damage) on
a fixture extracted from test_repair.cc into `tests/detent_delivery/scripted_upstream.hh`.

## Next

1. Build `.scratch/build`, run the new tests and the suite; fix what recovery-after-publication tests assumed.
2. Baseline: the same test with generation.cc's condition reverted (the mutant patch, also A1's fails-without).
3. A3 mutant patch (scrub no compare), docs (docs/mechanisms/trusted-restart.md, docs/status/namespace-s4.md, CHANGELOG).
4. Submit jobs: `campaign verify`, A1 native, A2 strict (10K and 100K), A3 test and revert.

## Blocked on

Nothing blocking, one file-set mismatch for the lead: the quest's file set is `src/detent_delivery/**`,
`src/detent_relay/**`, their includes, `tests/detent_delivery/**`, `docs/status/namespace-s4.md`, but the outcome
(no tier re-validates its own build; hash counters) lives in `src/detent_local/` (`generation.cc`,
`trusted_restart.*`, `paged_layout.*`, `paged_packer.cc`, `paged_segment_builder.cc`), with frame counters in
`src/detent_distribution/` (`frame_encoder.cc`, `framing.cc`, a new header), the agent's log in
`src/detent_agent/managed_host.cc`, and docs in `docs/mechanisms/`. No change inside the set can remove the
recovery's hashing. I edited those files; `land` refuses edits outside the set, so extend the file set
(`campaign set namespace-s4 files=...`) before landing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
