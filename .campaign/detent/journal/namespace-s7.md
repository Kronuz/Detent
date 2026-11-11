# namespace-s7: journal

State: waiting on jobs 261111-013953-ee8bac 261111-014002-b517ac 261111-014006-0f1d84 261111-014010-7d7284
Quest: .campaign/detent/quests/namespace-s7.md (the lead's; never edit it)

## Doing

D58 folded in (Detent:c171898): `detent_agent_store` (alias `detent::agent_store`) builds in
`src/detent_local/CMakeLists.txt`; the status doc says the store lands as a module and
namespace-s7-wire wires it. Evidence jobs at c171898 (supersede those at 627697e): native suite
261111-013953-ee8bac (A1, A2, A4), pins 261111-013953-bc7285, crash model 261111-014002-b517ac (A3),
mutants 261111-014006-0f1d84 (n6-rename-before-fsync) and 261111-014010-7d7284
(ndr4-unsynced-reuse), both --fails-without A3.

Local runs at 627697e (same code): test_agent_store, test_agent_catch_up pass; test_agent_store_crash
38,318 cases (630 distinct cuts), 102 republications, 60 repairs, 0 violations, 86 s. Each of the
three mutants is killed locally.

## Next

1. When the jobs finish: paste `campaign brief`'s Evidence table, check CI, set State to ready for
   review (or fix a failure). A durability review is the lead's to launch.

## Blocked on

Nothing. msg-21 ruled by the lead (D58): the target moved to src/detent_local; wiring is
namespace-s7-wire.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |

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
- Startup order: lock, `change` opened, barrier, `.tmp-` removal, table read and verified, `change`
  bump. A profile other than the store's refuses the store (D43).
