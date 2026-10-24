# Ask: consolidate HTTP client transport into the existing HTTP library

**Priority:** P2 · Rank 5 of 7 · Required consolidation after resource adoption.

Required before release, but correctness and storage blockers take precedence. Existing isolated client work must be recovered and reconciled.

## Repositories and starting point

1. **Start repository:** [Kronuz/http](https://github.com/Kronuz/http).
2. **TLS/session facilities:** [Kronuz/reactor](https://github.com/Kronuz/reactor).
3. **Consumer integration:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
4. **Existing blueprint:** [Kronuz/Detent/blob/main/docs/HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md).
5. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** Consumer `src/detent_http/http_client.cc`, `src/detent_delivery/client.cc`, `response.cc` and `include/detent_transport/https_client.hh`; shared HTTP response parsing and client targets. Existing evidence: `benchmarks/http-client-preliminary-20261019.json`.

**Existing local candidate:** `.scratch/library-adoption-20261017/detent-http-client-extraction` relative to the consumer checkout, if available. Inspect both tracked and uncommitted work. A fresh clone will not contain this scratch worktree; use the linked committed blueprint and evidence to recover the migration, and do not assume it is already on main.

**Dependencies and ownership:** Audit may start now; integrate after the resource task settles the shared ownership API. Coordinate TLS changes with reactor and preserve existing server users.

**Release-unblocking outcome:** All three clients use the shared transport/parser, migrated private engines are deleted, held-original settlement tests pass, and clean public-pin consumer/platform qualification plus complete-path cost comparisons are recorded.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 service with a blocking keep-alive client, a coroutine TLS streaming client and a small HTTPS status client. Existing isolated consolidation work may already cover these paths. Recover it before implementing another client.

## Scope

Use one request representation and bounded incremental response parser beneath blocking and coroutine drivers. Reuse existing TLS configuration/session facilities. Keep route selection, authentication policy, application schemas and publication feedback in callers. Extend the existing HTTP library rather than creating a parallel client or parser library.

Preserve absolute deadlines, response-body streaming, connection reuse, admission charges and authentic IO completion. A logical cancellation or handler return does not settle an accepted transport original. An exchange that fails late must not return a poisoned connection to the reusable pool. Borrowed TLS/authentication state cannot outlive its owner or leak across leases.

## Acceptance and delivery

Test fixed-length and chunked responses, split headers, malformed input, EOF, conflicting framing, tiny parse/disposal budgets and bounded headers/bodies. Hold a response write open and prove a second handler cannot reuse that connection until original settlement. Cover abandonment, shutdown, repeated release, held request/response ownership and final resource conservation.

Qualify all three caller styles plus existing server users. Keep server-only and client-only dependencies optional; TLS must not become mandatory for plaintext-only users. Preserve wire semantics and application error classification. Measure allocations, admitted peak memory, CPU and connection counts on real production exchange paths, including large streamed responses.

Deliver a narrow shared API, library/platform/sanitizer tests, separate adoption commits and an inventory of deleted private parser/cache/deadline implementations. Pin public revisions and qualify the final dependency closure without local source overrides. Follow local repository rules and coordinate host load; work serially and request one component review when complete. Do not move application session state machines into the HTTP library.
