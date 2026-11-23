# http-tls-small-reply-cost review: correctness

Commit: http:ad6a52efeb904ba2052568b5c08063f510dd0a8d (code at http:0eb4c52f0751)
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The code is correct; the one blocking finding is wording in `docs/perf/idle.md` and `CHANGELOG.md`
that claims more than job 261116-200643-d30410 measured. No code change is asked.

## Evidence I relied on

- Suite 261116-200651-b6727f at 0eb4c52: 13/13 in Release, `tls_asio` included. The code commits
  after it are docs-only (ad6a52e).
- TSan 261116-200653-56d463 at 0eb4c52: it checks that tls_asio, readahead, coroutine and asio are
  instrumented, then passes 13/13 with `halt_on_error=1`. The only suppressions are libev's, which
  this code does not use.
- Mutant 261116-200704-3f19a7: it replaces the zero-byte send with `return finish(self);` and
  fails on exactly the two new checks, "a TLS write found done completes after its initiating
  call" and "and so does one started inside that completion". "both writes arrive, in order" and
  every other check still pass, so the test catches inline completion and nothing else breaks.
- Counts 261116-200646-529fc3 (`docs/perf/idle/small-reply/count.jsonl`): at c1 the head arm runs
  0 `io_context` executes, 2 socket handlers and 1 `async_send` per request with no
  `non_blocking_send` (the send is a no-op), the same as base's handler counts. prev runs 1, 1
  and 0. Syscalls are equal.
- Load 261116-200643-d30410 (exclusive, 5 rounds, four arms): I recomputed every published cell
  from the committed `load.jsonl` and all of them match.

## What I checked myself

Q1, the Asio rule. I read the Asio 1.36.0 sources the build pins (`CMakeLists.txt:52`):
`reactive_socket_service_base.hpp:315-318` and `impl/reactive_socket_service_base.ipp:236-256`.
- For an empty buffer on a stream socket, `async_send` sets `noop`. `do_start_op` then calls
  `on_immediate`, which is `reactor::call_post_immediate_completion`. That function goes to
  `scheduler::post_immediate_completion`, so the operation never runs inside the initiating call.
  The reactor's `io_context io_{1}` sets `one_thread_`, so a send started from the loop thread
  takes the private queue with no lock or wake.
- Error paths:
  - A result found at start, whether success, an SSL error or `any == false`, takes the same
    `Phase::start` branch at `http_asio_tls.h:200-203`.
  - An error after a wait or receive completes inside that completion, which is allowed.
  - On a closed socket `state_` is 0, so the send is not a no-op. `set_internal_non_blocking`
    fails and the operation is still posted through `on_immediate`, carrying `bad_descriptor`.
    Line 179 ignores the send's own arguments and delivers the stored result.
- Cancellation: the reactor op cancellation emplaced at `reactive_socket_service_base.hpp:305` looks
  for the op in the descriptor's write queue. The posted op is not there, so cancellation is a
  no-op and the stored result is delivered. The old `asio::defer` behaved the same way.
- Exactly once, in order: the composed operation is moved into one operation that completes once.
  The bytes leave in `SSL_write_ex` before the send is posted, so a following write cannot reorder
  them. The empty send never touches the descriptor queues, so it cannot interact with an
  in-flight `async_receive` or `async_wait`.
- A handler that binds its own immediate executor takes `Op::do_immediate` and completes through
  that executor. This is Asio's opt-in, and the same thing happens with `asio::ssl::stream`.

Q2, buffers.
- The net diff of `http_asio_tls.h` touches only E2 (where read-ahead is set) and E3. E1's write
  change is reverted in 12b79cc.
- `joined` belongs to the `call` lambda inside `Operation`. It lives until `complete`, across
  every move and every `WANT_WRITE` retry, and the `unique_ptr` move keeps `b`'s pointer valid.
  `SSL_write_ex` succeeds only after the whole record is sent (partial-write mode is off).
- `take()` copies the bytes out of `room` before `SSL_read_ex` writes plaintext into it. It runs
  only after `bio_read` returned retry, which requires `input_` to be empty, so it never drops
  unread input. `bio_read` frees `input_` when the input is drained.
- No pointer into OpenSSL's record buffers is held, so `SSL_MODE_RELEASE_BUFFERS` cannot leave
  a dangling one.
- One record per reply: the "records in a small reply: 1" check passes in the suite and the
  mutant log, and the counts show 1 `sendto` per request.

Q3, the numbers. Script `.scratch/review-http-tls-small-reply-cost-correctness/load_table.py`, run
on `docs/perf/idle/small-reply/load.jsonl`, prints the per-round values and the paired head/prev
ratios. I submitted no job: the committed data settles the question.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The docs claim a c1 CPU improvement that this job does not show. `CHANGELOG.md` gives "+1.17 us ... against +2.61 us for http:ad6771f in the same job", and `idle.md` says "Every cell of after is inside the A/A band; ad6771f's c1 CPU (1.094) is not". Together they read as E3 cutting c1 CPU by about 1.4 us. Paired in the same rounds, head minus prev is +0.28 us and head/prev is 1.009 (0.882-1.038). Per round, head minus prev is +1.48, -4.45, -0.01, +0.28 and +0.61 us, so head costs the same or more in 3 of 5 rounds. The gap between the two medians comes from rounds 1 and 4, where prev runs high. "prev outside the band, head inside" is a difference between two medians, not a measured difference between head and prev. | `load_table.py` on load.jsonl (job 261116-200643-d30410). c1 `head/prev` CPU: median 1.009, diff +0.28 us. | Lead with what was measured. E3 restores base's handler counts (job 529fc3). At c1 its CPU effect against ad6771f is not resolved: 1.009 (0.882-1.038). At c64 it is directional and inside A/A. Keep +1.17 us and 1.037x as head against base, without the +2.61 contrast as a gain. Rerunning `load_table.py` checks the new text. |
| F2 | yes | gap | The close-path attribution in idle.md treats two medians inside the A/A band as measured effects: "costs about 3.5 us of system time per connection (system 1.064x) and is outweighed by 15.6 us less user time". head/base system time is 1.064 (0.968-1.152) against an A/A of 0.769-1.159, and user time is 0.972 (0.889-1.004) against an A/A of 0.839-1.124. The 2x recvfrom count is measured. Neither CPU split is resolved. | `load_table.py`, row ('tls','/',8,True), system and user blocks. | State the recv count. Say that the per-connection system and user deltas are inside A/A, so the recvs' cost is not resolved. |
| N1 | no | nit | The code comment at `http_asio_tls.h:166`, idle.md ("the speculative send asio::ssl::stream makes") and CHANGELOG ("as asio::ssl::stream's speculative send does") misattribute the trick. `asio::ssl::stream` posts an immediate result through a zero-sized `async_read_some` (`asio/ssl/detail/io.hpp:225-238`), not a send. The mechanism is the same, since Asio treats empty reads and sends on a stream socket as no-ops. | `asio/ssl/detail/io.hpp:231-238` | Say "as asio::ssl::stream's zero-sized read does", or drop the comparison. |
| N2 | no | nit | idle.md credits read-ahead for "a record's header and body arrive in one recv" on a kept-alive connection. On that path the read is a speculative `async_receive` into the caller's buffer, which takes all available bytes whether or not read-ahead is set. The handshake's exact 2x recvfrom count (9.44 against 4.71, and 9.57 against 4.80) instead suggests read-ahead does not apply to the BIO's direct socket reads under OpenSSL 4.1. E2 at either placement leaves the count unchanged. | count.jsonl close rows; `http_asio_tls.h:259-262` | Optional probe: a count job with `SSL_set_read_ahead` removed. If c1 and close counts do not change, drop the read-ahead sentence and record the cause as "header and body read apart". |
| N3 | no | nit | E2 is behaviour-neutral (the counts do not change), and it is kept by the lead's ruling. Both placements set read-ahead before any I/O, so there is no correctness risk either way. | journal "E2"; jobs 200140 and 200646 | None. |

## Not run

- Linux (epoll) and every sanitizer other than TSan: these are CI's. CI at http:ad6a52e was still
  running when I reviewed.
- A profile of head. The 0.85 us cost attributed to the defer is shown removed only by counts,
  not by sampled CPU.
- No exclusive or new jobs: F1 and F2 rest on data that is already committed.
- The bench tooling (`sample_summary.py`, `load_ab.py`, `ab.sh`) was not reviewed beyond
  confirming its medians reproduce.
- An immediate-error completion on the read path has no test of its own. It shares the
  `Phase::start` branch that the write test covers.

## Round 2

Commit: http:2671470a488e6fe45148146e98cf3b264308ac82
Verdict: APPROVE
Date: 2026-11-17

The fix changes docs only: `git diff ad6a52e 2671470` touches `docs/perf/idle.md` and
`CHANGELOG.md` alone, so the code evidence from round 1 still holds.

- F1 is fixed. CHANGELOG and idle.md no longer give +2.61 us as a contrast. They state the paired
  c1 result against ad6771f, 1.009 (0.882-1.038) and +0.28 us, with head costing the same or more
  in 3 of 5 rounds, and call E3's c1 effect unresolved. They give the reason E3 stays: the Asio
  rule and the handler counts from job 529fc3. All of these figures match `load_table.py`.
- F2 is fixed. The close bullet keeps the measured 2x `recvfrom` count. It now says the system
  (1.064x) and user (0.972x) deltas are inside the A/A band, so neither is a measured cost or
  saving.
- No overstated figure is left. `git grep` for "2.61", "3.5 us", "15.6" and "outweigh" in
  `docs/` and `CHANGELOG.md` finds only raw data rows and older, unrelated docs.
- N1 is still open, non-blocking: the CHANGELOG "Changed" bullet, idle.md and
  `http_asio_tls.h:166` still credit the trick to asio::ssl::stream's "speculative send". The
  stream actually posts through a zero-sized read. N2 and N3 are unchanged.

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Fixed at 2671470. | `git diff ad6a52e 2671470`; `load_table.py` | None. |
| F2 | no | gap | Fixed at 2671470. | same | None. |
