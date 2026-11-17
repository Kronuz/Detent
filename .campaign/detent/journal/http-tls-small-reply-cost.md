# http-tls-small-reply-cost: journal

State: waiting on jobs 261116-195707-1d58eb 261116-195720-666362 261116-195723-32a71d 261116-195740-090f5c 261116-195804-80be60 261116-195807-5f942e 261116-195818-4f8352 261116-200117-b61cf4 261116-200140-8e96ee
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

E1 (8d177a1): a write whose SSL_write_ex finished without waiting completes in place (await_asio continues
in place); one started inside that completion is deferred (thread_local flag), so nesting is one level.
Jobs: build 261116-195707-1d58eb; load base/prev ad6771f/head/A/A, c1 c64 close, 261116-195720-666362
(A2); counts 261116-195723-32a71d; suite 261116-195740-090f5c (A4); TSan 261116-195804-80be60 (A4); mutant
(in-place off, tls_asio_test must fail) 261116-195807-5f942e (A2); 10K TLS idle 261116-195818-4f8352 (A3).

E2 (8368ea9): SSL_set_read_ahead moved after SSL_set_accept_state/connect_state. Jobs: build
261116-200117-b61cf4 (slot tls-small2), counts 261116-200140-8e96ee. If recvfrom drops to base's 4.6, an
exclusive load at E2 (base, e1, head, A/A; c1 keep, c8 close) and suite/TSan/10K rerun at E2; else revert.

## Next

1. Read E1 load/counts/mutant/10K/suite/TSan and E2 counts.
2. E2 kept: load + gates at its commit. E2 not: revert it (the suite/TSan/10K at 8d177a1 stand).
3. docs/perf/idle.md + CHANGELOG: the attribution and the new numbers; Evidence table; ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
