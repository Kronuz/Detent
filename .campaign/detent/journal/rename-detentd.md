# rename-detentd: journal

State: waiting on jobs 261119-064309-e7a340 261119-064310-b5175d 261119-064311-8a1334
Quest: .campaign/detent/quests/rename-detentd.md (the lead's; never edit it)

## Doing

msg-106 (the lead's ruling) folded in: namespace-s8.md:85 and namespace-s1-s2.md (not HISTORY)
renamed; `DETENTD_*` in benchmarks/logging_compare.py and the usage lines of test_dns_voters.py and
test_lost_quorum.py renamed; the A1 check is case-insensitive and allows the docs/status HISTORY
files. Suite, A1 and its mutant queued at the head.

## Next

1. When the jobs finish: paste `campaign brief`'s table; A4 is CI at the head (push the branch;
   report if refused).

## Blocked on

Nothing.

## Messages

- msg-106 (lead's ruling): done at Detent:1ccfef49, as described under Doing.
- msg-107 (file set holds the renders and reference/cli/detent-authority.md): nothing more to change; those paths were already committed in Detent:ad8c73c8.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | check 261119-064310-b5175d and its mutant 261119-064311-8a1334 (`DetentD` appended to README.md must fail), both queued; script `.campaign/detent/journal/rename-detentd.check.sh` |
| A2 | suite 261119-064309-e7a340 (queued) |
| A3 | `diagrams/01_overview.svg`, `diagrams/07_agent.svg`, `diagrams/08_detent_deps.svg`, `diagrams/09_thirdparty_deps.svg`, `diagrams/10_how_actors.png`, `docs/diagrams/actors-overview.png`, `docs/diagrams/actors-where.png`: re-rendered with the d2-diagrams script, each composited on white and #0d1117 and checked: no overlap, no arrow through a box. Sizes at 1x: 918x385, 1057x430, 1200x746, 1004x807, 1094x540, 951x463, 1043x644 |
| A4 | CI running on the pushed branch at Detent:1ccfef49 |

## Result

`detentd` is `detent-authority`: main at `src/detent_authority/main.cc` (git mv), target and binary
`detent-authority`, `detent-authority.json`, `DETENT_AUTHORITY_{PORT,REACTORS,RUNTIME_DIR}` (and the
refused `DETENT_AUTHORITY_WORKERS`), `detent-authority-<port>.pid/.log`, service files
(`detent-authority.service`, `org.detent.detent-authority.plist`, rc.d `detent_authority`), CI,
tests, tools, docs, website (`reference/cli/detent-authority`), diagrams. CHANGELOG has the entry.

## Rulings I made

- One mechanical rule: `detentd` becomes `detent-authority` in every name (config, pid, log, units),
  `DETENTD_` becomes `DETENT_AUTHORITY_`; identifiers use `detent_authority` (rc.d name and rcvar,
  `authority` for Python variables and the D2 node id). The config cannot be `authority.json`:
  that name is the authority definition passed by `--authority`.
- Kept as dated records (the check allows them, the D123 principle):
  1. `benchmarks/*.json` (8 files): recorded results with file hashes and captured output.
  2. `docs/perf/m1-w5-evidence/classic-daemon-log-diff.txt`: captured log of the old binary.
  3. `docs/handoffs/detent-codex-2026-11.md`: marked HISTORY.
  4. `docs/dev/docs-site-audit.md`: dated audit of `Detent:5b50eb3`, line refs into that tree.
  5. `docs/design/http-coroutines.md`: the 2026-11-03 W2 design, refs into the pre-W2 tree.
  6. `docs/VALIDATED_ARCHITECTURE.md`: dated 2026-10-02 review of the removed classic code.
  7. `tests/detent_authority/revert_{ready,followup}_check.sh`: restore BASE's `src/detentd/main.cc`;
     they only work at commits before the rename.
  8. CHANGELOG: the rename entry and two lines naming files removed under the old name.
- `docs/perf/m1-w5-evidence.md` and `docs/DEPENDENCY_UPGRADES.md` say "the authority daemon" where
  they describe old builds, so no mention stays and nothing is falsified.
- `tools/w5/http_replay.py --detentd` becomes `--daemon`: it replays against old builds of any name.
- `tests/docs/check_docs.py` reads flags from `src/detent_authority/main.cc` only, not the whole
  `detent_authority` directory (the old `detentd/` held only main.cc).
- Diagrams 01, 07, 08, 09 were in plain d2 style and over size; they take the house header, and
  01 and 07 become 2x3 grids with shorter edge labels, 08 goes `direction: right` with shorter
  container titles, 09 `direction: down`. Each keeps its .svg format; the PNG ones stay PNG.
