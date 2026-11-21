# dense-distribution-build--history-ranges: journal

State: waiting on jobs 261120-161154-462bce 261120-163145-987bc9 261120-163148-de1079 261120-163151-62ee61 261120-163158-71ef88 261120-163200-f3d566 261120-163203-380025
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

The lead's round at 53686a6c: prefix_dropped survived, the 5M row's agent exited, relay-to-relay
ranges.

## Next

1. Read the jobs at 27d27d42; the 5M diagnosis job 261120-161154-462bce (53686a6c plus the relay's
   swallowed answer and build errors printed, .scratch/history-ranges/diag/diag.sh) names the
   exception behind the agent's 503 `distribution_unavailable`.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| D138.1 ranges, verified, resumed | test_http_upstream (exact, resume) 261120-140658-c98aab, ASan+UBSan 261120-140701-e20960; test_authority_history_ranges 261120-140659-cc116c, ASan+UBSan 261120-140702-b2e24c |
| D138.2 bounded backoff, reported | test_http_upstream (backoff), same jobs |
| Mutants killed (--fails-without) | prefix_dropped 261120-140716-764a97, whole_files 261120-140717-ac69d9, no_backoff 261120-140718-121ba1, unbounded 261120-140720-77337a |
| Lost answer keeps the prefix (prefix_dropped) | test_http_upstream 261120-163145-987bc9; ASan+UBSan 261120-163148-de1079; mutant 261120-163158-71ef88 |
| Relay chain in ranges | test_http_upstream `chain` (same jobs); mutants relay-range-whole 261120-163200-f3d566, relay-range-refused 261120-163203-380025 |
| No regression at 27d27d42 | 261120-163151-62ee61 |
| No regression | ctest -R 'relay\|history\|test_authority_service\|http_upstream' 261120-140704-29b54e |
| 5M relay mirror (the repro) | dense-distribution-row 5000000-flat-main 261120-140706-39dc89 (strict, 10 min) |

Void: 261120-140615-f81b0b .. 140633-b220ff (a shell quoting slip ran no test).

## Result

`/v2/history/file` takes an optional `offset` and `length` (both or neither, length at least 1). A
range answers at most the reply bound (`reply_bytes` less 256) from its offset, fewer bytes at the
bound or the file's end, 416 at or past the end; the whole-file form is unchanged (413 above the
bound). The voter holds the file a relay reads in ranges (`HeldFile`, by name and table seal), read
and digest-checked once, until its last range: n ranges cost one read and hash, not n.

`HttpUpstream::fetch` reads a file above `range_bytes` (default 4 MiB, two replies fit the default
16 MiB budget) in ranges, the shape of D8's transfer resume: the request names the offset, the
reader keeps the verified prefix (each answer non-empty and at most the length asked) with its
running SHA-256, a lost answer (no answer, or 503) is asked of the next source, every source once,
and a fetch that still fails keeps the prefix for the next fetch of the same file. The whole file
is checked against the layout's size and digest; a mismatch drops the prefix. Reads at most
`range_bytes`, never more than the voter's bound, so no bound sets the cliff.

The ~9 retries a second in the 5M repro are the system harness's `reconnect_maximum_ms: 100`
(tests/detent_system/managed.py:111, 203), not a missing backoff: the mirror service already
doubles its wait from `interval` to `reconnect_maximum` (default 30 s) and reports `sync_failures`
and `error` in health. test_http_upstream's backoff case bounds it: 29 syncs in 1.5 s at a 50 ms
bound, against 300 at the 5 ms interval with no backoff.

Docs: docs/status/namespace-s8.md, "The service" (the route row and the paragraph after it).

## 5M row (261120-140706-39dc89)

The relay now mirrors 5M (ready in 14.8 s, 4 files, 203,889,102 bytes, where it failed 413 at
560 s). The cold pages agent then got `503 distribution_unavailable` 1,258 times in 3 min (its
`distribution session reset: unavailable`) and exited at its preparation deadline. That 503 is
mirror_service.cc's catch-all around `answer()` (`NamespaceRelay::changes`), which drops the
exception's message; the relay never logs it. Not this task's code path (answers, builds), first
reached at 5M because the mirror never got that far before. Cause pending the diagnosis job.

## Rulings I made

- A relay's range answers at most the length asked from the offset (no reply bound of its own, as
  its whole-file answer has none; the answer account bounds both), and holds the file read once
  until its last range, as the voter does: n ranges cost one read, not n.
- `MirrorJob::range` in include/detent_relay/mirror_service.hh: the job's half of the route.
- The chain test lives in test_http_upstream.cc (relay A in-process behind ServiceRuntime and the
  HTTPS listener, torn down in the host's phases), so it links detent::relay_http and
  detent::authority (history files A's answers decode).

- A range answers fewer bytes than asked at the voter's bound (read(2) semantics) rather than 413:
  the relay need not know the bound, so a voter configured below 4 MiB still serves it.
- The relay sends the whole-file form (two fields) for a file within one range, so the relay route
  and small files keep their requests; one loop serves both.
- No per-range digest on the wire: the voter slices bytes it digest-checked whole, TLS covers
  transit, and the relay checks the whole digest, as D8 pins a transfer by its name.
- Touched outside the listed globs, each the listed sources' own half: include/detent_relay/
  http_upstream.hh (the option and stats), tests/detent_authority/CMakeLists.txt and
  tests/detent_relay/CMakeLists.txt (registering the two tests), docs/status/namespace-s8.md.
- The backoff mutant patches mirror_service.cc only when its job runs; the patch is a test file.
