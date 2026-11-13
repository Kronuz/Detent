# http-idle-cost: HTTP idle cost: cheaper idle connections, TLS first

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-idle-cost`.

## Outcome

An idle HTTP connection, plain and especially TLS, costs measurably less memory (and wakeups) on the server, so one process holds many more idle long-poll/session connections. Assignment: performance. Measure first: bytes per idle connection (RSS and allocator) for plain and TLS at 10K and 100K idle, broken down by owner (socket, OpenSSL SSL/BIO buffers, read/write buffers, parser, coroutine frames, timers); then cut the largest owners (e.g. release or shrink buffers while idle, SSL_MODE_RELEASE_BUFFERS, smaller initial reads, lazy parser state). Detent's authority idle tests (test_authority_distribution_idle_100000/500000) are the consumer.

## Acceptance criteria

- A1 A benchmark reports bytes per idle connection and its breakdown by owner, plain and TLS, at 10K and 100K idle, at named commits before and after: an exclusive job and a results doc
- A2 TLS bytes per idle connection drop by a stated, measured amount (before/after at named commits, exclusive), with no throughput or latency regression on the existing benches beyond their A/A band: exclusive jobs
- A3 Every change keeps behavior: the suite, TSan, and a test for each buffer that is released and reacquired across idle (a request after a long idle, a TLS renegotiation-free resume of reading): the suite and tsan
- A4 [artifact] `docs/`: "idle" (where the bytes go, what changed, the numbers)

## Contracts

No API or wire change intended; if one helps (e.g. a configurable idle buffer policy), it is additive and named here before it lands.

## Decisions

Requested by the owner (2026-11-13): try making plain and TLS HTTP cheaper so a server holds many more idle TLS connections. Numbers decide (measurement.md): connect results to how many idle connections fit per GiB.

## May land early

Benchmarks and measurement tooling may land early.
