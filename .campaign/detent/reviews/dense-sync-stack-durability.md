# dense-sync-stack review: durability

Commit: Detent:9f4f681f7f2d9d76b79186ab1c524cf2fc451364 (branch head; code identical to Detent:dc773f5d, the later commits touch only the journal)
Verdict: APPROVE
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261118-055013-136e37 (passed at Detent:e6b4b48; only merges since): the DTPAGE03 T7 matrix, counters against the seam, and the grouped-order check pass.
- 261118-055014-19aed1 (n6, rename before the table's fsync): killed by a T7 cut ("power cut, restart, power cut after event 43 (rename table): agent store table: header").
- 261118-055016-95665a (owner group removed): exit 134, but at `test_paged_publication_artifacts.cc:171`, the counter check `matches()`, not a cut. See F1.
- A5 gates 261118-055412-a8791e (asan), -1bba5b (tsan), -608f32 (native).

The production order is main's `AgentStore::publish` (`src/detent_local/agent_store.cc:357-480`). This branch adds only a test hook (`disk_store.cc:46,78,435`; `store_core.hh:9-12`) and the test.

## What I checked myself

Scripts are in `.scratch/detent/review-dense-sync-stack-durability/` (`probe.sh MODE`, `make_patches.py`, `make_census_patch.py`). Every probe ran at Detent:9f4f681f in slot `dss-mutant`.

| Probe | Job | Result |
|---|---|---|
| P1: the payload group syncs after the table is renamed over `selected` | 261118-172137-508ee1 | Killed by a T7 cut: "power cut after event 39 (rename table)", store not Ready (`:429`) |
| P2: owner records never synced, with counters kept equal to the seam (group 1 syncs the directory only) | 261118-172140-56c888 | Killed by a T7 cut: "power cut after event 44 (rename table)", `:429` |
| P3: `tests/detent_agent/mutants/f2-reuse-unreached.patch` (adopt an unreached name whose bytes match) | 261118-172143-4f96d1 | Survives every cut; fails only at the order check `:321` (`files[0] == owners`). See F2 |
| P4: DiskLedger census. After each cut's open, after republish and after collection, the ledger's `used` equals the directory; a fresh reopen equals the running census; the names after collection equal the clean run's | 261118-180812-49ef0c (261118-172146-bcae5a failed to build: my patch's ordering) | Holds: 41 republications collected, no census drift, no leaked names |

Answers to the four questions:

1. **Power cut at each step.**
   - Every new file is staged under `.tmp-`, then linked to its final name, and only then synced. Before the rename, those names are orphans that no durable table reaches.
   - Startup removes `.tmp-` names. A later publication unlinks and relinks any orphan (`:386-401`), and collection deletes any orphan that is left.
   - The table reaches nothing until its own bytes are synced (`:463`, N6). Every name and byte it reaches is durable before the rename: all links precede group 1's directory sync, and both groups flush.
   - The rename is made durable by `sync_directory` before `change` is published.
   - Recovery therefore sees the old table whole or the new one whole. P1 and P2 show that T7 enforces both groups.
   - No barrier the per-file order needed was merged away. The payload group's directory sync is redundant, since the directory does not change between the groups (F3).
2. **Restart, then republish.**
   - The code reuses only names the durable table reaches whose bytes verify (`:391-397`). A half-published group is never adopted: it is unlinked and relinked.
   - P4 shows the census is exact after a cut and that no names leak past the next publication's collection.
   - The DTPAGE03 matrix does not exercise the kill-then-adopt hazard (F2).
3. **Counters.**
   - On macOS the test counts real `F_FULLFSYNC` calls at completion's `fcntl` seam (`PosixIO::replace_sync_calls`). Startup and publication counters equal it, groups == 2. These are device flushes, not calls.
   - On Linux, completion issues `fsync`, not `fdatasync` (`completion:560c288 posix.h` `files_sync`). Counted "device flushes" there are fsync calls by definition (F4).
4. **The tests.**
   - T7 cuts after every primitive, every fsync and every device flush of startup, publication, collection and close. That covers each barrier of the new order, with kill and four power-cut variants, and nested restart-republish-cut runs after power cuts.
   - 19aed1 targets a barrier that matters and is killed by a cut. 95665a does not prove what it claims (F1), but P2 shows that barrier is covered.
   - The untested cut is kill, then restart, then republish, then power cut (F2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Mutant 95665a ("owner records unsynced must fail DTPAGE03 T7") is killed by the counter check, not by a cut: removing `sync_group` drops `groups` to 1. The owner barrier is covered, but only P2 shows it. | 261118-055016-95665a log (`:171`, `matches`); 261118-172140-56c888 | Use `p2-owners-unsynced.patch` as A2's owner mutant |
| F2 | no | gap | No kill, restart, republish, power-cut cycle in the DTPAGE03 matrix. Kill cases are terminal, and nested runs start from `durable(root)`, so bytes a kill left in the page cache are modelled as durable. f2 (adopting an unreached name) survives every cut and is caught only by the order check. The reached-only rule is proven by `test_agent_store` (f2, 261111-021849-6ce213) and agent T7. | 261118-172143-4f96d1; `test_paged_publication_artifacts.cc` `matrix()` | Nest a recorded run after each kill with the event's own durability, as `tests/detent_agent/test_agent_store_crash.cc:390-400` does |
| F3 | no | nit | The power-cut model has only two entry states, Durable and All, so it cannot express a dropped group directory sync. In the real order it does not matter: every name links before group 1, and the payload group's directory sync changes nothing (a wasted fsync on Linux). | `agent_store.cc:371-425`; test `after_power_cut` | None needed; owner's call whether to drop the redundant directory sync |
| F4 | no | nit | On Linux, "device flushes" are fsync calls (`fsync`, not `fdatasync`). `record_fcntl` would count an `F_BARRIERFSYNC` as a flush, though the store never issues one. The `total` check omits `device_flushes`. | test `record_fcntl`, `matches`, `record()` total check | Optional: compare `total.device_flushes` too |
| F5 | no | nit | The perf doc's headline "2 groups with 3 device flushes" is the stacked tree Detent:d04794d, whose descriptor rides in the payload group. The code that lands (main's agent store) issues 4 on macOS because the table syncs alone. Writing the table before group 2 and joining it there would keep D11's invariant at 3. | `docs/perf/dense-sync-stack.md` 100K table (Main row: 4); `agent_store.cc:455-466` | Say in the doc that main's order is 4. Joining the table is the owner's call, outside this quest |
| F6 | no | nit | D11 says reused names join group 1; the code syncs them in no group. This is safe: a reached name was durable before the table that reaches it. | `docs/design/namespace-delivery.md:272`; `agent_store.cc:414-424` | Doc wording, outside this quest |

## Not run

- The Linux branch of the test (CI runs it), ASan or TSan builds of my probes, and kills or cuts inside completion's own syscalls.
- The 5M arms and the timings: these are performance, not durability.
- Hardware validity of macOS `fsync` per file then one `F_FULLFSYNC`: that is completion's documented mechanism, taken as given.
