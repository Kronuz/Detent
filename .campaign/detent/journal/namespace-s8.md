# namespace-s8: journal

State: ready for review; waiting on jobs 261113-003635-494354 261113-003635-834187 261113-003637-27c63b 261113-003639-930a27 261113-003647-1556e1 261113-003650-50d7ee
Quest: .campaign/detent/quests/namespace-s8.md (the lead's; never edit it)

## Doing

Session 2 (after namespace-s5-2 landed): main merged (Detent:728fe958); layout gets an explicit
cut (a voter's empty base is no file); `plan_answer` replaced by `NamespaceRelay` (W8 reader and
W6 source over S6's `NamespaceHistory`); `test_relay_chain` over three real voters.

Local (Detent:0b09208c): `test_relay_chain` ok in 46 s wall (0.3 s user; the cluster's syncs):
14 steps, 2,349 file comparisons byte-identical, 1,674 cursors with identical W6 bytes per hop,
C restarted with 0 files fetched; `test_relay_mirror` ok; T7 mirror 14,860 cases, 0 violations.
Mutant smoke, locally: copy-keeps-tombstones, sections-named, keep-dropped killed.

| Job | For | What |
|---|---|---|
| 261113-003635-494354 | A1, A2, A4, A5 | native suite (`test_relay_chain`, `test_relay_mirror`) |
| 261113-003635-834187 | A1, A2, A4, A5 | pins |
| 261113-003637-27c63b | A3 | T7 crash matrix |
| 261113-003639-930a27 | A5 | mutant copy-keeps-tombstones (fails-without) |
| 261113-003647-1556e1 | A5 | mutant sections-named (fails-without) |
| 261113-003650-50d7ee | A1 | mutant keep-dropped (fails-without) |

Earlier mutant jobs (A2-A4, at 5afd10fb/89157642) stand; collect-missing-unlink (261112-120558-1bde4c)
is retired as equivalent (ruling 7).

## Next

1. Read the jobs; paste `campaign brief`'s table.
2. Durability review (the lead launches it).
3. The lead rules on Blocked on 1.

## Blocked on

1. **HTTP wiring of the relay service** (journal "Left open" 1 in docs/status/namespace-s8.md).
   The relay's reading and answering is `NamespaceRelay` (library, tested in process over real
   voters). Wiring it into `service.cc` needs the authority to export its layout and serve
   history files and W5/W6 over HTTP: files by name are namespace-s5-3's peer endpoint, W5/W6
   are namespace-s6-cutover's, and no quest exports the layout. Options: (a) land S8 as the
   library plus in-process proof, and a follow-up quest after s5-3 and the cutover adds the
   relay routes (`layouts` as a `DTMIRR01` table body, files by name, W5 sessions) and an HTTP
   `Upstream`, with the authority's layout export in s5-3 or the cutover; (b) S8 adds the relay
   routes now and tests relay-to-relay over HTTP, the authority upstream waiting. Recommend (a):
   one HTTP shape for authority and relay, decided where the authority's endpoints are.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending 261113-003635-494354, 261113-003635-834187; fails without: 261113-003650-50d7ee |
| A2 | 261112-121305-5dbe27, 261112-121305-c6bca1; fails without: 261112-120549-f1e18a, 261112-120552-fbda3d (rerun at the new head pending) |
| A3 | 261112-121306-11c4d7 (rerun 261113-003637-27c63b); fails without: 261112-120540-e6efea, 261112-120543-c18350, 261112-120546-8f0fcd, 261112-121309-eb786c |
| A4 | 261112-121305-5dbe27, 261112-121305-c6bca1; fails without: 261112-120555-5fb518 (rerun pending) |
| A5 | pending 261113-003635-494354, 261113-003635-834187; fails without: 261113-003639-930a27, 261113-003647-1556e1 |

## Result

Pending the jobs. Docs: `docs/status/namespace-s8.md` (how the mirror adopts, starts, reads,
repairs and plans answers; what each test shows).

## Lead rulings received

- D84 (Detent:86704b50, merged at quest/detent/namespace-s8 7f2c16ce): A5 added (W8); S8 lands
  after namespace-s6. Build A1-A4 now, A5 against S6's quest file.

## Messages

- msg-37 (closed by the lead): S5.2 landed; main merged, A1's voter half proven on real blocks
  in `test_relay_chain`.
- msg-38 (closed by the lead): slot rebuilt; jobs resubmitted.

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
7. The mutant collect-missing-unlink (job 261112-120558-1bde4c survived) is equivalent:
   completion's `PosixIO::remove` ignores ENOENT, so the existence check before the collector's
   unlink did nothing. The check and the mutant are removed; `collect_missing` stays as a test.
8. A layout names its cut; the base is optional (a voter writes no file for an empty base).
9. `plan_answer` goes: S6's `select_answer` and encoder answer from `MirrorHistory`, the mirror
   as a `NamespaceHistory`, so relay answers are the authority's bytes by construction.
10. The relay library links `detent::authority` (state and S5.2's block codec, no consensus) to
    decode blocks for answers; relays stay readers (W8).
11. A1 and A5 run over real voters through the cluster harness (tests/detent_authority's
    `cluster_harness.hh`); the authority's `Upstream` (layout from `Layout::table()`, files from
    its directory, frames from an oracle that replays every write) is test-side until the
    authority exports them (Blocked on 1).
12. Relay mutants for the reader: copy-keeps-tombstones, sections-named, keep-dropped. A ring
    reset on a new incarnation is also implied by D3 (positions are never reused under a name,
    so a new incarnation's s is above the old ring's head); the explicit incarnation check stays
    and its mutant is equivalent against real voters, so it is not in the set.