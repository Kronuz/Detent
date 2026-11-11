# consensus-dual-stack: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-dual-stack.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done; awaiting review.

## Next

1. Lead: review and land (squash the two PROBE commits).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261111-111524-8d12f0; fails without: 261111-110219-4d2aa0, 261111-111521-3f02d4 (mutant v6-only; macOS default is dual-stack, so a plain revert passes here). FreeBSD (v6-only default) runs `dual-stack`: ok in CI run 43076028878, and all jobs green at consensus:3d988d1 in CI run 43085473756 (FreeBSD and ASan green on 3 attempts) |
| A2 | `CHANGELOG.md`, 2026-11-11 Changed, first entry: a `Peers` whose `PeerOptions::listen` is `::` opens its acceptor with `v6_only` off |

## Result

When `PeerOptions::listen` is `::`, `Peers` opens the acceptor with `v6_only(false)` (peers.h),
so one socket serves IPv4 and IPv6 peers on every OS. New `dual-stack` transport scenario: voter
three listens on `::`, voter one dials it over 127.0.0.1, voter two over ::1; all commit with 4
connections each. Verified on FreeBSD (the v6-only default) in CI, and the `v6-only` mutant
(`test/transport/dns_mutants.sh`) is caught on macOS. Also fixed forward: the FreeBSD `outage`
abort that had main's FreeBSD CI red was a test race (see Rulings), now `Cluster::fully_connected`.
Docs: CHANGELOG.md. `docs/mechanisms/peer-dns.md` untouched. Open: nothing.

## Rulings I made

- A1 host note: this host is macOS, whose default is dual-stack, so a plain revert of the
  `v6_only(false)` line does not fail the new `dual-stack` scenario here; FreeBSD's v6-only
  default is what CI's FreeBSD job (where enabled) checks. To still show the test detects a
  v6-only acceptor, `test/transport/dns_mutants.sh` gains a `v6-only` mutant (sets
  `v6_only(true)`, what a v6-only OS default does); the --fails-without job runs it.
- Test: voter three listens on `::`; voter one dials it at 127.0.0.1, voter two at ::1 (lower id
  dials), all three commit and hold 4 connections each. Skips if the host has no ::1.
- Only exact `::` triggers the option; other addresses, including v4-mapped, bind as before.
- Superseded jobs: 261111-100403-c63f54, 261111-100400-7132ce (replaced by the two above after reordering the scenario).
- FreeBSD `outage` abort (lead's ruling: ours, fixed forward). Cause, proven: a test race, not a
  product bug. A lane counts (`connections`, `admitted`) on each end when that end's handshake
  completes, so voter two holding four connections does not mean voters one and three have
  admitted their sides; `outage` snapshotted `admitted` right then. Probe run on the old wait
  with diagnostics (CI run 43081866654, rerun): `admitted before the outage: 3 4 4`, all voters
  at 4 connections afterwards, rejected 0: voter one's last lane was admitted after the
  snapshot. Before that, old wait failed on main 905823da7ca4 (run 43067577357) and on this
  branch (43074178623, 43076028878); passed intermittently (43077059699 twice). Fix:
  `Cluster::fully_connected` waits for all three voters at four, used in `outage` and in
  `names-and-moves`, `system-localhost`, `dual-stack`, which checked right after a commit (ASan
  failed dual-stack's immediate check in run 43083388574, same race). The probe commits
  (consensus:07ef452, consensus:7513106) are reverted by consensus:ce1012e; squash them on landing.
- FreeBSD `dual-stack`: ok in run 43076028878 (log `dual-stack: ok (256 ms)`); FreeBSD defaults
  to v6-only, so this is the real check that `v6_only(false)` works there.
