# Reader capsule decoder fuzz qualification

**Status:** Bounded decoder and prefix run passes; complete capsule adoption remains conditional.

The complete capsule candidate at Detent:b02f7dc receives 544,315 libFuzzer inputs in 61 seconds with no crash, ASan finding or UBSan finding. The preceding 100-input smoke passes. This is a bounded safety check, not an exhaustive proof or a storage-I/O fault test. The [component review](../../reviews/reader-capsule-component-2026-10-29.md) retains platform, canonical-closure and latency conditions.

The driver probes raw decoding and prefix validation, then mutates a valid 624-byte capsule. It optionally repairs nested commitments and reseals metadata before probing full capsules and truncations. Successful decodes must re-encode byte-identically. Original-root prefixes span lengths 0 through 320; capsule prefixes span 0 through 624. Existing deterministic fixtures separately exercise every capsule truncation.

`build_fuzz.py` preserves the exact scratch build controller. It copies dependency sources at the manifest's public pins and verifies their revisions, then compiles the local-view and builder closures with LLVM 23, `-O1 -g -UNDEBUG`, coverage and ASan/UBSan. It expects the retained `.scratch/reader-capsule-20261029/fuzz` layout and the public-pin SDK dependency cache named in that script. Restore these inputs there before replaying. `DETENT_FUZZ_CAPSULE` names the included seed, captured from the independently cloned 5M fixture. The seed contains generated benchmark metadata, not production configuration.

The complete raw run and smoke logs are included. The evolving corpus and empty findings directory remain in the scratch run. No claim about first-read latency, throughput, full-system leak freedom or production default follows from this result.
