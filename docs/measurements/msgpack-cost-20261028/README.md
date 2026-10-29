# Bounded codec cost qualification

**Date:** 2026-10-28. **Verdict:** Adoption remains blocked by snapshot encoding CPU. The scoped-packer prototype is discarded; the consumer source remains Detent:4c37ca6.

The initial batch contains 96 measured processes across snapshot and delta inputs, two key widths and two input fragment sizes. The follow-up contains 72 measured processes comparing the private codec, the initial extraction and a prototype reusing one packer per row/header. Each process has a matching warmup. Five plain pairs or triplets per case supply CPU measurements; observed runs separately record allocation events and instrumented owner-turn maxima. Raw JSON includes binary and source hashes, corpus hashes, exact methods and the discarded patch.

Snapshot encoding medians in microseconds per iteration:

| Key bytes | Fragment bytes | Private codec | Extraction | Scoped packer | Scoped/private |
| --- | --- | --- | --- | --- | --- |
| 34 | 37 | 286.60 | 302.02 | 287.18 | +0.20% |
| 34 | 4096 | 273.62 | 291.17 | 299.83 | +9.58% |
| 200 | 37 | 365.74 | 387.61 | 382.54 | +4.60% |
| 200 | 4096 | 361.55 | 392.61 | 384.93 | +6.47% |

Admitted peaks remain identical across all three arms: 460,528 bytes for 34-byte snapshot keys and 919,616 bytes for 200-byte keys. Every encoder output chunk must match the captured logical input and dedicated accounts must refund completely. These cases do not measure compressed frames, transport, fleet fanout or large-cardinality qualification. Instrumented maximum step times include observation overhead and do not establish a production tail-latency bound.

The first follow-up launch fails before recording a measured sample because the copied initial executable lacks its executable bit. Its failure record is preserved separately. Restoring that bit leaves the executable byte hash unchanged; the complete follow-up passes 72/72 processes under an exclusive host hold.

Reproduction requires the captured wire-before corpora, the exact baseline and candidate builds and the matching executable for each arm. The scripts preserve their original scratch paths as provenance; binary hashes must match before comparing results. The JSON records are evidence, not a portable standalone benchmark package.
