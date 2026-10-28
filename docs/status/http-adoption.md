# Shared HTTP client adoption

**Updated:** 2026-10-28. **State:** Qualification candidate; no main adoption. The [component review](../reviews/http-adoption-component-2026-10-27.md) closes both framing findings at source level, conditional on qualification. The resource first-read gate remains open independently.

| Check | Evidence and remaining work |
| --- | --- |
| Reconciliation | Detent:dab891d restores the saved client component onto the isolated resource candidate. Current checked delivery wire bounds, reset diagnostics and HTTP completion logging remain present. |
| Native integration before framing guards | All products build. The focused delivery/HTTP suite passes 54/54 in 136.10 seconds, including held-response and 500K paged transitions. Four explicit local prerequisite overrides are used; this is not final public-pin qualification. |
| Library baseline qualification | Allocator, parser and compressor candidates pass eight hosted jobs each. http:be9efc1's default and client profiles pass 16 jobs in run 40599941236. These suites precede the new framing regressions. |
| Chunk-extension grammar | http-parser:d2eb839 rejects malformed names, values, quotes and controls while preserving valid whitespace and quoted bytes. Native tests pass; hosted run 40673316543 passes all jobs. The additional byte-at-a-time fixture in http-parser:e9b0c48 passes natively in 0.44 seconds; its hosted run is pending. |
| Queued input and reuse | http:c64fb43 checks the TLS input BIO as well as SSL and transport buffers. Fixed and chunked separate-record regressions reproduce the old cache retention failure. The full native HTTP suite passes 14/14 in 6.86 seconds. Current hosted qualification is pending. |
| Public consumer closure | The candidate pins allocators:4b39819, http-parser:d2eb839, compressors:86ff17d and http:c64fb43 without local overrides. None changes the production manifest on main. |
| Consumer startup failure | Run 40670848521's macOS ARM snapshot-catchup case reports two daemon bind failures (`Address already in use`). The fixture releases all six reservations before starting children. This is a concrete startup failure; attribution to the HTTP migration is not established, and the fixture still needs repair and qualification. |
| Remaining acceptance | Verify the composed product/platform and sanitizer matrix, optional dependency profiles and Xapiand compatibility, then record matched production-path costs. Before/after minimal builds use the same public dependency closure. Library branches remain isolated until acceptance. |

The [blueprint](../HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md) defines the component gates. Scratch state is under `.scratch/http-adoption-20261027/`, including recovery patches, original and fixed worktrees, compiler failure logs and regression results. The parser state and deflate constructor/layout changes require all C++ consumers to rebuild with matching revisions.
