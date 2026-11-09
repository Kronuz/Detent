# completion-task-driver: completion Asio driver on task::task

Risk: normal. History: this landed before the campaign, under the earlier campaign skill, at completion:576618d (2026-11-02); its ledger `docs/status/task-driver.md` in Kronuz/completion is the record. The quest's repository is `../completion`. Its machine state is in `state.json`: `campaign status completion-task-driver`.

## Outcome

`AsioCompletionDriver`'s `run`, `run_one` and `drive` return `task::task<void>` on an injected `task::asio_scheduler` (Detent W2 Q6: one shape everywhere), with journal and consensus cascades pinning it.

## Acceptance criteria

Carried from the ledger's Done list, each shown by the ledger as an artifact.

- A1 [artifact] `asio_completion.h` on task; owner check against the injected scheduler; the queue wait as one `await_asio`: `docs/status/task-driver.md`
- A2 [artifact] `COMPLETION_ASIO` fetches task and links `task::asio`; `completion::completion` unchanged: `docs/status/task-driver.md`
- A3 [artifact] `test/asio_extension_test.cc` ported with sections homes, yield, cancel: `docs/status/task-driver.md`
- A4 [artifact] 19 of 19 in ASan+UBSan and TSan on macOS arm64; CI run 41573992492 green on 8 jobs: `docs/status/task-driver.md`
- A5 [artifact] Mutants M1-M3 each fail their sections: `docs/status/task-driver.md`
- A6 [artifact] CHANGELOG, README, ARCHITECTURE, AGENTS updated: `docs/status/task-driver.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- The drive takes no token, so its uncancellability is structural (review CR1).
- Cascades: journal:836c98b (CI 41574972922 green) and consensus:c7bf941 pin this head.
- Risk normal: the completion driver Detent's owners run on (the auditor's mapping).

## May land early

None.
