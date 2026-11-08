# http-client-adoption: EX-2, shared HTTP clients and common TLS

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-client-adoption`.

## Outcome

Detent's private blocking and coroutine HTTP client machinery is deleted; `http` serves it and
`reactor` holds the common TLS, both at public revisions on their mains. Authentic settlement and
the optional server and TLS dependencies of client-only targets are preserved, and the caller CPU
cost is attributed and closed, or brought to the owner as a considerable gap with numbers.

## Acceptance criteria

Sources: `docs/handoffs/http-clients.md` (the release-blocker brief, 2026-10-23); `docs/EXTRACTION_CHECKLIST.md` EX-2 and "Each item requires"; `docs/RELEASE_PLAN.md` RC-1;
`track/http-adoption` (`Detent:e70bad8`); `docs/SHARED_LIBRARY_DECISIONS.md`, HTTP reconciliation (October 27).

- A1 The private client implementations are gone; Detent's clients run on `http` and `reactor` at public main revisions, not the isolated `http:bd3e8e1`: a build-and-grep job and the `pins` gate
- A2 [manual] The `http` and `reactor` changes are on their mains with library tests and supported-platform CI green, and the Xapiand closures verified (EX-2): the lead names the runs
- A3 The full native suite, ASan/UBSan and TSan pass on immutable public sources (EXTRACTION_CHECKLIST): the `native`, `asan` and `tsan` gates
- A4 Wire bytes are unchanged and authentic settlement is preserved (EX-2; RC-1 "compatible bytes"): a job
- A5 Failure and lifetime checks of the client paths pass (RC-1): jobs
- A6 Caller CPU per request, the +13.2% for 100K identity responses and the 8-9 microseconds per small request, is attributed and closed, or the remaining gap goes to the owner with before and after numbers (EX-2; D7): `exclusive` jobs
- A7 [artifact] Measured costs before and after, with the A/A: `docs/perf/http-client-adoption.md`
- A8 [artifact] EX-2's row updated: `docs/EXTRACTION_CHECKLIST.md`
- A9 [manual] Library side (handoff, Acceptance): http's tests cover fixed-length and chunked responses, split headers, malformed input, EOF, conflicting framing, tiny parse and disposal budgets and bounded headers and bodies; a held response write keeps a second handler off that connection until original settlement; abandonment, shutdown, repeated release, held request and response ownership and final resource conservation; on supported platforms and sanitizers: the lead names the http commit and CI run
- A10 All three caller styles and the existing server users are qualified on the shared client (handoff): jobs
- A11 Server-only and client-only dependencies stay optional, and TLS is not mandatory for plaintext-only users (handoff): a configure job per profile
- A12 Application error classification is unchanged (handoff, "preserve wire semantics and application error classification"): a job
- A13 Allocations, admitted peak memory, CPU and connection counts are measured on real production exchange paths, large streamed responses included (handoff): `exclusive` jobs
- A14 [artifact] An inventory of the deleted private parser, cache and deadline implementations (handoff): `docs/EXTRACTION_CHECKLIST.md`

## Contracts

Consumes `http`'s client API and `reactor`'s TLS; changes no Detent wire format. Library API
changes are shared with Xapiand and need the Xapiand lead's agreement first.

## Decisions

1. D5 and D7. Started only after `resource-adoption` (EXTRACTION_CHECKLIST order EX-1, EX-2).
2. `track/http-adoption` is an isolated Codex-era candidate on old pins, "never adopted on the old
   pins" (lead plan); it is a source, not this quest's branch.
3. Library sources (audit, 2026-11-08): the HTTP reconciliation's library candidates are unmerged
   `track/http-integration` branches in four repositories: http (http:bd3e8e1, 10 commits), http-parser
   (http-parser:e9b0c48, 5: opt-in repeated Content-Length), compressors (compressors:e8dbcd6, 4: bounded
   gzip output) and allocators (allocators:4b39819, 2: unique owners); Detent `docs/SHARED_LIBRARY_DECISIONS.md`,
   "HTTP reconciliation (October 27)". Earlier uncommitted variants sit in the laptop's
   `Detent/.scratch/library-adoption-20261017/` worktrees (http-client-extraction, http-parser-client-extraction,
   compressors-http-bounds); their code files match blobs on GitHub, only README and doc edits do not.

## May land early

None.
