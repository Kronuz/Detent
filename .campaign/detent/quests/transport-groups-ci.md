# transport-groups-ci

## Outcome

consensus main is green again after transport-groups (consensus:e4e394b). Main's CI run 44119963752 and
rpc-lane-codec's merged run 44120112658 fail on code transport-groups landed:
1. linux-tsan: a data race inside libcrypto between two loop threads, `ASN1_STRING_set` (under the
   `SSL_CTX` lock created in `reactor::tls::configure`) against `ASN1_STRING_cmp` (unlocked), each resumed
   from `Connection::want` (groups_driver, groups_threads).
2. linux-arm64-gcc: groups_test.cc:369 `threads`, `stats(k).posts <= stats(k).reads...` fails.
The quest-branch CI (run 44116972577) passed both, so each is timing-dependent.

## Acceptance criteria

- A1 The race's cause is named (which OpenSSL object two loops share and which call writes it) and removed in the product, not suppressed: a TSan job running groups_driver and groups_threads 50x each with no report
- A5 [manual] The race is shown without the fix where it reproduces: Linux TSan CI reported it at consensus:e4e394b (run 44119963752) and passes at this quest's head; macOS TSan does not reproduce it (jobs 261117-084234-d7c1d4, -084243-55886d, uncached-context mutant, no report)
- A2 The groups_threads assertion's cause is named: a product defect is fixed, or the test's bound is corrected with the reason the old bound was wrong; 200 runs under a parallel load pass: jobs
- A3 The suite passes, and CI is green on linux-tsan, linux-arm64-gcc and macos (macos-x86_64's peers flake belongs to peers-retry-flake): the gate checks and CI

## Notes

transport-identity runs in parallel and edits host_transport.h and groups_test.cc; this quest lands
first, and transport-identity merges main after.

## May land early

Nothing.
