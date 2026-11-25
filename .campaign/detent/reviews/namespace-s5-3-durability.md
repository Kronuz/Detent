# namespace-s5-3 review: durability

Commit: b7f621e3 (Detent:b7f621e3; re-review of Detent:33576164; Detent:4e5c4830 adds only the journal)
Verdict: APPROVE
Date: 2026-11-13

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This approval assumes the suite 261113-052400-cfef61 and the review mutants 261113-052403-7aca58
pass at b7f621e3, as the landing gate requires anyway. Both were still running when I wrote this.

## Re-review (b7f621e3)

F1 and F2 are fixed.

- **F1.** `History` hands `Fetch` a check per name. `PeerHistory` passes over a voter that holds no
  copy, holds a rotten one, or cannot serve it, and starts each round at the next voter in
  rotation (`peer_history.cc`). My rot-peer probe now installs and repairs.
- **F2.** The lane is `history::fetch_bytes(limits)`, which is max(image_bytes, the default policy's
  `file_bytes`): 256 MiB by default. The config loader sets it, and `Service` refuses peers below it.
  A policy whose bound exceeds the lane is refused at encode, decode and node options, and a
  SetLayout that would exceed it across the change applies as Conflict. `io_error` and
  `message_size` now end a fetch with each voter's answer instead of retrying forever. My probe's
  73 MB base is served.

**Count cut.**

- **Deterministic and byte-identical.** `items.size() > max_blocks` is a compile-time constant
  evaluated over replicated inputs. Replay recomputes it with the same `Layout::apply`.
- **Across a policy change.** It does not depend on the policy, so a policy change cannot move it.
  The cluster test reaches it after a SetLayout, at 33 seals, with byte-identical files on three
  voters (261113-052356-6966f1).
- **Crash safety.** The cut base is one more output in the seal's output group, under the same
  commit protocol as the cost and cap cuts, which the power-cut matrix covers.
- **Derivation (ruling 18).** It holds:
  - A ratio break bounds a chain by log_r.
  - An L break means the merge exceeded L_t, and the cost rule kept the younger bytes below
    4 L_t. So each L break raises the younger bytes by at least (4(r+1))/(4(r+1)-1) = 20/19 at
    r = 4.
  - The count therefore grows logarithmically with a base near 1, which needs a cut. My probe
    reaches 32 blocks within 80 seals of insert-only growth (probe_outputs).

**Bounds.** I checked the terms against the encoders and against apply:

- **Base.** Header 247 plus `state_bytes`. A base holds no tombstones, and each change's encoded
  size equals its footprint: my 73,260,000-byte namespace wrote a base of 73,260,000 + 51 bytes.
- **Block.** L plus one seal's changes. A squash past L only proceeds when the merge grows
  nothing.
- **One seal's changes.** At most seal_bytes − 1 plus one entry. An entry holds at most 64
  commands (`apply_batch`). A command's keys and definitions fit `request_bytes`, plus 16 per
  mutation. A drop adds no per-key changes.
- **Level-0 sections per namespace.** At most 2P + 1, because the directory position is fixed
  per incarnation.
- **Policy changes.** Widening each parameter covers a change from one policy to the next. Each
  accepted policy is itself within the lane, so files written under an earlier policy fit too.
- **Seal file outputs.** I suspected a squash that breaks on L re-lists the older block as a third
  output per namespace. The probe saw at most two over 80 seals with the count cut firing, so I
  drop that suspicion.

The bounds are a derivation, not a check: nothing refuses a written file above its kind's bound
(F8). A file above the lane now ends a fetch visibly, with each voter's answer.

## Evidence I relied on

- History tests 261113-052356-6966f1 at b7f621e3, exit 0:
  - transport: 18 fetched, 11 identical, 6 rotten copies passed over, and an unservable file ends
    with each voter's answer.
  - large base: a 73 MB base, an 8.4 MB seal file and an 8.4 MB block install through a
    268,435,456-byte lane, and the base is refused at 64 MiB.
  - cluster: the count cut fires after 33 seals and 59 files are identical.
  - power cut: 3078 cuts, 4417 in installs; names, layout and change history pass.
- From the first round, still valid for the unchanged write path:
  - 261113-033810-0c0530: suite at 42dc0361.
  - 261113-032601-1a5622 and 261113-033814-67ea69: mutants.
- The crash model is journal's `MemoryIO`. Write, sync, create, replace, remove and directory
  sync each count as one primitive. A name is durable only at a directory sync.

## What I checked myself

The scripts are in `.scratch/review-namespace-s5-3-durability/`. Run them with
`run_probes.sh PROBE_CC...` from a checkout root.

- 261113-053022-c73e32 at b7f621e3:
  - `probe_s53`, exit 0:
    - rot-peer install: installed.
    - rot-peer recovery: repaired.
    - rounds: 4 and 7.
    - cut-over-own: 1698 cuts, 0 bad.
  - `probe_large_base`, exit 0: the 73,260,051-byte base is served at `history::fetch_bytes`.
- 261113-054803-69da54 at b7f621e3, `probe_outputs`, exit 0: 80 seals of insert-only growth at α 1
  and period 1. At most 2 outputs per namespace per seal file; blocks reach 32 of 32.
- First round:
  - 261113-034724-30bd1b: F1 refused both cases, cut-over-own had 0 bad.
  - 261113-040148-932628: F2, the base was refused at 64 MiB.
- Reading the code:
  - `file_bounds` and the encoders: `encode_block`, `encode_base`, the seal file and the table.
  - Seal accounting in `state.cc`: `close_entry`, `count_change`, `apply_batch`, the drop path.
  - The squash and cut loop in `history_layout.cc`.
  - The administrative validation and SetLayout apply.
  - The node and service refusals.
  - The `PeerHistory` lasting and transient split.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no (fixed) | defect | A rotten copy on the first peer stopped repair and install. Fixed at b7f621e3: a check per fetch, the next voter on a bad answer, and rotation. | 261113-053022-c73e32 `rot-peer`; transport test "6 rotten copies passed over" | Done; mutants first-bytes-win, same-order and unchecked-fetch are in 261113-052403-7aca58. |
| F2 | no (fixed) | defect | A base above 64 MiB could not be fetched, and the fetch retried forever. Fixed: the lane is `fetch_bytes(limits)` (256 MiB by default), set by the loader and checked by `Service`. Policies are bounded per file kind, and Failed or oversized replies are terminal. | 261113-053022-c73e32 `probe_large_base`; 261113-052356-6966f1 large base | Done; mutants in 261113-052403-7aca58. |
| F3 | no | gap | The executor cost of serving one file was measured only from the page cache. F2's fix quadruples the bound to 256 MiB, so a cold read of the largest file now costs more on the serving voter, possibly the leader. | status doc Left open 5 | Measure append round trips while serving a cold file of the lane's size, or move the reader off the executor. |
| F4 | no | gap | Each restore attempt re-verifies everything fetched so far: 4 or 7 rounds here, and about 65 at P = 64. History is held about three times in memory during an attempt. | probe `rounds` | Keep verified results across attempts. |
| F5 | no | nit | After a write stalls at its directory sync, a restore in the same process can skip that sync for a name that already holds the same bytes. | `history.cc:116-117, 137-138` | After a stall, always sync the directory at the end of a restore's write. |
| F6 | no (fixed) | nit | The design doc named files by the namespace itself. | `docs/design/namespace-delivery.md` | Done. |
| F7 | no | gap | No test runs seal or table writes through the service's driver on PosixIO. | `service.cc` history_driver | Add a service test that seals before S6 enables positions. |
| F8 | no | gap | The file bounds are a derivation that nothing enforces when files are written. If a term is wrong, a file above the lane is written, and installs of it fail visibly but durably. The tests compare actual sizes with the bounds in one configuration only (large base). | `history_format.cc:481-519`; no use in `history.cc` | When a seal writes an output or seal file above its kind's bound, fail it (stall history) or assert in Debug, so a derivation error shows on the writer, not on a later install. |
| F9 | no | gap | No restart or power cut reaches a count-cut seal. The power-cut log never exceeds 32 blocks, and the cluster test does not restart after the cut. Replay is the same `apply`, so this is correct by construction but not exercised. | `test_history_cluster.cc` last phase | Restart one voter after the count cut in the cluster test and compare files. |
| F10 | no | nit | `max_blocks` is a format constant outside the replicated policy. Changing it in a later binary changes layout bytes, so voters on mixed versions would diverge. | `history_format.hh:124` | Note it in the format docs as a version-bearing constant, or carry it in the policy. |

## Not run

- The suite 261113-052400-cfef61 and the review mutants 261113-052403-7aca58 at b7f621e3 were still
  running, and S5.3's six and S5.2's seven (261113-052406-041cb0, -052410-390ee0) were queued.
- Sanitizer CI and the Linux matrix: the branch is not pushed.
- Real-device fsync; a cold-cache read (F3).
- A worst-case construction for every term of the seal and table bounds at once. I checked the terms
  by reading and the reaching case by probe, not with an adversarial maximizer.
- `PeerHistory` under faults on the live transport, beyond the transport test.
