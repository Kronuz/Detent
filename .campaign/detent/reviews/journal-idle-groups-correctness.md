# journal-idle-groups review: correctness

Commit: journal:d2ce548c5182f59a8fc6f817061dcf2e041c0c88
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261120-144344-ae3714 (A3 at d2ce548, Debug): idle_crash, split_crash, split_stale_node,
  table_damage, park_mid_turn, idle_damage, idle_unmanifested pass. The tests compare recovered
  content with a model of staged and acknowledged states (host_test.cc:842-860), not counts.
- 261120-144342-00de69 (suite at d2ce548, 57/57).
- The quest's seven mutants were killed at 7b64376, 48df21b and 893fd77, before or during the
  bounded-shard rewrite; I reran all seven at d2ce548 (below) instead of trusting the older commits.

## What I checked myself

Scripts are in `Detent/.scratch/detent/review-journal-idle-groups-correctness/`. `probe.sh CASE
[no-orphans]` includes `probes.cc` into host_test.cc. `mutants_at_head.sh` reruns the quest's
mutants.

- 261120-161513-029104: all seven quest mutants KILLED at d2ce548 (parked-as-open,
  wake-skips-commit, create-with-block, park-keeps-log, damaged-shard-fails-open,
  split-removes-early, split-keeps-parent).
- 261120-161412-33424b `probe_orphan` at head: passes. 80 open entries share one shard. A park that
  splits the shard fails at its directory sync after its renames, which leaves 4 files. A create in
  the slot is then acknowledged by its turn and survives a power cut and two restarts.
- 261120-161418-74c11a `probe_orphan` with `remove_orphans(slots);` deleted (mutant `no-orphans`):
  killed. After the crash, the failed split's children cover the root and win at open, so the
  acknowledged create reads `none` (status 0) on both restarts.
- 261120-161423-0ae8e9: the whole suite with `no-orphans`. Every suite test passes. The only
  failures are this review's three injected probe cases (log lines 39-48), so the mutant survives
  the suite.
- 261120-161425-a8d75c `probe_missing_leaf`: defect reproduced (below).
- 261120-161426-5dc762 `probe_strata_park`: defect reproduced (below).
- I read the code for Q1 to Q3 at every park, wake, create, split and orphan step:
  `Host::commit`, `close_into_table`, `park`, `load_table`, `host_detail::resolve` and `covered`,
  `TurnDriver::start`'s hook, `TurnLog::completed`'s copy notes after `forget`, the streaming
  `repartition`, and the Store path in `journal.h` (create_shared, recover_shared).

Answers to the lead's questions:

1. Crash at each write of park, wake and split: under the power-cut model, reopen finds each group
   exactly once. Nodes are prefix-free and `decode_shard` checks `holds()`, so no id is read from
   two files. The split ordering is right, the hook commit precedes the turn's write, and the
   forget comes after the directory sync. Two windows remain. A failed (not crashed) split commit is
   safe only through `remove_orphans`, which no suite test exercises (F1). A shard file lost
   outside the crash model deletes its healthy siblings at open (F2).
2. A CRC-damaged shard: the host opens, every id on the shard's path reads `lost`, the shard is
   never rewritten, and other shards recover (table_damage). The loss is reported only per group,
   on access; there is no host-level report (F4). A missing shard file is not covered: its loss is
   silent and amplified (F2).
3. A turn racing park: through the Store path a parked group cannot take a write. Holds, `hold()`
   and `park()` all run on the loop. `opened_` covers released groups with records in flight, and
   `staged()` covers pending and in-flight records. A forwarded copy that rides the turn is ignored
   at `completed()`, because the group is in neither `index_` nor `evicted_` after `forget`, and
   its original was dropped once, so the live counts stay right (turn_log.h:396-404). Only the
   first half of D10's mid-turn claim is tested (F5). Through strata's path, park never takes the
   group at all (F3).
4. Values: yes. Mutants: the seven are killed at head. D9's orphan rule has no test (F1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | D9's orphan rule ("the next commit of their slot removes them ... so no orphan shadows a later version of its node") is load-bearing and untested. Without it, a split commit that fails after its renames leaves children that cover their node at open and win over every later commit of that node, so later acknowledged creates read `none` and block reservations revert. | host.h:654 (`remove_orphans(slots)`); mutant `no-orphans` survives the suite (261120-161423-0ae8e9) and is killed by `probe_orphan` (261120-161418-74c11a) | Add `probe_orphan` (probes.cc) or an equivalent to host_test, and `no-orphans` to test/mutants.py with a `--fails-without` job. |
| F2 | yes | defect | A slot split into leaves that loses one leaf file (gone, not CRC-damaged) loses the whole slot at open. `resolve` sees the unstored root and children that do not cover it, so it calls every leaf stale and deletes them (host.h:263-276, 827). In the probe, 3 leaves became 0, and all 160 parked groups read `none`, never `lost`. `none` lets `create_group` start a new incarnation 1 whose artifact names can collide with the old incarnation's files. This also breaks D11's "one shard file" bound and its "never silent". | 261120-161425-a8d75c | Options: (a) never split an unstored node, so a slot's first commit writes its root whole, even past leaf_bytes, and a later commit splits it. Then "no stored ancestor and children that do not cover it" never comes from a crash, and open can treat the uncovered paths as damaged (lost, kept on disk, reported) instead of deleting. (b) Keep the current rule but never delete at open. Report and mark the uncovered paths lost. Test: `probe_missing_leaf`, where healthy leaves stay on disk and their parked groups read `closed` with their manifests. |
| F3 | yes | defect | The park contract does not reach strata's groups. strata (turn_seal.hh:111-144) calls `create_group`, `reopen_group` and `attach` with no `Host::hold`, and `opened_` is cleared only by `settle()` of a released hold (host.h:460). So `Host::park` skips every strata-owned group (host.h:517), yet Xapiand's turn-log-parity quest says "Xapiand's idle policy calls `Host::park`". strata's park-by-close (D2) goes through `close_into_table`, which does not erase `opened_`, so each parked group keeps a set node in Host's heap, against A2 and D3's "no per-group heap". D2 ("strata and Xapiand park a group by closing it") and D10 ("Xapiand calls `Host::park(loop)`") contradict each other. | 261120-161426-5dc762: parked 0 of 10, and `opened_` still holds 10 after 10 `close_group` calls | Pick one path and say it once in D2, D10 and Contracts, then tell Xapiand's lead. (a) Consumers park by `close_group`: `close_into_table` erases `opened_[loop]` for each closed group, and A2's counting test covers `close_group` with no hold. (b) `Host::park` also takes attached groups with no hold, no sealer and nothing staged. The erase from (a) is needed either way. Test: `probe_strata_park`, or a counting test over the no-hold path. |
| F4 | no | gap | A damaged shard is reported only per group, on access (`status() == lost`). The host has no report that a table shard was damaged, and the Store's error says "the turn log lost this group" (journal.h:593), which blames the wrong component. Groups the log held of that shard leave their artifacts on disk, as documented. | host.h:832, journal.h:593 | Optional: count or list damaged shards on `Host`, and word the error for both causes. |
| F5 | no | gap | D10's second mid-turn half (a forwarded copy of the parked group's checkpoint rides the in-flight turn, is dropped at `completed()`, and is ignored at recovery) is reasoned but not tested. park_mid_turn has no forwarding. | host_test.cc:1060-1083 | Optional: a park_mid_turn variant whose turn forwards the parked group's checkpoint. |
| F6 | no | nit | Outside journal: strata keeps a `closed_` map entry per closed group (turn_seal.hh:329), a heap cost per parked index on Xapiand's path. | strata include/strata/core/turn_seal.hh:125,329 | Tell Xapiand's lead. Not this quest's. |

## Not run

- Sanitizers and Linux; they are CI's.
- A2's 1M counting jobs (queued by the contributor); F3 shows the Store path is the only one they
  measure.
- A crash sweep over the first commit of a slot that splits an unstored root. I reasoned it from
  `resolve` and did not run it.
- `split_crash` and `idle_crash` under other `crash_policy` picks than `sector_cut`.
- The streaming re-partition beyond reading it. `placement_test` and its mutants are unchanged and
  pass in the suite.
