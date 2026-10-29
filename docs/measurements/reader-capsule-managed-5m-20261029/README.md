# Complete capsule real-publisher trial, 5M knobs

**Status:** Functional lifecycle passes; latency acceptance fails. Capsule and resource adoption remain blocked.

The canonical-resource capsule candidate Detent:30fee44 prepares and restarts a cloned, already seeded 5M corpus through the real foreground agent. Its fresh complete native suite finishes with 230 passes and one privileged-path skip in 825.83 seconds. The 1,194 publication fault/cancellation cases across both encodings pass in that suite. This is native correctness evidence, not privileged or complete supported-platform qualification.

The exclusive trial checks 40 saved keys across five rounds, alternating private/canonical resource order and running a separate fresh-process warmup before every measured process. It measures both a live publisher and a stopped publisher sequentially. Each measured process checks its first value, then performs 100,000 repeated lookups. The private probe is built at Detent:b02f7dc; the canonical probe and publisher are built at Detent:30fee44. Binary hashes and host identity are in `result.json`. Inputs are the preserved `managed-5m-r3` generation and [forty key/value cases](../../../benchmarks/prepared-pack-access-5m-20261025.json). No reseeding occurs.

| Publisher | Resource closure | First reads below 1 ms | Median first, µs | Maximum first, µs | Median repeated mean, ns |
| --- | --- | --- | --- | --- | --- |
| live | private | 199/200 | 704.523 | 1126.646 | 1390.515 |
| live | canonical | 200/200 | 702.725 | 858.338 | 1374.500 |
| stopped | private | 198/200 | 800.380 | 1256.620 | 1395.490 |
| stopped | canonical | 193/200 | 799.332 | 1333.923 | 1360.825 |

The requirement remains 200/200 below 1 ms in each closure. The live private row fails it; stopped rows also fail. Every measured key/value and 5M-record health count is correct, and no first-read sample reports a major page fault. The outliers include time in both attachment and first lookup. Those counters do not identify a root cause; no unchanged retry or relaxed gate follows. Publisher states are sequential, so their median difference is not attributed causally to stopping the daemon.

Daemon preparation takes 119.273 seconds and prepared restart takes 109.325 seconds. These are full daemon recovery/readiness operations, not SDK attachment. The real publisher changes the selected generation to a 624-byte capsule, preserves the names of all 1,441 existing pack files, and restarts without republishing that prepared head. The controller checks pack names rather than comparing every pack byte. Both supervised daemon exits return zero without forced termination. Publisher peak RSS is 1,541,398,528 bytes during preparation and 1,540,644,864 bytes during restart. Whole-reader maximum RSS ranges up to 1,220,608 bytes across the captured samples; these kernel counters are not mapped virtual size or Darwin physical footprint.

The bound, non-listening upstream intentionally prevents ingestion during the trial. The live daemon records reconnect timeouts while preserving its durable selection. It remains alive throughout live sampling. Every publisher has a parent-death supervisor, a 300-second wall-clock limit and bounded shutdown drainage; neither bound fires.

`result.json` preserves the controller's original `status: passed`, which means lifecycle/value checks completed. It does not score latency. `qualification-summary.json` explicitly records failed latency acceptance. Both raw samples and logs are retained. The controller is copied unchanged; restore it under `.scratch/reader-capsule-20261029/` with its named frozen build directories and preserved input before replaying under `quiet exclusive`. It refuses to overwrite an existing trial directory. Repeating the same batch is not the next diagnostic.

The next bounded task must attribute attachment and first-lookup costs on the captured slow keys, preserve file-trust and page-verification semantics, and demonstrate a measured improvement before another acceptance trial. Production metadata remains Legacy by default.
