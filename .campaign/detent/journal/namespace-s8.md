# namespace-s8: journal

State: waiting on jobs 261112-120518-8667ef 261112-120518-a03f29 261112-120534-7e0236 261112-120537-fb5cc7 261112-120540-e6efea 261112-120543-c18350 261112-120546-8f0fcd 261112-120549-f1e18a 261112-120552-fbda3d 261112-120555-5fb518 261112-120558-1bde4c
Quest: .campaign/detent/quests/namespace-s8.md (the lead's; never edit it)

## Doing

Mirror store built and tested locally at Detent:5afd10fb (`test_relay_mirror` ok; T7 mirror 14,860
cases, 475 distinct cuts, 475 re-adopted, 0 violations, 12 s). Jobs queued:

| Job | For | What |
|---|---|---|
| 261112-120518-8667ef | A2, A4 | native suite (`test_relay_mirror` restart, follows) |
| 261112-120518-a03f29 | A2, A4 | pins |
| 261112-120534-7e0236 | A3 | T7 crash matrix |
| 261112-120537-fb5cc7 | A3 | mutant rename-before-fsync (fails-without) |
| 261112-120540-e6efea | A3 | mutant no-sync-group |
| 261112-120543-c18350 | A3 | mutant collect-before-table |
| 261112-120546-8f0fcd | A3 | mutant keep-orphans (startup sweep) |
| 261112-120549-f1e18a | A2 | mutant fetch-reached (reuse) |
| 261112-120552-fbda3d | A2 | mutant trust-first-read (repair) |
| 261112-120555-5fb518 | A4 | mutant no-collector |
| 261112-120558-1bde4c | A4 | mutant collect-missing-unlink |

## Next

1. Read the jobs; paste `campaign brief`'s table.
2. Durability review (the lead launches it; the quest's lens).
3. After S6 lands: A5 (W8) against its W5/W6 types; the mirror implements S6's history interface
   and `plan_answer` goes.
4. After S5.2 lands: A1's voter half and the service wiring (upstream layout and file-by-name).

## Blocked on

1. **A1 needs S5.2 (namespace-blocks).** The quest consumes `namespace-blocks`, which
   namespace-s5-2 provides; it is `planned`, behind namespace-measure. No voter writes a seal
   file, block, base or `layout` on `main`, so "byte-identical to the voters'" has nothing to
   compare against, and the layout's wire (fetch the upstream's layout, a file by name) is not
   served. The brief only names namespace-s6. Options: (a) S8 builds the mirror against an
   abstract upstream layout now (names, digests, sizes, ranges, cut, opaque policy), proves A2-A4
   and the relay-chain half of A1 over a scripted upstream, and the voter half of A1 plus the
   wiring into `service.cc` land after S5.2 as a follow-up task; (b) hold S8 until S5.2 lands.
   Recommendation (a), which I am doing: the mirror's durability (A3) does not depend on the
   block format, only on names and digests. Lead decides whether S8 needs S5.2 before landing.
2. A5 waits for namespace-s6 (D84).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

Pending the jobs. Docs: `docs/status/namespace-s8.md` (how the mirror adopts, starts, reads,
repairs and plans answers; what each test shows).

## Lead rulings received

- D84 (Detent:86704b50, merged at quest/detent/namespace-s8 7f2c16ce): A5 added (W8); S8 lands
  after namespace-s6. Build A1-A4 now, A5 against S6's quest file.

## Rulings I made

1. The mirror is content-agnostic: it never parses a block, base or section; the answer plan names
   files and the revision filter, and S5.2's codec trims and merges. So nothing here fixes
   S5.2's formats.
2. `tests/CMakeLists.txt` gains one line, `add_subdirectory(detent_relay)`: the file set names
   `tests/detent_relay/**`, which needs it; nothing else outside the file set changed.
3. The mirror lives in a directory of its own: startup removes every name its table does not
   reach, so it cannot share a directory with the agent-layout store (D7's prepared generation).
4. Local names are `m-` + 32 hex of SHA-256(upstream name): one flat shape for any namespace name.
5. Startup checks reached files by name and size only (D12.2, no re-hash); digests are checked at
   a file's first read in the process, which re-fetches a rotted file by name (D6 Recovery).
6. A refused or failed fetch publishes nothing and leaves the mirror usable; only an I/O error
   after staging needs a restart. An adoption that changes nothing writes nothing.
7. A1 is not tagged on any job: the relay-chain half is shown by `test_relay_mirror` `chain` (in
   the suite), the voter half waits for S5.2.
