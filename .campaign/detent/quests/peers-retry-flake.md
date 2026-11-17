# peers-retry-flake

## Outcome

consensus `test/transport/peers_test.cc:1196` (the retried 8 MiB fetch after a frozen peer) stops failing on
loaded macOS x86_64 CI (run 44074119964). Cause, inferred by ci-test-flakes A7: the 500 ms `fetch_deadline`
set for the frozen-peer check also bounds the whole retried reply, which a loaded runner cannot always deliver.

## Criteria

- A1 The cause is forced: a reader delay over 500 ms on the retried fetch fails the unchanged test the same way, and the fixed test passes with it: jobs, `--fails-without`
- A2 The fixed test still checks the frozen fetch's timeout (timed_out, no bytes, waited >= the deadline) and that the retry reuses the same lanes, and passes 200 runs under a parallel load: a job
- A3 The suite passes: the suite check

## Notes

Peers and DTFT0001 go away in worker-pump-removal (D106); this keeps consensus CI green until then.
Test-only: the product's deadline semantics do not change.

## May land early

Nothing; it is one change.
