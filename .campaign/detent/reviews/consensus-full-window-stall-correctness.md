# consensus-full-window-stall review: correctness

Commit: consensus:90ff87897ad8930991cbc647bff7e16d820f0553 (code at fe0d2dc; 90ff878 is docs-only)
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1: 261120-201357-bc0621 (test passes at fe0d2dc), 261120-201357-874d17 (fails with 8801b99's
  core.h), 261120-201358-1847ef (fails without the timeout's floor reset). Enough for the
  64 KiB-command shape the test models.
- A3: 261120-201346-c3e8de (loss, fix: 10 windows, 0 role changes, 0.49 MB/s, ran the full 60 s),
  261120-201347-0f5364 (reorder, fix: 2,048-commit cap in 44.4 s, no failures), 261120-201349-112b82
  (loss, main: 21 role changes, 30 NotLeader rejections, FAIL), 261120-201350-63af72 (reorder, main:
  passes, 3.62 MB/s). I read each log's checker output.
- A4: TSan 261120-201358-da8c6d (five Core tests including the new one).

## What I checked myself

Scripts in `.scratch/detent/review-consensus-full-window-stall-correctness/` (`probe.sh asis|guard`,
`big_entry_probe.cc`: the A1 test's Cluster with commands larger than the 64 KiB floor; passes iff
no append payload exceeds `Limits::rpc_bytes` and each load commits).

| Job | Code | Result |
|---|---|---|
| 261120-225013-f7ec72 | quest head | FAIL: appends of 3,276,800 B (100 KiB x 32, fast), 6,656,000 B (100 KiB x 66, 600 B/ms, 3 elections, commit index lost), 8,388,608 B (1 MiB x 8), 4,194,368 B (65,537 B x 64) |
| 261120-225017-b2ad4b | base 8801b99 | pass: largest 2,048,000 / 2,097,152 B; the slow path stalls as the quest describes (5 elections, 58 commits) |
| 261120-225015-1227c3 | quest head + `bytes >= rpc_bytes ||` guard | pass: largest <= 2 MiB everywhere; 100 KiB slow path 142 commits, 0 elections; fast paths within 3% of base |

Read by hand (core.h at 90ff878):

1. Safety (Q1). Pacing changes only the byte bound of `replicate()`'s batch loop
   (core.h:1199-1206). Entries stay contiguous from `peer.next`, the Flight records
   `previous + batch.size()`, terms and `durable_.commit_index` are unchanged, and `pace()` runs only
   on a success whose `matched == at->through` (core.h:1013-1015) and writes only `peer.frame`.
   Partial and superseded answers take the unchanged paths. No skip, duplicate, reorder or wrong
   match/next advance from pacing. The overflow of F1 changes only how many entries go, not which.
2. State (Q3). `Peer::frame` defaults to `frame_floor`; every Peer is built fresh on
   `become_leader` (core.h:1160-1163), on a membership add (core.h:732), and `peers_` is cleared on
   `follower()` and fencing (core.h:464, 470). A restarted follower's flight times out, which resets
   the frame to the floor (core.h:1137). Removed members are erased with their Peer (core.h:1114).
3. Oscillation (Q2). Shrink only above the target, double only within half of it and with the
   frame at least half used: a band between target/2 and target holds the frame steady. Heartbeat
   flights (0 bytes) pace nothing. `flight.bytes * target` cannot overflow (target < 2^31).
4. Checker (Q4). Ending the windows at the load's last sample is fair for the evidence: the fixed
   reorder run hit the 2,048-command cap with no failures; the fixed loss run ran all 60 s. See F2
   for what the rule does not check.
5. Doc (Q5). No reorder speedup claimed ("their runs vary more than they differ"). The main-loss
   cell matches 112b82. Its "One entry always goes, whatever its size" is true but more than one
   then goes (F1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | An entry larger than the paced frame unbounds the batch. `replicate()` always takes a first entry; when it exceeds `rpc_bytes` (now `<= peer.frame`, 64 KiB at start), `rpc_bytes - bytes` (core.h:1204) wraps and every later entry joins, up to `rpc_entries` (256). Main's Driver groups (`Flow{}`) never hit this because commands (<= 1 MiB) never exceed 2 MiB; with pacing any command over 64 KiB does. Results: appends of 3.2 to 8 MiB, past `rpc_bytes`, `codec.h` `control_frame_bound` (encode throws "control frame bound", codec.h:25-26) and `maximum_frame` 4 MiB (host_transport.h:358 drops the frame); and on the A1 slow path with 100 KiB commands the very stall the quest fixes returns (3 elections, commits lost). The latent bug exists at main for any `Flow::frame_bytes` smaller than an entry. | 261120-225013-f7ec72 (fails), 261120-225017-b2ad4b (base passes), 261120-225015-1227c3 (guard passes) | `if (!batch.empty() && (bytes >= rpc_bytes \|\| entry.payload.size() > rpc_bytes - bytes)) { break; }` (deliver()'s guard, core.h:1250). Test: a full_window_stall_test case with commands above 64 KiB (100 KiB on the 600 B/ms path, 1 MiB on the fast path) asserting the largest append is at most max(rpc_bytes-bound frame, one entry) and no election; `probe.sh asis` must pass. |
| F2 | no | gap | `full_window_check.py` accepts a load that ends early without checking why: it passes a load that answered at 20 s with one advancing window and `failures` (e.g. NotLeader rejections) but no recorded role change. The evidence is unaffected (c3e8de full 60 s; 0f5364 2,048 commits, no failures). | full_window_check.py:49-62 | When `ran < seconds`, require `commits` equal the cap and `failures` empty. |
| F3 | no | gap | A3's jobs build from `~/.scratch/detent/consensus-full-window-stall/a3/sustained.patch` and xapiand's `quest/xapiand/netlab-consensus` at e1144c2, neither committed in this quest; a scratch sweep makes A3 unrerunnable. | 261120-201346-c3e8de spec `build` | Land the patch in xapiand (msg-190) or commit it beside the checker. |
| F4 | no | gap | A1 asks for "dropped or reordered appends"; the test models TCP's result (in-order, slow links), not drops or reordering. The argument is sound for the Control lane, which is TCP. | test/full_window_stall_test.cc:23-28 | None needed; state the substitution in the quest's evidence. |
| F5 | no | nit | Below about 64 KiB per `frame_target()` (roughly 256 KB/s) the floor frame itself misses the target, and at about 128 KB/s it reaches `rpc_timeout`, so floor frames are resent while crossing. The doc does not name this lower bound. | core.h:324-335 | One sentence in append-pacing.md. |

## Not run

- The netem bench (A3) again; F1 at the Driver/host-transport level (whether encode's throw or the
  4 MiB drop is what a real group sees); TSan on the probe; the full suite at the head (CI's).
- Pipelined groups (`Flow::pipeline > 1`) beyond reading the code.
