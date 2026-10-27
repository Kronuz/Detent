# Resource first-read attribution probe

This macOS-only observer separates source-factory construction, Client construction and the first verified lookup. It measures caller-thread CPU with `CLOCK_THREAD_CPUTIME_ID`; process CPU, faults and context switches include background threads. Allocation mode uses the exported `malloc_logger` hook and counts successful caller-thread allocation/reallocation events. It refuses an occupied or unavailable hook and checks the observer before the batch. Allocation-mode timings are not acceptance measurements.

The archived scripts use the qualification workspace layout. Copy this directory into `.scratch/resource-adoption-20261027/diagnostic/` in a checkout containing the prepared 5M corpus and the recorded key manifest. `build.py` links identical observer source against the existing `baseline-build` and `reader-build` CMake probe libraries and flags. `run.py` reads the corpus from `.scratch/fast-client-build/managed-5m-r3/generations` and refuses to overwrite `diagnostic/result.json`. These scripts require those existing artifacts; they do not seed a corpus or reconstruct builds.

Run `quiet check`, then wrap `build.py` with `quiet light` and `run.py` with `quiet exclusive`. The fixed batch uses 40 keys, two counterbalanced rounds, both binaries and both observer modes: 320 measured processes, each with its own matching warmup. All four observer conditions pass a smoke check before the batch. No required production initialization moves outside its measured phase.

The [recorded result](../resource-caller-diagnostic-20261027.json) retains the observer source SHA-256, binary SHA-256 values and every sample. This diagnostic is not an acceptance timing run and does not replace the ordinary uninstrumented probe.
