# dense-distribution-build--history-ranges: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs for D138 at the branch head (see Evidence).

## Next

1. Read the jobs; the 5M row shows whether the relay now mirrors 5M (it failed 413 at 560 s).

## Blocked on

The relay's own `/v2/history/file` (src/detent_relay/service.cc `parse_history`, and the File job in
mirror_service.cc) refuses `offset`/`length` (exactly two fields), so a relay reading another relay
fails on a file above `range_bytes` (4 MiB): today's chain fails only above the voter's 16 MiB bound
at the first hop, so chains with 4 to 16 MiB files regress. Both files are outside this task's set,
and mirror_service.cc is the fix contributor's. Options:
1. Add those two files to this task (about 15 lines: parse the optional pair, answer a slice of the
   mirror's bytes, 416 past the end), after the fixer's mirror_service.cc lands. Recommended.
2. The fixer adds it.
3. HttpUpstream asks ranges only of voters. Rejected: two request shapes by upstream kind.

## Evidence

| Criterion | Evidence |
|---|---|
| D138.1 ranges, verified, resumed | test_http_upstream (exact, resume), test_authority_history_ranges: jobs below |
| D138.2 bounded backoff, reported | test_http_upstream (backoff): jobs below |

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

## Rulings I made

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
