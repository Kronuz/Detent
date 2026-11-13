# namespace-s8: journal

State: ready for review; waiting on jobs 261113-014108-9248b2 261113-014110-061ecc 261113-014119-1dd450 261113-014123-bc39b0
Quest: .campaign/detent/quests/namespace-s8.md (the lead's; never edit it)

## Doing

Durability review CHANGES at cf28f3f2 (`.campaign/detent/reviews/namespace-s8-durability.md`).
Fixed at Detent:2b4bb2f2, mutants and docs at Detent:ec1c3148:

- D1: adoption's EEXIST branch now fsyncs the staged file and renames it over the existing name
  ("sync replacement", "rename replacement"), as repair does, instead of unlink + relink before
  the sync group. Test: `test_relay_mirror_crash` step 4 ("replace", 24 events): a reached file
  of the wrong size at startup, the same layout adopted again, a power cut after every event;
  every cut opens with the namespace failed or the upstream's bytes in place, and re-adopts to
  the uninterrupted mirror. Mutant relink-unsynced (the old branch) fails at the "never trusts
  zero-filled bytes" check, locally.
- D2: `NamespaceRelay::layouts` offers a namespace failed at startup. Test: `test_relay_chain`
  restarts B with one file lost; C syncs from B first: 0 collected, 0 fetched, ring kept; after
  B re-adopts, C fetches 0. Mutant hide-failed (the old filter) fails there, locally.

| Job | For | What |
|---|---|---|
| 261113-014108-9248b2 | A1, A2, A4, A5 | native suite at ec1c3148, --fresh |
| 261113-014108-5c54ac | A1, A2, A4, A5 | pins at ec1c3148: done, exit 0 |
| 261113-014110-061ecc | A3 | T7 crash matrix with the D1 run, --fresh |
| 261113-014119-1dd450 | A3 | mutant relink-unsynced (D1), fails-without |
| 261113-014123-bc39b0 | A2 | mutant hide-failed (D2), fails-without |

Mutant jobs run `tests/detent_relay/mirror_mutant.sh`, which already exits 0 when the mutant is
killed (1 survived, 3 no verdict), so they carry no `!`.

Confirm mapping for `campaign accept namespace-s8 durability --confirm`: D1=261113-014108-9248b2,
D2=261113-014108-9248b2 (the suite job; the crash matrix job 261113-014110-061ecc also shows D1).

Before the review (Detent:0b09208c): native 261113-003843-d3ad47 and pins 261113-003844-6e5e22
passed; crash 261113-003637-27c63b; mutants copy-keeps-tombstones 261113-003639-930a27,
sections-named 261113-003647-1556e1, keep-dropped 261113-003650-50d7ee killed. Superseded:
261113-003635-* (broken slot, msg-45).

## Review findings for a follow-up (no code here)

- D3: an I/O error while staging poisons the store until restart without removing the `.tmp-`
  files; fix: `clean()` and rethrow, poison only once linking or the table has begun; test with
  an injected write failure. Suits namespace-s8-wiring.
- D4: `MirrorStore` and `NamespaceRelay` are single-threaded (now said in the status doc); the
  wiring serializes calls or snapshots the table.
- D5 (nit): `RingSink` should refuse an entry outside (after, to] as UpstreamMoved.
- D6 (nit): the ring is memory only; the quest's Outcome says "persisted mirror of ... ring". The
  lead rules on the wording.
- D7 (nit): one namespace refused (FetchRefused) stops the whole adoption; per-namespace adoption
  is the owner's call.

## Next

1. Read the jobs; paste `campaign brief`'s table.
2. `campaign accept namespace-s8 durability --confirm D1=... D2=...` (the lead).
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
| A1 | 261113-003843-d3ad47, 261113-003844-6e5e22 (rerun 261113-014108-9248b2, 261113-014108-5c54ac); fails without: 261113-003650-50d7ee |
| A2 | 261112-121305-5dbe27, 261112-121305-c6bca1; fails without: 261112-120549-f1e18a, 261112-120552-fbda3d (rerun at the new head pending) |
| A3 | 261112-121306-11c4d7, 261113-003637-27c63b, 261113-014110-061ecc (D1); fails without: 261113-014119-1dd450 (D1), 261112-120540-e6efea, 261112-120543-c18350, 261112-120546-8f0fcd, 261112-121309-eb786c |
| A4 | 261112-121305-5dbe27, 261112-121305-c6bca1; fails without: 261112-120555-5fb518 (rerun pending) |
| A5 | 261113-003843-d3ad47, 261113-003844-6e5e22 (rerun 261113-014108-9248b2, 261113-014108-5c54ac); fails without: 261113-003639-930a27, 261113-003647-1556e1 |

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
- msg-45 (to the lead): the slot broke again; gate jobs resubmitted with --fresh.

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
13. Review D2: a failed namespace is offered downstream but still not answered by `frames` until
    adopted again (an answer would read through a missing file); downstream rings are kept, as an
    upstream with no frame for a namespace means Current.