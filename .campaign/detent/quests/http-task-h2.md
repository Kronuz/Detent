# http-task-h2: http streamed path on task (H2)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at http:93dc7bb (2026-11-03); its ledger `docs/status/task-h2.md` in Kronuz/http is the record. The quest's repository is `../http`. Its machine state is in `state.json`: `campaign status http-task-h2`.

## Outcome

`BodyStream::read` and `push` are tasks, the streamed feed and handler join under `when_all`, a failed body cancels the request before failing the stream, and H1 and H2 are proved and documented.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] D1 `BodyStream` on task (`read`, `push`); `async_read` gone: `docs/status/task-h2.md`
- A2 [artifact] D6 the streamed join under `task::when_all`; the lazy watch after the body: `docs/status/task-h2.md`
- A3 [artifact] D9 for `read`: `BodyStream::affine()`; mutant D9-read killed: `docs/status/task-h2.md`
- A4 [artifact] K1 one shape; K2 13 tests on macOS and Linux sanitizer builds: `docs/status/task-h2.md`
- A5 [artifact] K3 all 13 L3 probes on task handlers, ASan+UBSan and TSan, Linux and macOS: `docs/status/task-h2.md`
- A6 [artifact] K4 T4's streamed cases; K5 sanitizers 0 reports: `docs/status/task-h2.md`
- A7 [artifact] K7 streamed POST kind allocations at most A: `docs/status/task-h2.md`
- A8 [artifact] K9 docs and the CHANGELOG upgrade note: `docs/status/task-h2.md`
- A9 [artifact] K10 CI green on every job (run 41603112360); the timeouts flake explained and fixed: `docs/status/task-h2.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Lead ruling (2026-11-03): T3's `probe_streamcancel` expects `read_err=1` (the design's D1), not 0.
- Risk high: body streaming and disconnect handling (the auditor's mapping).

## May land early

None.
