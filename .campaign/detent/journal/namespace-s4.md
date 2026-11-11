# namespace-s4: journal

State: waiting on jobs 261111-092428-184ad3 261111-092435-0ed21e 261111-092439-33747b 261111-093510-b0bb5b 261111-093514-e582da 261111-093518-e23830 261111-093523-a6e823 261111-093529-99825c (pins 261111-092439-6fbbc2 green at 4452eee0)
Quest: .campaign/detent/quests/namespace-s4.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head 4452eee0. Jobs in State: damage stress after the fix 184ad3; native 33747b; A1 mutants 0ed21e, b0bb5b,
e582da; A3 mutant e23830; A3 test a6e823; A2 strict 99825c.

## Next

1. When the jobs finish: paste `campaign brief`'s Evidence table below, fill the A2 numbers into the Result and
   `docs/status/namespace-s4.md`, set State to "ready for review".
2. CI: the lead pushes head; run 43054127674 (0f071900) failed only on Linux legs at the apt/ccache step
   ("Temporary failure resolving azure.archive.ubuntu.com"), before configure; macOS x86_64 and arm64 passed.

## Blocked on

Nothing.

## Findings

- Native 261111-080602-b970f1 failed `test_delivery_hash_once_damage` at line 184. Reproduced with the check split:
  261111-091328-380510 at 98c0abdd, 4 of 160 runs failed (8 at once), all at `scrub.damaged_packs >= 1`. Cause: the
  gauge reports one scrub pass and resets to 0 when the scrub moves to the repaired root (coordinator.cc scrub
  loop; `test_repair.cc` expects 0 after a repair), so reading it after the repair races the scrub tick. A test
  bug, not code: 4452eee0 checks `repair.requested` and `failures`, which only grow. After the fix: 184ad3.
- The relay-delta shutdown hang is pre-existing: probe at main 6cc26585, 261111-080405-ffc37e, exit 124; at the
  quest code ff7a1e20, 261111-080407-249b55, exit 124 (both print "delta published; closing" and do not drain).
  `relay-delta` stays out of ctest (CMake registers agent, relay, agent-delta, restart, damage); the lead opens the
  follow-up. The earlier pair 080104-4bc749 / 080106-08759e never ran the probe.
- Mutants 261111-080611-bdb97c, 080613-d97e81, 080616-194a7b, 080618-672220 gave no verdict: `hash_mutant.sh` wrote
  its build log into a build directory the job checkout lacked. 98c0abdd adds `tests/detent_delivery/hash_build.sh`,
  which configures a missing build directory as native does; the mutant and report runners use it.

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

- The damage test asserts scrub detection through `repair.requested >= 1 && failures >= 1`: with no page read in
  that phase, the scrub's report is the only requester; the A3 mutant (scrub-no-compare) still kills it by timeout.
- Evidence jobs run unchained; only A2 (strict, capped at 10 minutes) keeps `--after` native, for the built slot.
