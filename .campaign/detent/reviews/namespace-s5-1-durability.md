# namespace-s5-1 review: durability

Commit: ae6441cebb78dacb349aefa4f0495f25ae5863c8
Verdict: APPROVE
Date: 2026-11-10

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

## Evidence I relied on

- Native suite 261110-140332-98a69b, exit 0, 225 of 225 tests passed (one skipped:
  `test_local_trusted_paths`). It ran at Detent:e95341b; `git diff e95341b ae6441c` touches only the
  journal, so the code is the same. The suite includes `test_authority_namespace_positions`,
  `test_authority_namespace_cluster` (44 s) and `test_authority_namespace_power_cut`. Tests build
  with `-UNDEBUG` (`tests/CMakeLists.txt:2`), so their asserts are live in Release.
- Pins 261110-140332-333571, done, exit 0.
- GitHub CI run 42912730188 on `quest/detent/namespace-s5-1` at Detent:7e98828 (ae6441c merged
  with main; the merge adds only the ci.yml trigger): linux-asan-ubsan, linux-tsan, linux
  clang/gcc/arm64, macos-arm64 and macos-x86_64 all passed.

## What I checked myself

Scripts are under `.scratch/review-namespace-s5-1-durability/`. Rerun one with
`campaign job submit --quest namespace-s5-1 -- sh <dir>/run_probe.sh <dir>/<probe>.cc`.

- Job 261110-141944-a71bb1 (`probe_prefix_power_cut.cc`) passed. It runs the A5 matrix and checks
  each cut's recovered state, before any retry, against the reference state after step k. The
  state compared covers revision, bytes, directory, the a/b/c entries, records with values, and
  the producer's high water; k must be at least the number of steps acknowledged. Result: 352 cuts
  over 352 operations, 0 violations. Every cut recovered exactly its acknowledged prefix. In 27
  cuts the drop was the last acknowledged step, and in 16 the enabling entry was.
- Job 261110-143242-606755 (`probe_drop_history.cc`) passed. It attaches a `ChangeHistory` to a
  real `Node`, enables positions, writes, drops the namespace and re-creates it. A cursor from
  before the drop is not covered by deltas (floor 5 = the drop's revision), the drop has no delta
  body, and a namespace whose position differs from the group revision is a gap. Job
  261110-142033-eecc45 is the same probe and failed only to link (`ChangeHistory` lives in
  `detent::authority_export`), not in a test.
- Code reading of the diff `main...ae6441c`:
  - Inputs are canonical. Incarnations derive from the nonce in the entry, the context, the
    directory position and the name. Enabling walks the source map in name order, and the stepped
    and synchronous paths agree (tested). Live sizes use the same encoder at apply
    (`footprint`) and at decode (`stored_footprint`). Every mutation key is bound to its command's
    namespace (`state.cc:212`), so the per-namespace size changes are exact.
  - The image (`DTAIMG02`) carries the seed, the directory head and each row's position,
    directory position, live flag and incarnation. The 240-byte row bound is exact:
    8 + 199 + 8 + 8 + 1 + 16.
  - Administrative payloads come only from `Node` (`PreparedProposal`'s constructor is private to
    `ProposalBuilder` and `Node`), so no client can inject one.
  - `checkpoint()` refuses while an application is in flight, so no image captures a half-applied
    enabling entry.
  - No compatibility shim: `DTAIMG01` is refused, and nothing in the diff recognizes legacy data.
    The state before the enabling entry is the design's (D3, D6), not a shim.

## Risk questions

1. Canonical: yes, by construction and by test. Voters agree after an install and after a restart
   (`test_namespace_cluster.cc`), and image decode re-derives each live incarnation and checks that
   the sizes sum.
2. The matrix covers both the enabling entry and the drop, including an image that holds a
   tombstone. Recovery keeps every acknowledged step (probe a71bb1). The suite's own check is
   weaker; see S1.
3. Not with this code. `answer()` returns FullCopy on an incarnation mismatch and Absent for a
   tombstone. On today's group-revision wire, the drop moves the revision with no batch, so
   `ChangeHistory::publish` resets the floor (probe 606755). Positions under one name never repeat,
   so old-incarnation preconditions conflict.
4. Reachable and tested through the real `Node` in the cluster and power-cut tests. Nothing in
   production calls `propose_administrative`, `answer`, `namespace_cursor` or
   `for_each_directory_entry`. Those are the contract S5.2 and S6 consume (R1), not half-wired code.
   The history-reset path has no test in the suite (S2).
5. No.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | gap | The A5 test checks acknowledged steps only through the producer's high water and the enabling flag. A lost acknowledged drop that was the last acknowledged step would be re-applied by the retry and hidden. It does not assert that any cut acknowledged the drop. The property holds (probe), but the suite does not prove it. | `tests/detent_authority/test_namespace_power_cut.cc:216,224`; job 261110-141944-a71bb1 | Fold the prefix-state check from `probe_prefix_power_cut.cc` into the test, and assert that some cut has the drop as its last acknowledged step. |
| S2 | no | gap | The status doc says "A drop resets the history at publication", but no suite test attaches a recorder across a drop. | `src/detent_authority/node.cc:398`; job 261110-143242-606755 | Add `probe_drop_history.cc`'s checks to the suite when S5.2 or S6 first proposes a drop. |
| S3 | no | nit | Image decode accepts a directory head above every entry's directory position. Canonical writers never produce one, but decode could check that the head equals the highest directory position it read. | `src/detent_authority/state.cc:1999,2052` | Require `directory_head_ == max(entry.directory)` at the end of stage 3. |

## Not run

- No local ASan, UBSan or TSan; CI's runs were accepted.
- No disk-full or I/O-error injection beyond what the journal's `MemoryIO` power cut models.
- No crash matrix with more than one voter. A4 covers install and restart without cuts.
- I did not review S5.2 or S6 consumers, the agent side of a drop (deferred to S7), or the
  history's wire semantics after enabling beyond the gap rule.
