# Shared HTTP client adoption

**Updated:** 2026-10-27. **State:** Qualification candidate; no main adoption. The [component review](../reviews/http-adoption-component-2026-10-27.md) records two framing blockers. The resource first-read gate remains open independently.

| Check | Evidence and remaining work |
| --- | --- |
| Reconciliation | Detent:dab891d restores the saved client component onto the isolated resource candidate. Current checked delivery wire bounds, reset diagnostics and HTTP completion logging remain present. |
| Native integration before framing guards | All products build. The focused delivery/HTTP suite passes 54/54 in 136.10 seconds, including held-response and 500K paged transitions. Four explicit local prerequisite overrides are used; this is not final public-pin qualification. |
| Library baseline qualification | Allocator, parser and compressor candidates pass eight hosted jobs each. http:be9efc1's default and client profiles pass 16 jobs in run 40599941236. These suites precede the new framing regressions. |
| Chunk-extension controls | The new request/response split-point regression fails before the fix and passes natively after it. http-parser:87ae8cf passes its full hosted matrix in run 40630737877. |
| Queued input and reuse | The old cache fails the regression at its post-return live-entry assertion. http:cde8795 adds guards and POSIX/Asio/TLS coverage, including arrival after lease return. Updated native and hosted results are pending. |
| Public consumer closure | The candidate pins allocators:4b39819, http-parser:87ae8cf, compressors:86ff17d and http:cde8795. Hosted consumer qualification must fetch these immutable revisions without local overrides. None changes the production manifest on main. |
| Remaining acceptance | Close both review findings, run affected failure/lifetime checks and the composed product/platform matrix, verify optional dependency profiles and Xapiand compatibility, then record matched production-path costs. Library branches remain isolated until acceptance. |

The [blueprint](../HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md) defines the component gates. Scratch state is under `.scratch/http-adoption-20261027/`, including recovery patches, original and fixed worktrees, compiler failure logs and regression results. The parser state and deflate constructor/layout changes require all C++ consumers to rebuild with matching revisions.
