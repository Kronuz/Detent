# Blocking HTTP cost screen

This partial exclusive screen blocks shared HTTP adoption. It covers the classic plain blocking `HttpConnection::get` path with the same immutable public dependency closure, before and after consumer migration. It does not qualify TLS, streaming delivery, tail latency or fleet behavior.

Three alternating-order pairs complete for 100-knob identity/gzip/zstd responses and the 100K-knob identity response. Each process validates its generated schema outside the timer and verifies every returned body. For 100K identity, median caller CPU divided by requests is 4.95 ms before and 14.64 ms after. These are process-batch averages, not per-request CPU percentiles. The capture stops on the next case; no missing cases are inferred.

`partial.jsonl` contains all 24 completed processes. `metadata.json` records compiler, flags, link lines, dependency revisions and hashes. `blocking.cc` matches its recorded source hash. Process CPU includes the fixture server; peak RSS includes payload construction and schema validation. Setup/compression are outside the measured batch, but connection construction and body verification are inside.

The large gzip failure is a fixture range-iteration defect: an empty intermediate compression block compares equal to the end iterator. The state-based iteration used by the library helper avoids that premature termination. The captured fixture and failure remain unchanged here; any corrected follow-up uses a separate record.

For reproduction, provide matching minimal CMake builds at `cost-before-build` and `cost-after-build`, with the exact dependency manifests in metadata, alongside this directory's scripts. `build.py` reuses the framing-test compile/link metadata. Run builds under `quiet light` and `paired.py` under `quiet exclusive`. The original scratch workspace is `.scratch/http-adoption-20261027/`.
