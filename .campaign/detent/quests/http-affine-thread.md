# http-affine-thread: Affinity checked against the loop thread (D9)

Risk: low. History: this landed before the campaign, under the earlier campaign skill, at http:73cfa02 (2026-11-03); its ledger `docs/status/affine-thread.md` in Kronuz/http is the record. The quest's repository is `../http`. Its machine state is in `state.json`: `campaign status http-affine-thread`.

## Outcome

`AsioResponseWriter::affine()` and `BodyStream::affine()` assert the loop thread, not the scheduler's identity, so a handler under a wrapper scheduler that resumes on the loop thread passes.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] The asserts check `reactor_->on_loop()` and `scheduler_.runs_here()`: `docs/status/affine-thread.md`
- A2 [artifact] `coroutine_test` [T] (h) `/affine-wrapped` passes; the old asserts abort: `docs/status/affine-thread.md`
- A3 [artifact] Death tests `/affine-body`, `/affine-read`, `/affine-thread` still die: `docs/status/affine-thread.md`
- A4 [artifact] Mutant: empty `affine()` fails the three death tests: `docs/status/affine-thread.md`
- A5 [artifact] Debug ASan+UBSan and TSan ctest 13/13: `docs/status/affine-thread.md`
- A6 [artifact] Docs: AGENTS.md, CHANGELOG.md, `docs/mechanisms/coroutine-handlers.md`: `docs/status/affine-thread.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- The lead's D9 ruling: the rule is the loop thread.
- Risk low: a Debug-only assertion (the auditor's mapping).

## May land early

None.
