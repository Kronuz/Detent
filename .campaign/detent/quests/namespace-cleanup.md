# namespace-cleanup

## Outcome

The three non-blocking notes from namespace-followups' review close: the history table bound has a test
that kills its removal (N1), the relay's last user gone, `CompletionScheduler` and its port are deleted
(N3: greenfield, one mechanism), and every `*_mutant.sh` runner uses the fails-without convention (N7).

## Acceptance criteria

- A1 A test fails when the table bound check (history.cc, around lines 508-515) is removed, on the bound itself rather than on a later decode: the suite, and a mutant job with `--fails-without`
- A2 `CompletionScheduler`, `CompletionPort`, `CoordinatorOptions::completion_port` and test_completion_scheduler are gone, and `git grep -E "CompletionScheduler|CompletionPort|completion_port"` over src, include and tests finds nothing: the suite, and a grep job
- A3 Every `tests/**/*_mutant.sh` exits nonzero when its mutant is killed, and each converted runner still kills its mutant: one `--fails-without` job per converted runner
- A4 The suite and pins pass: the gate checks

## Notes

Ordered after namespace-s6-hardening, which writes test_namespace_history.cc.

## May land early

Each criterion's change, once its row is met.
