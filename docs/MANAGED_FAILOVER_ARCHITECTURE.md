# Validated Architectural Blueprint: managed authority failover

**Date:** 2026-10-14
**Status:** Lead Software Architect source GO; component qualified on native, ASan/UBSan, TSan, Linux and FreeBSD. [Qualification record](../benchmarks/managed-failover-qualification.json).

Legacy single-endpoint managed sources retry one configured endpoint. In the pinned consensus baseline (`b7073bb`), followers reject reads, unclassified read rejection becomes generic HTTP 503, and parked subscription Next requests can return idle without a new quorum read. A reachable isolated former leader can therefore remain idle indefinitely. A configured endpoint pool needs periodic quorum revalidation as well as error-driven rotation.

## Component boundaries

1. Append a pool of at most eight endpoints to `CoordinatorOptions`; leave `ClientOptions.endpoint` as the single destination for each transport attempt. An empty pool preserves existing behavior. Configuration accepts either legacy `address`/`port` or an explicit endpoint list. Reject duplicates and invalid destinations. Every entry shares the configured TLS name, CA, credential, authority identity and scope.
2. Keep routing in one private `source_selection.hh` component. It owns bounded endpoint state and refresh timing, with no transport, storage or session ownership. Try endpoints serially, once per round, before existing seeded reconnect backoff. Remain sticky after success. Add no per-endpoint workers.
3. Distinguish authority `source_not_leader` read rejection from admission pressure and write outcome uncertainty. Reuse bounded control decoding for subscription and polling. Rotate on classified transport/deadline failures, NotLeader, temporary unavailability and SourceBehind. Local construction, storage and admission failures do not change routes. Parser exceptions do not establish source unavailability.
4. Bind each session to its opening endpoint and trusted TLS name. Validate Next and Close against that route. Before rotation, drain the outstanding original and attempt bounded Close on its opening endpoint. Discard the old session before Open on another endpoint. Open uses the actual recovered durable selection, window zero and the retained forced-snapshot latch. Session IDs and feedback never cross endpoints.
5. Give pooled subscriptions a finite refresh interval, default 30 seconds with seeded bounded jitter. Idle responses do not extend it. Cap parked Next waits by the remaining interval. Finish accepted publication and recovery before closing, then try a fresh quorum-backed Open on the current endpoint before alternatives. Polling already performs quorum reads and only needs error-driven rotation.
6. Fail closed on explicit authentication/authorization denial, TLS verification failure and unexpected identity/profile. Never follow redirects or send credentials outside the configured pool. Routing grants no reset permission and permits no revision regression. Multiple relay endpoints need a separate freshness contract because relay Open does not itself prove a fresh authority quorum.
7. Report active endpoint, rotations, refresh failures and the last successful quorum Open without credentials. Document discovery time in terms of refresh, member deadlines, retry backoff and accepted local work. Existing 60-second request deadlines prevent a short failover guarantee. A nominal 30-second refresh across 10,000 agents adds roughly 333 Opens per second before jitter and requires measured admission/quorum qualification.

## Acceptance gates

1. A real three-voter authority feeds a managed relay/agent and independent reader, starting at a follower, surviving leader death and converging after majority writes.
2. A separate partition leaves the former leader's client HTTPS reachable while cutting peer links. Idle responses cannot prevent discovery of the functioning majority.
3. All-members-unavailable and no-quorum cases preserve last-known-good reads and durable selection, keep attempts/backoff bounded and shut down through authentic drainage.
4. Held and failed Open, Next and Close originals cover rotation and cancellation. Cleanup uses the opening endpoint, and feedback never crosses routes.
5. Authentication, TLS and identity negatives fail closed. SourceBehind cannot regress state. History loss negotiates a snapshot. Both delivery modes and legacy single-endpoint behavior remain covered.
6. Native, sanitizer and reduced Linux/FreeBSD process checks use the pinned consensus baseline. Measure periodic refresh load separately.

This is one complete component under [Stage 6](STAGE6_ARCHITECTURE.md), with a consolidated review after implementation. It changes no consensus dependency or membership contract.
