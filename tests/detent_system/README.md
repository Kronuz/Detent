# System qualification runner

This directory implements the bounded process and evidence foundation for [Stage 6](../../docs/STAGE6_ARCHITECTURE.md). The authority writer driver runs three real voters; relay, agent and transport-probe orchestration remain pending. The JSON presets describe intended workloads and do not establish qualified capacity.

The runtime requires Python 3.12 or later. Scenario loading rejects unknown, duplicate and out-of-range fields, then checks descriptor and disk capacity before launch. At most 16 child processes are owned. Repeated cancellation waits for an accepted launch and for child drainage; launch and evidence failures still drain already-owned children. A child that exceeds its shutdown deadline is killed and fails the run.

Evidence is capped by `artifact_bytes`. Child output drops after its share of the budget is exhausted, preserving one quarter of small budgets (up to 64 KiB) for control records. Exhausting the control-record budget fails the run. Metadata command output is bounded to 64 KiB (4 MiB for source-path inventories), with a ten-second deadline and child reaping on failure. Archive dependencies retain a content fingerprint; matching an upstream revision requires the frozen-build manifest.

```sh
/usr/local/bin/python3 -m unittest discover -s tests/detent_system -p 'test_*.py'
```

CTest registers the same lifecycle checks when Python 3.12 or later is available. These checks exercise real child processes, including launch failure, repeated cancellation and evidence-write failure. They do not qualify daemon throughput or fleet behavior.

## Sustained authority writes

```sh
/usr/local/bin/python3 tests/detent_system/run_writers.py \
  --scenario tests/detent_system/presets/smoke.json \
  --binary build/src/detentd --build build \
  --certificates build/tests/detent_authority/tls-fixture \
  --output .scratch/stage6/writers-17 --seed 17 --duration 60
```

The output directory must not exist. Generate the authority TLS fixture through CTest first. The writer-only driver uses the preset's record and producer counts; subscriber and durable-agent counts remain reserved for the complete runner and are not launched here. Its preflight conservatively retains their disk and descriptor requirements.

Each producer owns its namespace, issues one command at a time and retries the exact command after an unknown transport outcome. Logical capacity rejection consumes its producer sequence and preserves the previous oracle value. Bootstrap capacity rejection fails the run. Retryable admission responses, terminal capacity rejections and reconciled transport unknowns have separate counters. Service-side admission is available in sampled health; client attempt counts alone cannot establish admission.

After intake stops, every configured definition is read through quorum-backed reads and compared with a bounded integer oracle. Every producer's latest receipt is independently queried. A sampled health snapshot must drain publisher requests, body/reply reservations, active connections and retired log credit before graceful shutdown. Source fingerprints include untracked source contents; git dependency revisions and archive content fingerprints are recorded alongside the executable hash. Archive fingerprints do not assert an upstream revision. Reader/relay semantics, namespace contention and failure injection remain subsequent components.

A native macOS 60-second development run with 16 producers and 1,000 records received 2,541 mutation acknowledgments, verified every final definition and retained receipt, and drained all three daemons. This single run establishes no fleet capacity or comparative performance claim. The instrumented five-second follow-up also passes health drainage. CTest runs the five-second workload as `test_system_authority_writers`.
