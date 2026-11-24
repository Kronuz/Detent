# consensus-fetch-alongside-flake review: correctness

Commit: consensus:4dbaee01388321e91e845ca9927477cf71f11a8d
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261123-164650-bbaf94 (A2, 4dbaee0, Release with `-UNDEBUG`, so the asserts are live,
  CMakeLists.txt:347): a follower's 6 s sync gives 16 of 16 exact, 0 peers gone, a commit of
  6,111 ms within 8,038 ms (2,000 + journal 6,031 + stall 7), and the term kept.
- 261123-141159-efa9e4 (fails without, 16c5bfa): the same injection gives 13 of 16 exact, all three
  inexact fetches `connection_aborted` at 5,031-6,116 ms, 2 peers gone, no wrong bytes. The
  exactness assert differs only by placement between the two commits (the bounds do not feed
  `exact`), so A2's fails-without is attributable to the fix.
- 261123-194343-b857a5 (A1, signature.sh at PRE=16c5bfa): the same result again, rc 134.
- 261123-164653-3cb87a and 261123-164658-9dea3a (A3, 4dbaee0): 0 of 25 failed each, beside 4
  burners; the host's take stays 78-126 ms.
- CI macos-x86_64 log of run 44996244171 (job 137144962603, saved under the scratch directory
  below), line 649: "13 of 16 exact; ... slowest 7077 ms; ... node 1 leads in term 1 throughout: NO".

## What I checked myself

Scripts under `.scratch/detent/review-consensus-fetch-alongside-flake-correctness/` in the home.

- Placement (Q1): `links_on` and `groups_on` use the transport's own hash
  (`host_detail::loop(slot(id), L)`, journal host.h:125). The assert on `loop_of_node` against
  `loop_of_group` (groups_test.cc:2566-2568 (assert at 2568)) runs through the transport. With 3 node ids and 4
  loops, a free loop always exists by pigeonhole, so the placement survives any slot-hash change.
  Only the comment at groups_test.cc:2539 states today's values (1, 3 and 0).
- Consensus frames still share the fetches' connections (Q3): a Driver's frames go through
  `here.out[to]` to `loop_of_node(peer)`'s link (host_transport.h:489-506, 863-866), the same link
  `fetch` uses. The D102 property (consensus frames are not starved by 8 MiB fetches through 1 MiB
  lane budgets on one connection) is still exercised. Only the Driver's co-location with a busy
  link loop is gone (F3).
- Leader-side sync, probe `leader-sync.sh` (patches the case's slow node to the leader inside the
  job checkout only):
  - 261123-200326-1b5a25 at 4dbaee0: 16 of 16 exact, 0 peers gone, commit 6,134 ms within 8,045 ms,
    a term change to term 2 allowed ("the host held it past the election timeout"), rc 0.
  - 261123-200329-a9c5f6 at 16c5bfa (expected to fail): term NO, slowest 5,121 ms, 2 peers gone on
    nodes 1 and 3, but 16 of 16 exact.
- The sibling quest's claims (Q4): `bsd_completion.h:105-153` sends macOS reads and writes to AIO
  and syncs to the fallback worker (`work_.release()`, line 153). FreeBSD uses aio_fsync. Blocking
  entry: DRIVER.md:153-155. `InlineCompletion::run` drives synchronously (completion seam.h:76-83).
  So `TimedInline.longest` is the operation's wall time on the loop.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | The reproduction gives half of CI's signature, not "identical to CI" as the journal says. A follower's slow sync gives 13 of 16 with the term kept and commits capped near 2 s (slowest 2,052 ms). CI had 13 of 16 with the term lost and a 7,077 ms commit. A leader's slow sync gives the term change and a 5,121 ms commit, but 16 of 16 exact. CI's run fits both journals syncing slowly at once (all three share the runner's disk), which neither probe injects. The fix holds for each half: bbaf94 (follower) and 1b5a25 (leader) both pass. The cause stays the one named, and the sibling quest's hazard is confirmed for both roles. | efa9e4, b857a5, a9c5f6, 1b5a25; CI log line 649 | Journal wording: "matches CI's fetch half; the term and 7 s commit need the leader's sync slow too (a9c5f6)". Optional: let FETCH_ALONGSIDE_SLOW_SYNC slow every node's next sync. |
| F2 | no | nit | `leads = kept \|\| host >= 1'000` takes `held` as the maximum over all nodes. A follower's 1 s sync therefore also excuses a leader's term change. In bbaf94 the term is kept anyway, and a resumed follower may legitimately campaign. | groups_test.cc:2680-2681 | Keep it, or take only the leader's journal hold and the stall for the term allowance. |
| F3 | no | gap | Moving the group off every link loop drops coverage of a Driver sharing a loop with fetch-serving links (TLS on 8 MiB, reader copies). Production places them together by default (host_transport.h:424-429). Lane sharing per connection is still covered. | groups_test.cc:2539-2551 | Owner's call. Option 1: keep it as is. Option 2: once consensus-inline-sync-loop lands, put the group back on a shared loop, or add a second case with native completion on a shared loop. |
| F4 | no | nit | The search starts at loop 0, which also holds the acceptor and the handshakes (host_transport.h:484). Under another hash, the group could land there, and a slow sync would delay redials; established links would not be affected. Today it lands on loop 2. | groups_test.cc:2547-2549 | Optionally search loops 1..L-1 first. |
| F5 | no | nit | The comment "Nodes 1, 2 and 3 place their links on loops 1, 3 and 0 of 4" hard-codes today's hash outputs. The code computes and prints them. | groups_test.cc:2539 | "the group takes a loop no node's links use (3 nodes, 4 loops: one is always free)". |
| F6 | no | nit | Evidence scripts `repro.sh` and `signature.sh` live in `~/.scratch/detent/consensus-fetch-alongside-flake/`, outside both checkouts, so the A1/A3 jobs cannot be rerun once that directory is swept. | job specs of b857a5, 3cb87a | Keep the directory until landing, or cite them in the journal. |

Q4: consensus-inline-sync-loop states the hazard correctly (shared slot hash; native completion
syncs off the loop; InlineCompletion syncs on it). One correction to the lead: in a single group, a
follower's slow sync costs peers and fetches but not the election (efa9e4, b857a5: term kept). The
election is lost when the sync is on the leader (a9c5f6), or across groups that share the loop. Its
A1, "no peer is lost and the leader keeps its term", fails at main through "no peer is lost". This
quest's test does not depend on that quest: it avoids the hazard by placement.

## Not run

- CI itself: I did not confirm F_FULLFSYNC as the runner's blocker. The case now prints the
  journal's hold and the stalled loop's stack, so the next CI failure will settle it.
- A combined injection (every node's sync slow at once).
- The case beside the install test (repro.sh's BESIDE argument), which is how CI's two failures
  ran. The A3 jobs ran with burners only.
- TSan or other sanitizers on the new LoopLags and TimedInline helpers (they are CI's). I read the
  helpers: atomics on every shared field, `probes` held by `shared_ptr` beyond the sampler's
  lifetime, and `dumped` touched only by the sampler thread.
- A4: the suite and `ci:ci` at the landed commit belong to the landing gate.
