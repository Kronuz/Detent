# namespace-s7: journal

State: waiting on jobs 261111-021846-f70216 261111-021846-db2959 261111-021847-4e419d 261111-021849-6ce213 261111-021851-2e1cef 261111-021853-b98c22 261111-021855-1b868d
Quest: .campaign/detent/quests/namespace-s7.md (the lead's; never edit it)

## Doing

D59 fix round on the durability review (CHANGES, `.campaign/detent/reviews/namespace-s7-durability.md`).
Code at Detent:096fea29 (fixes) and the mutant regeneration after it.

- F1: `ChangeWriter` (`src/detent_local/change_file.cc`) extends its own `change` when it is shorter
  than a word (owner, one link, no group/other write and a mode-only ACL checked first); readers keep
  refusing it, and a ragged `change` past a word still refuses. DiskStore shares the writer. T7 now
  models `change` (`test_agent_store_crash.cc`): never synced, so a cut keeps it empty or at its size,
  plus an explicit kill between its creation and its sizing. `test_agent_store` `short_change`: an
  empty and a 3-byte `change` open, without and with `selected`.
- F2: reuse reaches only names the durable table reaches (`agent_store.cc` link step); any other
  existing name is unlinked and relinked, so reused names leave sync group 1. `test_agent_store`
  `failed_sync`: the sync seam fails the pack's fsync with EIO, the store restarts, and the pack is
  relinked (a new inode), never the one whose fsync failed.
- Mutants: `ndr4-unsynced-reuse` removed (no reused name is synced any more), `ndr4-trust-reused-name`
  regenerated, `f1-short-change-refused` and `f2-reuse-unreached` added.
- F3 is namespace-s7-wire's; F4-F6 dropped (D59).

Local at 096fea29: test_agent_store passes; T7 42,024 cases (903 distinct cuts), 113 republished,
65 repairs, 0 violations, 51 s; test_change_file passes.

## Next

1. The lead accepts the durability review with the finding-to-job map below.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261111-021846-f70216 (native), 261111-021846-db2959 (pins) |
| A2 | 261111-021846-f70216; 261111-021853-b98c22 (ndr4-trust-reused-name, --fails-without) |
| A3 | 261111-021847-4e419d (T7); 261111-021849-6ce213 (f2), 261111-021851-2e1cef (f1, T7), both --fails-without |
| A4 | 261111-021846-f70216 |

Jobs at the fixes' head (journal commits only after Detent:39578e49). Superseded: everything at c171898.

### Finding-to-job map (D59)

| Finding | Job | What it shows |
|---|---|---|
| F1 | 261111-021846-f70216 | suite: `test_agent_store` short_change, `test_change_file` |
| F1 | 261111-021847-4e419d | T7 with `change` modelled, cut empty and between creation and sizing |
| F1 | 261111-021851-2e1cef | f1 mutant (writer refuses a short `change`): T7 fails |
| F1 | 261111-021855-1b868d | reviewer's probe rebuilt at the fix: `change-zero` lines should read opened |
| F2 | 261111-021846-f70216 | suite: `test_agent_store` failed_sync |
| F2 | 261111-021849-6ce213 | f2 mutant (reuse an unreached name): `test_agent_store` fails |

The probe job builds the worktree's `.scratch/build` and points the stale
`tests/detent_agent/libdetent_agent_store.a` (pre-c171898 location) at the rebuilt library, so
`run_probe.sh` links the fixed store unchanged.

## Result

D8's agent store as a module (`include/detent_local/agent_store.hh`, `src/detent_local/agent_store.cc`):
the namespace table `selected` (codec with one refusal), derived generation and segment ids (no
storage identity, pinned), publication by `linkat` from `.tmp-` names with verified reuse, orphan
relink (N1) and `ReachedNameMismatch` for D10's repair, D11's grouped syncs (table fsynced before the
rename, N6) and startup (one barrier, `.tmp-` removal, per-entry verification, `change` bump).
Tests: `tests/detent_agent/test_agent_store.cc` (A1, A2), `test_agent_store_crash.cc` (A3, T7),
`test_agent_catch_up.cc` (A4 and the absent-key tombstone on the real paged builders). How it works
and the evidence map: `docs/status/namespace-s7.md`. Open: wiring DiskStore, the coordinator and the
readers to the store (namespace-s7-wire, D58); `.lease` and the
collector of unreached names (S9 and later).

## Rulings I made

- The agent store is a module beside DiskStore (its own directory), the `agent-store` contract S8
  and S9 consume; it carries generation files as bytes and verifies them by SHA-256.
- One verification rule for a reused name: SHA-256 of its bytes equals the digest of the bytes the
  cycle holds (equivalent to D8.4's per-kind digests: the owner checksum is the pack's SHA-256, the
  table's digests are the root's and `.read`'s).
- Links first, then the two sync groups: payload names may be durable before their bytes, which the
  table never reaches until after the second group; a crash there leaves orphans N1 relinks.
- A failed entry's `.live` segments stay in the reached set when that record decodes, so the orphan
  rule never unlinks a file a failed namespace may still name.
- A4's "same store bytes": the paged state commitment (the root page's placement-free digest over
  every namespace revision, key, revision and value). A catch-up and a full copy derive different
  generation ids by design (D8.1's base input), so file bytes cannot match; the state does.
- S6's answers are modelled as deltas: the ring as its transactions, the block as each key's last
  change grouped by revision. The paged overlay already makes an absent-key delete a no-op; no code
  change was needed, only the test.
- F1 takes the writer-extends fix (D59's smaller option): a few lines in `ChangeWriter`, no new
  name or link step; the `.tmp-` link would add a second creation path beside DiskStore's.
- F2: dropping reused names from sync group 1 follows from reached-only reuse (each was synced before
  the table that reaches it was renamed); T7 still checks every reached file after every cut.
- Startup order: lock, `change` opened, barrier, `.tmp-` removal, table read and verified, `change`
  bump. A profile other than the store's refuses the store (D43).
