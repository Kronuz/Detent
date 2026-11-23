# System qualification runner

This directory implements the bounded process and evidence foundation for [Stage 6](../../docs/STAGE6_ARCHITECTURE.md). The authority writer driver runs three real voters; bounded relay and agent cohorts exercise managed failover; transport-probe orchestration remains pending. The JSON presets describe intended workloads and do not establish qualified capacity.

The runtime requires Python 3.12 or later. Scenario loading rejects unknown, duplicate and out-of-range fields, then checks descriptor and disk capacity before launch. At most 16 child processes are owned. Repeated cancellation waits for an accepted launch and for child drainage; launch and evidence failures still drain already-owned children. A child that exceeds its shutdown deadline is killed and fails the run. Writer teardown checks readiness and resource drainage on all three voters before sending termination, including a restarted follower. Shutdown failures report nonzero process codes and a bounded tail of the affected process output.

Evidence is capped by `artifact_bytes`. Child output drops after its share of the budget is exhausted, preserving one quarter of small budgets (up to 64 KiB) for control records. Exhausting the control-record budget fails the run. Metadata command output is bounded to 64 KiB (4 MiB for source-path inventories and tracked diffs), with a ten-second deadline and child reaping on failure. Archive dependencies retain a content fingerprint; matching an upstream revision requires the frozen-build manifest.

```sh
/usr/local/bin/python3 -m unittest discover -s tests/detent_system -p 'test_*.py'
```

CTest registers the same lifecycle checks when Python 3.12 or later is available. These checks exercise real child processes, including launch failure, repeated cancellation and evidence-write failure. They do not qualify daemon throughput or fleet behavior.

## Sustained authority writes

```sh
/usr/local/bin/python3 tests/detent_system/run_writers.py \
  --scenario tests/detent_system/presets/smoke.json \
  --binary build/src/detent-authority --build build \
  --certificates build/tests/detent_authority/tls-fixture \
  --output .scratch/stage6/writers-17 --seed 17 --duration 60
```

The output directory must not exist. Generate the authority TLS fixture through CTest first. The writer-only driver uses the preset's record and producer counts; subscriber and durable-agent counts remain reserved for the complete runner and are not launched here. Its preflight conservatively retains their disk and descriptor requirements.

Each producer owns its namespace, issues one command at a time and retries the exact command after an unknown transport outcome. Logical capacity rejection consumes its producer sequence and preserves the previous oracle value. Bootstrap capacity rejection fails the run. Retryable admission responses, terminal capacity rejections and reconciled transport unknowns have separate counters. Service-side admission is available in sampled health; client attempt counts alone cannot establish admission.

After intake stops, every configured definition is read through quorum-backed reads and compared with a bounded integer oracle. Every producer's latest receipt is independently queried. A sampled health snapshot must drain publisher requests, body/reply reservations, active connections and retired log credit before graceful shutdown. Source fingerprints include untracked source contents; git dependency revisions and archive content fingerprints are recorded alongside the executable hash. Archive fingerprints do not assert an upstream revision. Reader/relay semantics, namespace contention and failure injection remain subsequent components.

A native macOS 60-second development run with 16 producers and 1,000 records received 2,541 mutation acknowledgments, verified every final definition and retained receipt, and drained all three daemons. This single run establishes no fleet capacity or comparative performance claim. The instrumented five-second follow-up also passes health drainage. CTest runs the five-second workload as `test_system_authority_writers`.

`--kill-leader-at SECONDS` kills the elected leader during sustained writes, waits for a replacement and restarts the killed voter from its existing storage. A ten-second run against the publicly fetched frozen authority baseline reconciles 16 transport-unknown commands, verifies all 1,000 definitions and latest receipts, and drains the restarted three-voter group. Compilation ran concurrently on the same host; this is a correctness result, with no failover latency guarantee. An HTTPS-reachable peer partition remains a separate gate.

## Managed failover checks

CTest registers managed authority leader death, reachable former-leader partition, all-members restart, no-quorum recovery, relay fanout and terminal trust rejection. Agents follow each producer's namespace on W5/W6 sessions, from the voters or from two relays mirroring them. The cohort waits until every agent's table entry for each namespace reaches the writers' last revision there; an independent C++ SDK reader then opens each namespace's entry by name and compares every expected integer. Unavailable cases also compare the selected table before and after the outage. Cohorts stop before the publisher connection-drain assertion.

The trust fixture uses two real TLS endpoints and requires zero requests to the secondary after terminal denial. It covers Open authorization and identity rejection, malformed Next frames, certificate hostname and CA rejection, and authorization, identity or malformed-denial rejection during Close after a retryable Next failure. These correctness fixtures establish no throughput or fleet capacity.

## Relay pages

`test_system_relay_pages` (`run_relay_pages.py`) runs dense distribution DD3 end to end on three voters, one relay and two agents: one agent reads the authority and builds from records, the other boots 100,000 keys from the relay's page transfer, logging zero bytes hashed to build, to the same root (the root page's content entry at the same position). A clustered rewrite of 55,555 keys made while the relay is stopped (SIGSTOP) then reaches the warm agent as one pages delta, counted in the relay's `/v1/health` `mirror.pages`. `forcing/relay-root-forced.patch` makes every full copy the relay sends name a wrong root; `forcing/relay_pages_forced.sh` runs the test under it with `--expect-refusal`: the agent refuses the transfer and boots from records, to the same root, and its session then takes the delta as records.

## Flake forcings

`forcing/` reproduces the CI flakes fixed by quest ci-test-flakes. Each `<cause>-forced.patch` opens the failing window (a stall, a delay or a held resource) with the fix kept and must pass; each `<cause>-mutant.patch` opens the same window with the fix removed and must fail. `patched.sh PATCH CMD...` applies one in a disposable checkout and runs `CMD`, usually `loop.sh` (one test, repeated, copies at once), `soak.py` (each copy in its own directory) or `tsan_loop.sh`. Run them from the repository root, through `campaign job submit`, never in a working tree you keep.
