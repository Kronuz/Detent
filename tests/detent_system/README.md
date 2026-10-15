# System qualification runner

This directory implements the bounded process and evidence foundation for [Stage 6](../../docs/STAGE6_ARCHITECTURE.md). The workload driver is not yet implemented. The JSON presets describe intended workloads and do not establish qualified capacity.

The runtime requires Python 3.12 or later. Scenario loading rejects unknown, duplicate and out-of-range fields, then checks descriptor and disk capacity before launch. At most 16 child processes are owned. Repeated cancellation waits for an accepted launch and for child drainage; launch and evidence failures still drain already-owned children. A child that exceeds its shutdown deadline is killed and fails the run.

Evidence is capped by `artifact_bytes`. Child output drops after its share of the budget is exhausted, preserving one quarter of small budgets (up to 64 KiB) for control records. Exhausting the control-record budget fails the run. Metadata command output is bounded to 64 KiB, with a ten-second deadline and child reaping on failure. Archive dependency provenance and workload-level results remain pending.

```sh
/usr/local/bin/python3 -m unittest discover -s tests/detent_system -p 'test_*.py'
```

CTest registers the same lifecycle checks when Python 3.12 or later is available. These checks exercise real child processes, including launch failure, repeated cancellation and evidence-write failure. They do not qualify daemon throughput or fleet behavior.
