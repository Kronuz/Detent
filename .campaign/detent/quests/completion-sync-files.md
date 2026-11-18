# completion-sync-files: SyncFiles and OrderFiles primitives (namespace S1)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at completion:ab0f551 (2026-11-03); its ledger `docs/status/sync-files.md` in Kronuz/completion is the record. The quest's repository is `../completion`. Its machine state is in `state.json`: `campaign status completion-sync-files`.

## Outcome

completion's `SyncFiles` and `OrderFiles` operations are async front ends over `IO::sync_files_and_directory` and `IO::order_files_and_directory` for one directory, with read-only opens and per-platform recorded sequences (Detent namespace-design S1, D11, ND-R5, ND-R6).

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] 1 and 1a: `SyncFiles`, `OrderFiles`, `MutationRequest::files` and `read_only` opens: `docs/status/sync-files.md`
- A2 [artifact] 2: per-platform expected sequences committed in `test/sync_primitives_test.cc`; CI run 41738797330 green: `docs/status/sync-files.md`
- A3 [artifact] 3: `docs/mechanisms/sync-files.md` with pointers from ARCHITECTURE, PLATFORM_FINDINGS, commit-barriers: `docs/status/sync-files.md`
- A4 [artifact] 4 (completion side): mutants s1-s6; ASan+UBSan and TSan 20 of 20; CI sanitizer jobs green: `docs/status/sync-files.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Lead ruling L1: one directory; files outside the IO's directory are unsupported.
- Shape constraint from the Xapiand lead: front ends over the existing IO calls, so strata's counted IO and faultfs see them unchanged.
- Linux and FreeBSD fsync per file then the directory (N+1); unmeasured, T2 decides.
- Risk high: durability ordering of grouped file syncs (the auditor's mapping).

## May land early

None.
