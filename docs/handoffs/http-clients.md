# Ask: consolidate HTTP client transport into the existing HTTP library

Review a C++20 service with a blocking keep-alive client, a coroutine TLS streaming client and a small HTTPS status client. Existing isolated consolidation work may already cover these paths. Recover it before implementing another client. This brief is independent of any application or repository.

## Scope

Use one request representation and bounded incremental response parser beneath blocking and coroutine drivers. Reuse existing TLS configuration/session facilities. Keep route selection, authentication policy, application schemas and publication feedback in callers. Extend the existing HTTP library rather than creating a parallel client or parser library.

Preserve absolute deadlines, response-body streaming, connection reuse, admission charges and authentic IO completion. A logical cancellation or handler return does not settle an accepted transport original. An exchange that fails late must not return a poisoned connection to the reusable pool. Borrowed TLS/authentication state cannot outlive its owner or leak across leases.

## Acceptance and delivery

Test fixed-length and chunked responses, split headers, malformed input, EOF, conflicting framing, tiny parse/disposal budgets and bounded headers/bodies. Hold a response write open and prove a second handler cannot reuse that connection until original settlement. Cover abandonment, shutdown, repeated release, held request/response ownership and final resource conservation.

Qualify all three caller styles plus existing server users. Keep server-only and client-only dependencies optional; TLS must not become mandatory for plaintext-only users. Preserve wire semantics and application error classification. Measure allocations, admitted peak memory, CPU and connection counts on real production exchange paths, including large streamed responses.

Deliver a narrow shared API, library/platform/sanitizer tests, separate adoption commits and an inventory of deleted private parser/cache/deadline implementations. Pin public revisions and qualify the final dependency closure without local source overrides. Follow local repository rules and coordinate host load; work serially and request one component review when complete. Do not move application session state machines into the HTTP library.
