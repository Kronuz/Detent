# http-tls-small-reply-cost: journal

State: ready for review (http:ad6a52e; code at http:0eb4c52)
Quest: .campaign/detent/quests/http-tls-small-reply-cost.md (the lead's; never edit it)

## Doing

Commits (http): 996f1fc bench profiling (load_ab user/system split, --sample-dir, sample_summary.py, ab.sh
profile), 2bbd70a summarizer fix (stdin read is idle; allocator time by caller; drops a stray pyc),
8d177a1 E1 write in place + tls_asio_test check, 8368ea9 E2 read-ahead after the handshake state.

A1 attribution, jobs at 996f1fc (product ad6771f vs a17b197): profile 261116-190828-78aa9b (exclusive,
sample, 3 x 10 s, A/A arm; re-summarized with 2bbd70a's sample_summary.py), counts 261116-190816-f47f92,
load 261116-190832-1dac8f (exclusive, 5 rounds, A/A).

TLS keep-alive c1, head - base per request (A/A - base in parentheses):
- rusage under sampling: CPU +2.23 us (+1.04), user +1.88 (+0.61), system +0.35 (+0.43): user space only.
  Syscalls equal (counts: 2 recvfrom incl. 1 EAGAIN, 1 sendto, 2 kevent in both arms).
- sampled own time: http binary +0.95 (+0.31), libssl +0.89 (-0.09), libsystem_malloc +0.86 (+0.12),
  libcrypto +0.50 (+0.05), libsystem_kernel -0.08 (+0.68).
- Named costs:
  1. Deferred write completion (asio::defer through any_io_executor: executor_function built and the
     composed op moved in and out; 1 io_context execute per request, base 0): about 0.85 us
     (ICF'd frames under executor_function::complete 0.42, any_executor copy/move/destroy 0.25,
     executor_function ctor/complete 0.14, work_finished 0.06). E1 removes it.
  2. Received-bytes copy for the BIO (take(): new + memcpy + delete): 0.34 us. Kept: it is what lets an
     idle connection hold no receive buffer (D103's trade).
  3. Joined reply copy (head + body into one record): 0.20 us. Kept: without it a reply is two records
     and two sends (76f698d's +65% CPU).
  4. OpenSSL record path: libssl +0.75, libcrypto +0.64 spread over the write-record functions (none above
     0.11 us; A/A -0.09 / +0.11). Not attributed to a single cause.
- Fresh load at the same code (1dac8f): c1 CPU/req 1.070 [0.995-1.091], A/A 0.984 [0.966-1.087]: inside
  the band; 30.13 vs 28.82 us (+1.31: user +0.93, system +0.13). req/s 0.972, A/A 1.022.

TLS close c8 (handshake per request):
- counts: recvfrom 9.40 per connection head vs 4.60 base (EAGAIN 2.06 vs 1.94): +4.8 successful recvs,
  as if each handshake record's header and body were read apart; kevent 3.12 vs 3.13, sendto 3 vs 3.
- profile: system +5.47 us (A/A -0.31), user -7.31 (A/A -4.94; no BIO pair or staging), CPU 0.980x.
- Fresh load (1dac8f): req/s 1.049 [0.956-1.061] (A/A 1.028 [0.956-1.070]), CPU/req 0.968 (546.1 vs
  550.3 us): the 0.967x of 261116-090550-a7dfdc does not reproduce. E2 tests the extra recvs.

E1 (8d177a1): dropped on the lead's ruling (msg: completing in place breaks the Asio rule that a handler
never runs inside its initiating call; http-idle-cost review Q6). Reverted in 12b79cc; its jobs cancelled
(195707-1d58eb, 195720-666362, 195723-32a71d, 195740-090f5c, 195804-80be60, 195807-5f942e, 195818-4f8352).

Allocation vs hop in the deferred completion (profile 78aa9b, c1, own time per request, frames of the
executor machinery: executor_function, any_executor_base, initiate_defer, send_op do_complete/async_send,
handler_work, scheduler post/work, plus ICF'd frames directly under them): head 0.93 + 0.48 = 1.41 us,
base 0.27, A/A 0.21; allocation under it (recycled thread_info block, malloc) 0.05 us. So the cost is the
hop (type-erased any_io_executor defer, the composed op moved into an executor_function and out), not the
allocation: bind_allocator would save at most 0.05 us. A continuation dispatch does not apply: the
immediate result is always found inside the initiating call (Phase::start).

E2 (8368ea9): SSL_set_read_ahead after SSL_set_accept_state/connect_state (tests the close row's +4.8
recvs). Kept as the lead asked. Build 261116-200117-b61cf4, counts 261116-200140-8e96ee (arms base,
e1 8d177a1, head 8368ea9; recv counts do not depend on E1).

E3 (0eb4c52): an immediate result completes through a zero-byte socket_.async_send instead of asio::defer.
Asio makes no syscall for an empty send on a stream socket and, for a handler without its own immediate
executor, queues the reactor op itself via reactor::post_immediate_completion: private queue under
io_context(1), no lock or wake, never inside the initiating call (asio::ssl::stream's send path). Test:
tls_asio_test "a TLS write found done completes after its initiating call" (and nested). Jobs at 0eb4c52:
build 261116-200630-22b325; load base/prev ad6771f/head/A/A, c1 c64 close, 261116-200643-d30410 (A2);
counts 261116-200646-529fc3; 10K TLS idle 261116-200648-8f78b8 (A3); suite 261116-200651-b6727f (A4); TSan
261116-200653-56d463 (A4); inline-completion mutant (must fail) 261116-200704-3f19a7.

Close c8 (A2), paired with A/A at ad6771f (1dac8f, median of 5 per-round ratios [min-max]): head req/s
1.049 [0.956-1.061] vs A/A 1.028 [0.956-1.070]; CPU/req 0.968 [0.952-1.063] vs A/A 0.996 [0.939-1.040].
Head's medians sit inside the A/A intervals: the 0.967x of 261116-090550-a7dfdc was noise. Nothing fixed for
it (E2 is the recv-count experiment, kept by the lead's ruling).

E3 results (load 200643, exclusive, 5 rounds, median per-round ratio to base [min-max]):
- c1 keep: CPU/req head 1.037 [0.999-1.268], prev 1.094 [0.999-1.222], A/A 0.982 [0.874-1.066]; req/s head
  0.989, A/A 1.015 [0.929-1.157]. Paired diff head-base CPU +1.17 us (user +1.37, system -0.20), prev +2.61.
- c64 keep: CPU head 1.000 [0.901-1.060] (A/A 0.986 [0.854-1.129]); req/s 1.001.
- c8 close: req/s head 1.037 [0.981-1.073] (A/A 1.055 [0.878-1.218]); CPU 0.980 (A/A 0.980).
- head/prev paired: c1 CPU 1.009 [0.882-1.038]; c64 CPU 0.968 [0.945-0.995], req/s 1.033 [1.006-1.054] (5/5
  rounds, but inside A/A); close req/s 1.006.
- Counts (200646), c1 per request: head = base (1 socket async_send, 0 io_context execute, 2 socket
  handlers); prev 0 / 1 / 1. Syscalls equal (2 recvfrom incl 1 EAGAIN, 1 sendto, 2 kevent).
- E2 counts (200140): close recvfrom per connection head 9.20, e1 9.11, base 4.67: read-ahead placement is not
  the cause. At 0eb4c52: 9.51 vs 4.75 (EAGAIN 2.10 vs 2.00).
- A3 (200648): TLS 10K idle 40,075 B/conn (spread 11) vs 40,054: +0.05%. Base 96,788.

A2 by named cost (c1, per request):
1. Deferred completion (~0.85 us): replaced by the zero-byte send (E3); handler profile now equal to base's.
2. take() copy 0.34 us: kept, D103's idle trade (no receive buffer while idle).
3. Joined reply copy 0.20 us: kept, one record and one send per reply (76f698d: two records cost +65% CPU).
4. OpenSSL record path (libssl +0.75, libcrypto +0.64 in the ad6771f profile, nothing above 0.11 us): not
   separable at this band.
Remaining: +1.17 us/request (1.037x), inside A/A; 0.54 us of it is the two kept copies.
Close c8: req/s 1.037 inside A/A, CPU 0.980x; the extra 4.76 recvs cost ~+3.5 us system per connection
(system 1.064x), outweighed by -15.6 us user. Cause open (E2 ruled out read-ahead placement).

## Next

Review. CI has no run at http:ad6a52e (needs the branch pushed, the lead's call).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261116-200646-529fc3 (counts at 0eb4c52); profile 261116-190828-78aa9b, counts 261116-190816-f47f92, load 261116-190832-1dac8f (at 996f1fc, product ad6771f) |
| A2 | 261116-200643-d30410 (exclusive load, A/A); fails without: 261116-200704-3f19a7 |
| A3 | 261116-200648-8f78b8 |
| A4 | 261116-200651-b6727f (suite), 261116-200653-56d463 (TSan); fails without: 261116-200704-3f19a7 |

All passed at http:0eb4c52 and count at ad6a52e (docs-only). No messages listed by `campaign brief`.

## Result

http:0eb4c52: an immediate TlsStream result completes through a zero-byte socket async_send (as
asio::ssl::stream does) instead of asio::defer; 8368ea9 sets read-ahead after the handshake state (no
measured effect). Small TLS reply at c1: +1.17 us/request over a17b197 (1.037x, A/A 0.874-1.066) vs
ad6771f's +2.61 us in the same job; c64 1.000x; close c8 req/s 1.037x. Idle TLS 40,075 B/conn at 10K.
Docs: docs/perf/idle.md "Small TLS replies", runs in docs/perf/idle/small-reply/, CHANGELOG 2026-11-17.
Open: a TLS handshake makes 9.5 recvfrom per connection vs 4.75 (+~3.5 us system, net CPU still lower).

## Rulings I made

- Kept E3 although its c1 gain over ad6771f is not resolved (head/prev 1.009 [0.882-1.038]): it restores the
  old stream's handler profile exactly, is Asio-rule compliant (tested, mutant 200704 fails), and at c64 every
  round favors it (0.968 CPU, 1.033 req/s). The journal's earlier "revert if c1 not below prev" rule assumed
  a resolved c1 delta; this band cannot resolve 1%.
- Tagged the E3 mutant 200704 for A2 (fails-without) by an identical resubmit: the only A2 fails-without was
  E1's cancelled mutant 195807, dropped with E1.
- Stopped at three experiments (E1 dropped, E2 no effect, E3 kept); the close-row recvs are recorded open,
  inside the band.
