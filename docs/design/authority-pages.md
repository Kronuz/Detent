> Status: DECIDED (2026-11-24), revised after the design review (F1-F11 at `Detent:6557660`; R1-R6 at `Detent:28ca4c3`) and the correctness review (C1-C8 at `Detent:28ca4c3`), `.campaign/detent/reviews/authority-pages-design-*.md`. A measured design record; the owner decided A4 as option 2 (PLAN D156) and Q1 as no signing now (D157). Every number cites the campaign job that produced it.

# Authority pages

Should the authority build its own trie and pages, so that authorities and relays both serve pages or records by the range asked for?

- Author: Germán Méndez Bravo. Created 2026-11-21.
- Base: exclusive seal rows at `Detent:bf55a09`; the first commit rows and the strict seal reruns at `Detent:07ac6b6`; the paced commit rows and the root-lag rows at `Detent:7a4d6f8` (`main` at `Detent:aaf8ce4` merged). The relay builder and the seals path are unchanged between them. Harness: [`bench/authority_pages/`](../../bench/authority_pages/) (`authority_pages_bench.cc`, `run.sh`, `check.py`). The design reviewer's probes (`coldpool`, `coalesce`, `satN`) are jobs `261123-145400-63b0f2` and `261123-145405-24d1dd`.
- Host: Intel Core i9-9980HK (8 cores, 16 threads, x86-64, no SHA-NI), macOS 15 (Darwin 24.6), APFS, release build. Each row is a strict or exclusive `quiet` hold, as its job says. Strict holds run beside other jobs' light holds and share the disk with them: strict build CPU reads 1.2 to 2.2 times the exclusive rows with identical bytes written, and the journal's `F_FULLFSYNC` latency switches between about 135 ms and 260-300 ms within one job. So CPU comes from the exclusive rows, and commit latency only from deltas between interleaved blocks of one job, read against an A/A band.
- Depends on [dense-distribution.md](dense-distribution.md): D1 (one root digest, two encodings), D4 (the receiver verifies), D6 (pages or records per update), D7 (what a relay builds and serves), Limit 1 and Q4; on PLAN.md D34 (its Q2-Q4 rulings), D133 (the build steps DD1-DD4) and D145 (follower reads).

## Abstract

Today the authority builds no pages. A relay builds them from the records it mirrors and puts its own root in each answer's frame header, so the root proves that agents agree with their relay, not with the authority (dense-distribution Limit 1). A deployment with no relay runs one on the authority's host (Q4, ruled (b)).

Four options were priced: (a) no pages (today); (b) the leader builds; (c) every voter builds; (d) the authority keeps only a records commitment, an incremental multiset hash, with no pages. A seal is the history's unit of 2,048 positioned changes (`src/detent_authority/state.hh:28`); rc3's write target, 2,000 transactions/s sustained and 10,000/s for 10 s (`docs/SCALE_TARGETS.md:117`), is 0.98 and 4.9 seals/s.

At 5M keys (10 namespaces of 500,000) a seal of 2,048 uniform changes costs the builder 604.8 ms of one core and rewrites 11.2 MB of pages (exclusive, `261122-210429-1ed7a8`; strict reruns 783-1,072 ms). The records commitment of (d) costs 96.7 ms and stays flat in the store's size. Built by four threads in one pass per namespace every 250 ms, at rc3's 2,000 changes/s the authority's root trails the head by p99 0.33 s, and 0.56 s through the 10,000/s burst (`261123-193520-a9b30c`, `261123-193523-d6ce25`); a new leader's first root covers everything 3.3-6.2 s after it starts (`261123-193528-1675da`, `261123-193525-e60802`). Beside the journal, a builder that builds once per seal leaves group commit p50 inside the A/A band of -1.7 to 1.3 ms, persisting its pages adds 37.1 ms (`261123-193517-519b4d`, exclusive), and the recommended pool moves it by ⟨PASS4-DELTA⟩.

*Recommendation:* (b) for the root only. The leader builds on a small pool of builder threads off the service loop, coalescing each namespace's changes into one pass per 250 ms, keeps its head generation in memory, and proposes each root it builds as a log entry, so every voter holds and serves it and it survives a leader change. Relays keep building and serving pages as today (Q4 (b) stands), forward the authority's root unchanged, and refuse to serve a page tree whose root differs. Agents publish at the relay's head as today and verify at the newest authority root in the same answer, so Limit 1 closes with a bounded detection delay and no visibility cost while roots arrive; an agent whose oldest unverified record passes the bound (15 s derived) stops publishing that namespace. (b) is an added cost on the leader, not a moved one: 1.30 cores of the leader at rc3's sustained rate and 1.72 through its burst, at 5M with four builder threads and 250 ms passes (`261123-193520-a9b30c`, `261123-193523-d6ce25`, strict), against 0.53-0.58 of a core building once per seal (`261123-193517-519b4d`, exclusive); the short passes buy the 0.33 s root lag with CPU.

## The question

The authority is a replicated state machine: a leader orders transactions, voters apply them, and every voter holds the same records at each position. Pages are a deterministic function of those records (dense-pages D9: the build is canonical), so any builder that applies the same records names the same root. Who builds decides only who can vouch for the root and who pays.

| Option | Who builds | What it stores | Root agents check |
| --- | --- | --- | --- |
| (a) none | relays | nothing | the relay's |
| (b) leader | the leader, on builder threads | its head generation | the authority's, from the log (below) |
| (c) every voter | each voter, on builder threads | each its head generation | the authority's; voters cross-check each other |
| (d) root only | no one on the authority | a 2 KiB multiset hash per namespace | a records digest, not the page root |

Option (d) is priced as LtHash's shape (1,024 16-bit lanes, each record expanded to 2 KiB by SHA-256 in counter mode): a change subtracts the old record and adds the new one, so the authority pays per change, never per store.

## Cost on the authority

Per seal of 2,048 uniform one-key updates, over the relay's own builder (`detent::relay::build_generation`, with D7.4's relocation once dead bytes pass live ones), writing and syncing each new segment with `F_FULLFSYNC`:

| Per seal | 100K (4 × 25,000) | 5M (10 × 500,000) |
| --- | --- | --- |
| Job | `261122-210428-976f07` (exclusive) | `261122-210429-1ed7a8` (exclusive); idle row `261123-073248-5fdf61` (strict) |
| Build CPU, p50 (min-max of 30) | 78.8 ms (63.4-93.2) | 604.8 ms (507.1-1,552.4) |
| Build wall with writes and sync, p50 | 165.7 ms, of which sync 88.3 ms | 1,024.3 ms, of which sync 427.8 ms |
| Pages written, p50 (mean) | 2.42 MB (2.40 MB); store 2.54 MB | 11.21 MB (19.03 MB); store 128.8 MB |
| Relocations | 56 of 120 namespace builds | 20 of 300, all ten namespaces in the same two seals |
| (d) records digest, p50, wall on one thread | 97.3 ms (85.9-106.4) | 96.7 ms (85.4-141.8) |
| Idle seal (16 changes): CPU, wall, written | 11.6 ms, 99.1 ms, 118 KB; (d) 1.0 ms (`261123-102616-53d93e`, strict) | 33.8 ms, 265.1 ms, 171 KB; (d) 1.8 ms |
| Head generation in memory | 11.6 MB heap, 55.4 MB peak RSS | 135.1 MB cold, 243.1 MB after 30 seals; 618.8 MB peak RSS |
| Cold build, one thread | 203.6 ms CPU, 300.1 ms wall | 9.00 s CPU, 12.04 s wall with writes |
| Strict reruns: build CPU p50, (d) p50 | 129.1 ms, 165.6 ms (`261123-103432-c20472`); 145.0 ms, 188.7 ms (`261123-143709-e27543`) | 783.1 ms, 117.6 ms (`261123-103435-b1e0ae`); 1,072.3 ms, 173.8 ms (`261123-143709-c59049`) |

The CPU figures are one exclusive job each, at `Detent:bf55a09`. The strict reruns, at `Detent:07ac6b6` and at the merged head (with the idle rows `261123-143710-993a49`, `261123-143708-866493`), write the same bytes, keep the ordering of (b) and (d), and read 1.3 to 1.8 times the exclusive build CPU beside light holds. Build CPU includes encoding the records body the builder reads (`Corpus::build` in the harness), work the authority's builder would not pay, so (b) is priced slightly high. At 100K a seal touches nearly every leaf and rewrites about 95% of the store, the worst case per byte; the 5M rows are the realistic one.

At rc3's sustained 0.98 seal/s, each cell 100K / 5M, from the per-seal rows (one build per seal):

| Option | Authority CPU | Memory | Disk written, if pages are persisted | Wire, authority to relay |
| --- | --- | --- | --- | --- |
| (a) | 0 | 0 | 0 | records, as today |
| (b) | 7.7% / 59.1% of a core, leader only (strict: 12.6-14.2% / 77-105%) | 11.6 MB / 135-243 MB | 2.34 / 18.6 MB/s; 0 in memory | one root entry per pass (below) |
| (c) | as (b) on each of 3 voters: 23% / 177% of a core in all | as (b), each | as (b), on each voter | as (b) |
| (d) | 9.5% / 9.4% of a core on each voter | 2 KiB per namespace | 0 | +2 KiB per namespace answer, or +32 B for its hash |

(b) grows with the store and (d) does not: (d) pays per change, (b) rewrites each touched leaf and its path, and every 15 seals or so all ten namespaces relocate together (128.8 MB in one seal), which is why the 5M mean is 19.0 MB.

**Coalescing and the pool.** A builder that is behind builds all of a namespace's pending changes in one pass, and a pass costs far less than the seals it covers: over 18,432 changes (9 seals) 2,694 ms of CPU, and over 102,400 (a 10 s burst) 4,523 ms, against 872 ms for one seal in the same job (`261123-145400-63b0f2`): 3.1 times one seal for 9, 5.2 times for 50. The ten namespaces build independently, so a pool of up to ten threads builds them in parallel. The root-lag rows drive that shape directly: changes arrive open-loop at rc3's rate, uniform over the 5M corpus, and each namespace takes one pass per interval over what arrived; a change's root lag is the time from its arrival to the root that covers it.

| 5M, uniform changes | Pool, pass interval | Root lag p50 / p99 / max | Builder CPU | Peak RSS | Job |
| --- | --- | --- | --- | --- | --- |
| 2,000/s for 30 s | 1 thread, 1,000 ms | 923 / 3,315 / 3,863 ms | 0.92 core | 467 MB | `261123-193520-a9b30c` |
| 2,000/s for 30 s | 10 threads, 1,000 ms | 626 / 1,152 / 1,409 ms | 1.16 cores | 1,880 MB | `261123-193520-a9b30c` |
| 2,000/s for 30 s | 4 threads, 250 ms | 160 / 328 / 502 ms | 1.30 cores | 1,000 MB | `261123-193520-a9b30c` |
| 2,000/s for 30 s | 10 threads, 250 ms | 180 / 396 / 548 ms | 2.00 cores | 1,883 MB | `261123-193520-a9b30c` |
| 2,000/s for 30 s | 10 threads, 250 ms | 160 / 295 / 410 ms | 1.33 cores | 1,953 MB | `261123-193531-962367` (exclusive) |
| 10,000/s from 10 s to 20 s, else 2,000/s, 40 s | 1 thread, 1,000 ms | 1,010 / 3,200 / 3,523 ms | 0.81 core | 528 MB | `261123-193523-d6ce25` |
| the same | 4 threads, 250 ms | 207 / 564 / 737 ms | 1.72 cores | 1,132 MB | `261123-193523-d6ce25` |
| the same | 10 threads, 250 ms | 228 / 550 / 662 ms | 2.43 cores | 1,939 MB | `261123-193523-d6ce25` |
| the same | 10 threads, 250 ms | 197 / 407 / 513 ms | 1.87 cores | 1,960 MB | `261123-193531-962367` (exclusive) |

Builder CPU is the pool threads' CPU over the run (the process's, arrival thread included, reads 0.01-0.15 more); peak RSS is the pool's cold build at setup. One thread passing once a second does not keep up at 5M: it is busy 0.92 of the run, and each round in which all ten namespaces relocate pushes the lag to 3-4 s. Four threads passing every 250 ms hold p99 at 0.33 s steady and 0.56 s across the burst (the burst's worst second: p99 670 ms). Ten threads hold the same lag (0.40 and 0.55 s in the same jobs) at 1.4-1.5 times the CPU and 1.7-1.9 times the peak RSS. Short passes cost CPU rather than saving it: in one job ten threads spent 2.00 cores at 250 ms against 1.16 at 1,000 ms, for p99 0.40 s against 1.15 s, and the exclusive per-seal build is 0.53-0.58 of a core at rc3's rate (`261123-193517-519b4d`). Coalescing pays where the builder falls behind (above); short passes buy root lag. Strict CPU reads 1.2-2.2 times exclusive (ten threads at 250 ms: 2.00 strict, 1.33 exclusive).

**Commit latency and elections.** The question is whether the builder's CPU, memory traffic and, for persisted pages, its syncs slow the journal's group commit. The harness runs one durable single-voter authority (100,000 keys, positions and history on, 64 producers committing groups of 64 one-change transactions). The first commit rows (`261123-102553-3c089f` to `261123-102613-0c4d8c`, four strict jobs per corpus) triggered one builder seal per authority seal, and this host's voter commits only 139-474 transactions/s, one `F_FULLFSYNC` per group: the builder took 0.09-0.23 seals/s, a fifth to a tenth of rc3's, and was busy 5-15% of the run. Their deltas do not price rc3. They did settle one arm: building inline in the service loop stalled it for 971, 1,104, 1,507 and 2,650 ms in the four 5M jobs, with p99 commit latency 1,020-2,303 ms against none's 234-490 ms, past the 1,000-2,000 ms election timeout (`consensus/types.h:114-115`); inline is ruled out.

The paced rows hold the builder at rc3's 0.98 seals/s by the clock, whatever the voter commits, so the builder carries rc3's load: 0.53-0.58 of a core at 5M in the exclusive job, 0.53-1.09 strict (`261123-193517-519b4d`, `261123-193507-f0eafa`, `261123-193510-65f196`). Each job interleaves blocks of 40 groups: none alternates with thread (one builder thread), pool (one build per namespace on ten threads), write (one thread that also writes and syncs its segments beside the journal), aa (a second none, the A/A) and, from ⟨PASS4-JOB⟩ on, pass4 (the recommended pool: four threads, a pass per namespace every 250 ms, over changes arriving at 0.98 × 2,048 = 2,007/s); the arms rotate by one position per round. Intervals are Student's t at 95% over the pairs (3 degrees of freedom for four). An arm's paired delta is its block's group commit p50 minus the mean of the two none blocks beside it, so a switch of the fsync regime moves both sides of a pair alike, and the aa arm's deltas give the band a real arm must clear.

| Group commit p50, arm minus adjacent none, ms: median; mean [95% t interval] of 4 pairs | thread | pool | write | aa (A/A) |
| --- | --- | --- | --- | --- |
| 5M, exclusive `261123-193517-519b4d` | -2.2; -2.0 [-3.3, -0.6] | -0.3; -0.2 [-1.8, 1.3] | +36.6; +37.1 [33.3, 40.8] | +0.1; -0.2 [-1.7, 1.3] |
| 5M, strict `261123-193507-f0eafa` | -9.6; -28.2 [-98.5, 42.1] | -2.0; -9.1 [-34.9, 16.8] | +39.9; +30.0 [-43.5, 103.6] | -2.9; -0.8 [-51.9, 50.3] |
| 5M, strict `261123-193510-65f196` | -2.1; -11.4 [-63.5, 40.8] | +0.4; +3.7 [-42.6, 50.0] | +40.2; +74.6 [-36.4, 185.7] | +5.8; +33.1 [-63.3, 129.6] |
| 100K, strict `261123-193513-95a0ae` | +3.1; -1.8 [-65.4, 61.8] | -10.8; -50.0 [-186.5, 86.5] | +10.5; +12.8 [-6.6, 32.2] | -70.6; -81.1 [-204.4, 42.3] |
| 5M exclusive: build CPU per seal; share of a core | 605.8 ms; 0.53-0.58 | 791.0 ms; 0.72-0.76 | 664.8 ms; 0.58-0.62 | 0 |
| 5M exclusive: worst service-loop gap | 242 ms | 176 ms | 206 ms | 195 ms (none: 376 ms over 16 blocks) |
| 5M exclusive: longest build of one seal (wall) | 707-780 ms | 197-204 ms | 1,706-1,905 ms | |
| 5M exclusive: backlog left at each block's end (drain) | 49-648 ms | 0 | 3,168-3,463 ms | |

In the exclusive job the none blocks read 133.4 ms (median of 16) and the A/A interval is -1.7 to 1.3 ms. The pool moves group commit p50 by -0.2 ms [-1.8, 1.3], inside it. One thread moves it by -2.0 ms [-3.3, -0.6], overlapping the band and faster, 1.5% of the p50: no cost. Writing and syncing pages beside the journal adds 37.1 ms [33.3, 40.8], 28%, clear of the band, and both strict jobs' medians agree (+39.9 and +40.2), though their intervals do not resolve it. The strict jobs' A/A intervals span 100-190 ms because the fsync regime switches between blocks; the 100K strict job resolves nothing, and its builder takes 0.09-0.16 of a core. Two quantities describe a builder that is behind, and the table keeps them apart: a seal's build time (wall, from its pickup to its end) and its lag (from its post to the end of its build, queueing behind earlier seals included). The rows above measured the first and the backlog left when the block ended; the write arm's builder does not keep up, since one seal with writes takes 1.7-1.9 s against a seal every 1.02 s, so its backlog grows through each block and 3.2-3.5 s of work remained at its end. Lag p50 and p99 per arm, from ⟨PASS4-JOB⟩: ⟨SEAL-LAG⟩. The clock posted seals at a median 0.91 seals/s per block (0.82-0.98) against the 0.98 asked, since each block rounds down the seals due; the write builder completed about 0.6 seals/s. The exclusive thread arm's 605.8 ms per seal reproduces the seal row's 604.8 ms at the merged head.

At burst the pool is saturated by design. The reviewer's probe ran 4 and 16 threads building 500,000-key seals back to back, the host's 16 hardware threads saturated, beside the same commit path (`261123-145405-24d1dd`): group p50 moved by -5 to +21 ms against the adjacent none blocks, p99 read 259-369 ms against none's 231-358, and no stall exceeded 480 ms against none's 924. A bounded pool turns excess load into root lag, never into commit latency.

## Where the root comes from

The root is worth something only if every path from the leader to the agent carries it unchanged. Under (b):

| Path | Where its root comes from | When it is missing or differs |
| --- | --- | --- |
| Leader | its builder: after a pass over namespace n reaches position p, the leader proposes a root entry {n, incarnation, p, root}, one entry per pass round, about 50 B per namespace | the entry is not committed: no root at p; the next pass names a later position |
| Follower (D145) | the committed root entries it applied, answered once its leader confirms the read index, like any other history | no root entry yet: the answer carries none, and agents below publish nothing new (rules below) |
| Relay, every tier | the root entries in the history it mirrors from upstream (namespace D7); it puts the authority's root, unchanged, in the header of any frame whose target is p, and never fills it from its own build | its own build at p differs: it still forwards the authority's root unchanged, refuses its pages there and serves records, counts the divergence, and rebuilds from a full copy; it never strips or replaces a root |
| Agent | the frame header: every answer carries the newest root entry r at or below the relay's head | by the agent rules below: no root, a root older than the last one it verified, or a failed check publishes nothing for that namespace |

**Positions without a root.** Roots exist only at the positions a pass reaches, one per namespace per interval, and an answer runs from the agent's position a to the relay's head h, which under steady writes is rarely rooted. So every answer carries the newest root entry r at or below the relay's head, and one that spans r splits into two frames, records a→r with the root of r, then r→h: the agent builds r, checks it, and publishes h. A relay builds at rooted positions, not at its head: r→h goes as records, so its build cadence follows the LAG table's pool rows (a pass per touched namespace per interval). Every agent is verified at a rooted position at least once per answer that crosses a new root, at the cost of one extra delta build, and its visible position stays the relay's head. A mis-applied record is caught at the first root after it. Positions between the old leader's last committed root and the new leader's first are such positions too; no answer needs a root at them.

**Agent and relay rules.** Each rule below closes a way a missing or stripped root would let an agent publish unverified state. A model of them, [`root_model.py`](../../bench/authority_pages/root_model.py) with [`test_root_model.py`](../../bench/authority_pages/test_root_model.py), passes with these rules. Each earlier rule fails some of its eleven cases: a relay that strips a root it disagrees with and an agent that publishes a rootless answer (`ROOT_MODEL_RULES=strip`, four); rule 3's clock run from the last check of a newer root (`last-check`, three) or from any passing check (`any-check`, two).

1. **A relay never strips a root.** It forwards the newest root entry it holds in every answer, whatever its own build says. On a mismatch it refuses its pages there, serves records, counts the divergence and rebuilds. Forwarding is never worse: if its records are right and its build is wrong, agents building the records pass; if its records are wrong, agents refuse them.
2. **An agent publishes only behind a checked root.** It keeps its state at its last verified root v and the records it published since. An answer whose root r is at or past v is checked by building r from that state. On a pass it publishes through r, and the tail past r as well. On a failure, on a missing root, or on a root older than v, it publishes nothing for that namespace. A new namespace or incarnation therefore stays invisible until its first root, which costs its first values the root lag (p99 0.33 s plus the entry's commit). The state at v shares pages with the published generation, so it costs the pages the tail touched plus the tail's records: at most the bound times the write rate, 30,000 records at 2,000/s. v, the tail and rule 3's clock persist with the published generation. An agent that restarts without them treats its whole published state past its last persisted root as unverified from the restart.
3. **Bounded time unverified.** Rule 3's clock is the age of the oldest record the agent published past its last verified root, and zero when it published none. It starts at the first unverified publish. It moves to the next oldest unverified record when a checked root covers the oldest, and it stops when a root covers them all. Re-checking the root it already holds never moves it, and idleness never runs it. While the clock is within the bound (below, 15 s derived), the agent publishes new tails. Past the bound, it publishes nothing new for that namespace, reports it unverified in health, and asks another source. Those asks are jittered over a second, made at most once per bound per namespace, and take records, not a full copy, unless a check failed. A builder stall, a relay that loses root entries, or an election whose window outruns the bound therefore freezes a namespace with writes after the bound, rather than leaving it unchecked.
4. **After a failed check.** The agent keeps what it already published: its position never regresses, so the root detects a wrong state, it does not retract it. It counts the failure, reports the namespace unverified, and takes a full copy from another source per dense-distribution D9 (another relay, else the authority's records). The full copy is checked against its root before it replaces the published state. While every source fails, the agent stays at its published state and retries.

**Rule 3's bound.** The bound must outlast the longest time a record stays unverified across an election, or every election freezes namespaces with writes. Its parts, measured at 5M on this host:

1. **The record's age when the leader fails**, at most the root lag plus its entry's commit: p99 0.33 s (`261123-193520-a9b30c`) plus 0.31 s (`261123-193517-519b4d`), 0.64 s.
2. **The election timeout**, 1-2 s (`consensus/types.h:114-115`).
3. **The window**, 2.1-6.2 s: 2.1-2.7 s on ten threads exclusive (`261123-193531-962367`), 3.3-6.2 s on four strict (`261123-193525-e60802`, `261123-193528-1675da`). It grows with the store and shrinks with the pool.
4. **The new root's commit**, up to 0.31 s.
5. **Delivery** through two relay tiers and the agent's poll, within SCALE_TARGETS' 1 s p99 publish-to-visible budget (`docs/SCALE_TARGETS.md:131`; not measured here).

Parts 1-4 sum to 4.1-9.2 s, and 5.1-10.2 s with delivery. A leader restart adds its own journal open, which is unmeasured, before the election. With a margin of 1.5 over the worst sum, the bound is 15 s. As configuration it is 1.5 × (root lag p99 + 2 × commit p99 + election timeout + the longest window the root-lag metric reported + 1 s), so it follows a deployment's store size and pool.

What a user sees under each trigger, at 15 s:

| Trigger | Who | What a user sees |
| --- | --- | --- |
| Builder stall on the leader, commits continue | every agent of every namespace with writes, fleet-wide | values keep updating unverified for 15 s after the first unverified write, then freeze, with health showing the namespace unverified, while the authority keeps accepting writes; asking another source cannot help, since no source has a root; when roots resume, each namespace catches up in one answer. Idle namespaces see nothing |
| A relay loses root entries | agents below it, namespaces with writes | the same freeze after 15 s for that relay's namespaces; another relay with roots restores them on the first ask |
| Election or leader restart, majority side | agents of namespaces with writes during the gap | values keep updating unverified until the new leader's first root, 5.1-10.2 s here, under the bound: no freeze. A store or a starved pool whose window passes about 11 s freezes at every election unless the bound follows it (the configured form) |
| Partition, leader in the minority | everyone | no commits, so nothing unverified and no alarm |
| New namespace or incarnation | its agents | invisible until its first root, p99 0.33 s plus the commit |

The owner decides the bound. The options:

1. 15 s as derived here, fixed.
2. The configured form above, defaulting to 15 s (recommended): the bound follows the measured window.
3. 10 s, as in round 2: freezes namespaces with writes at any election whose parts pass 10 s, 0.8 s of headroom in the strict four-thread case.
4. No rule 3: rule 2's unverified tail is unbounded, and a relay that loses root entries goes unnoticed (R5 open).

**The builder and the service loop.** The pool never touches the loop's state:

1. **Input.** For each change the loop applies, it appends {key, position, value} to the namespace's pending list, an O(1) append under the namespace's lock. A cold build reads a `persistent_map` snapshot of the state at the applied index c (`state.hh`), which costs O(1) to take and shares structure with the live state.
2. **Encode.** The pool's thread encodes the records body the builder consumes. At 5M the cold encode alone takes 0.6-1.8 s (`261123-193517-519b4d`, `261123-073248-5fdf61` `cold` lines), so it cannot run on the loop.
3. **Output.** A finished pass pushes {n, incarnation, p, root} onto a queue, tagged with the term it was built in. The loop drains the queue into the next transaction group it proposes, as one more entry of that group's batch, so root entries take no proposal and no sync of their own. A root of an older term is dropped. An idle authority has no pending changes and so no passes and no roots to propose. A group that fails to commit loses its root entries with it, and the next pass names a later position.
4. **Memory.** The pool holds each namespace's head generation (135-243 MB at 5M) and the pending lists, about 2,000 records/s times the root lag. A list is bounded at its namespace's live key count: past it, a pass costs more than a cold build, so the pool discards the list and cold-builds the namespace from a snapshot. A cold build adds its snapshot and the peak in the window table. The loop holds only the queue.
5. **Scheduling.** Passes take namespaces round-robin among those with pending changes, and the interval is a minimum per namespace, not a schedule. The design reviewer's light probe (`261124-105108-776fa1`) ran 5M keys over 10, 100 and 1,000 namespaces at about 1.9 cores each, with no worse lag at 1,000 (1,727 passes a second at 1.0 ms each).

**Why roots go through the log.** A root entry is about 62 B: a 6-byte namespace name, a 16-byte incarnation, an 8-byte position and a 32-byte root. At ten namespaces and four rounds a second that is 40 entries and 2.5 KB a second, about five entries in each 133 ms group, riding groups already in flight. ⟨ROOT-ENTRIES⟩ Without it, followers hold no root and answer relays without one (D145 lets them serve), and a new leader cannot tell which positions its predecessor rooted. Fetching roots from the leader at each follower read would add a forwarded round per answer and still lose them at an election.

**What the root proves.** The root is not signed. It catches a relay that follows the protocol and gets the state wrong: a bug in its mirror or its build, a mis-applied delta, damaged storage. It does not catch a relay that rewrites both the records and the header; that needs the authority to sign each root entry and agents to hold its key, which is not priced here (Q1 below).

## After a leader change

A new leader holds no pages: it cold-builds every namespace from its state, then passes once over what committed meanwhile, while commits continue. The cold build at 5M on one thread took 9.0 s of CPU in the exclusive row (`261122-210429-1ed7a8`, 12.0 s wall with writes) and 11.1-20.1 s of CPU in eleven strict samples (`261123-103435-b1e0ae`, `261123-073248-5fdf61`, `261123-103438-3b96b1`, `261123-143709-c59049`, `261123-143710-993a49` and the `cold` lines of the four 5M commit jobs). The reviewer's runs took 13.4 and 16.3 s wall on one thread, 8.6 s on two, 6.2 s on four, and 4.6 and 5.1 s on ten at 1.81-1.84 GB peak RSS against 0.43-0.49 GB on one (`261123-145400-63b0f2`).

The window rows run that sequence end to end at rc3's sustained rate: the pool cold-builds every namespace while changes arrive at 2,000/s, then each namespace passes once per interval. The window is the time until every namespace has a root past its cold build; the lag columns are what changes arriving in the window wait for their root.

| 5M, 2,000 changes/s, pool and interval | Cold build done | Window: first root past it | Lag p99 / max over the run | Peak RSS | Job |
| --- | --- | --- | --- | --- | --- |
| 1 thread, 1,000 ms | 12.7 s; 9.8 s | 15.1 s; 11.6 s | 13.8 / 15.1 s; 10.5 / 11.6 s | 550 MB; 692 MB | `261123-193525-e60802`; `261123-193528-1675da` |
| 4 threads, 250 ms | 5.9 s; 3.2 s | 6.2 s; 3.3 s | 5.1 / 6.2 s; 2.4 / 3.3 s | 995 MB; 1,185 MB | `261123-193525-e60802`; `261123-193528-1675da` |
| 10 threads, 1,000 ms | 3.8 s | 4.0 s | 3.5 / 4.0 s | 1,947 MB | `261123-193525-e60802` |
| 10 threads, 250 ms | 4.1 s; 3.1 s; 2.2 s | 4.3 s; 3.2 s; 2.3 s | 3.8 / 4.3 s; 2.8 / 3.2 s; 2.0 / 2.3 s | 1,895-2,001 MB | `261123-193525-e60802`; `261123-193528-1675da` twice |
| 10 threads, 250 ms, exclusive | 2.6 s; 2.0 s | 2.7 s; 2.1 s | 2.2 / 2.7 s; 1.8 / 2.1 s | 1,998 MB; 1,957 MB | `261123-193531-962367` twice |

The window is measured end to end, not summed: 2.1-2.7 s on ten threads in the exclusive job and 2.3-4.3 s strict, 3.3-6.2 s on four (strict), 11.6-15.1 s on one. The review's estimate of about 8 s (a 4.6-5.1 s strict pool build plus a 2.7 s coalesced pass) overstates it: the pool's cold build here took 2.0-2.6 s exclusive, and the first pass after it covers only what arrived meanwhile, so it takes a few hundred milliseconds. Once the window closes, lag returns to the steady rows'. Peak RSS during the cold build grows with the pool: 0.55-0.69 GB on one thread, 1.0-1.2 GB on four, 1.9-2.0 GB on ten.

Agents in that window have two possible rules:

1. **Wait for a root.** Every published state is verified, and Limit 1 is closed with no exception. Visibility then stalls for the window at every election and leader restart, and in steady state every update waits for its root as well, spending the steady root lag above out of the 1 s p99 publish-to-agent-visible target through two relay tiers (`docs/SCALE_TARGETS.md:132`).
2. **Publish, verify at the next root** (recommended), within agent rule 3's bound. Visibility is unchanged while roots arrive; answers in the window carry no new root, and the first answer after it verifies everything the agent applied since its last root. Limit 1 becomes "closed, with detection delayed by at most the root lag, the root entry's group commit (133 ms p50, 309 ms p99 in `261123-193517-519b4d`) and the agent's next answer": root lag p99 0.33 s and at most 0.50 s under rc3's sustained writes, p99 0.56 s and at most 0.74 s through its burst (four threads, 250 ms passes; `261123-193520-a9b30c`, `261123-193523-d6ce25`), and 3.3-6.2 s after a leader change (`261123-193528-1675da`, `261123-193525-e60802`). The lag rows run from a change's arrival at the builder to the end of its pass; the change's own commit precedes it.

The second costs no visibility while roots arrive within agent rule 3's bound, except for a new namespace or incarnation, which waits for its first root, and bounds how long a relay's wrong state can stand before every agent below it refuses it. The first is the right rule only if a deployment must never show an unverified value, which nothing in the goals asks.

## Lifecycle of option 2

Per namespace, after the design review's table. F is a follower, LC a leader cold-building, LS a leader passing, and LB a leader whose round outruns its interval.

| State | Changes commit at p | Pass | Root for p | Root entry commits | Leader change | Agent's check fails |
| --- | --- | --- | --- | --- | --- | --- |
| F | applies them; no pages | none | none | applies it into history and serves it under D145 | elected: becomes LC from a snapshot at its applied index c | not involved |
| LC | commits continue; changes after c queue for the first pass | none until the cold build ends | the cold build is the base, not a root; the first pass gives the first root, 2.1-6.2 s after election | an entry of the old leader's that commits in this term is valid: roots are of committed prefixes, so deterministic | steps down: cancels the cold build (below) | agents keep the old leader's last root and publish tails within the bound |
| LS | queued per namespace | one pass per namespace with pending changes, round-robin, interval a minimum | pushed to the loop's queue and ridden into the next transaction group | relays forward it; a relay checks its own build at p and never strips it | steps down: cancels the pool (below) | agent rules 2 and 4 |
| LB | coalesced into the next pass | the interval stretches to the pass time | as LS | as LS | as LS | as LS |
| any | | | | an entry not committed at a term change: no root at p, and the next pass names a later position | | its oldest unverified record passes the bound: agent rule 3 |

**Clocks.** Every timer in option 2, with the event that starts it, what resets it, what idles it, and the case that shows it:

| Clock | Starts | Reset or stopped by | When idle | Shown by |
| --- | --- | --- | --- | --- |
| Rule 3 (agent) | the first publish of a record past the last verified root | a checked root covering the oldest unverified record (the clock moves to the next; zero when none) | nothing unverified: zero | model: an idle namespace's first write publishes at once; a stalled builder with writes and a relay that loses roots trip at the bound; re-checking the held root resets nothing |
| Pass interval (leader, per namespace) | the start of the namespace's last pass | the next pass's start | no pending changes, no pass; the interval is a minimum, so an idle namespace's first change passes on arrival | model `pass_due_s`; the harness's `lag` rows |
| Step-down (leader) | the loop sees its role change | a new term as leader starts a fresh pool | a follower has no pool | model: a result of an old term is never proposed |
| Staleness (voter, relay, agent) | not a clock: entries order by (incarnation, position) | a newer entry replaces the held one | none | model: a root of an older incarnation is ignored; a new incarnation starts empty |
| Asking another source (agent) | rule 3's bound passed, or a failed check | a checked root | nothing unverified | rule 3's jitter and rate (text) |

**Step-down.** When the loop sees its role change, it sets the pool's cancel flag and drops the root queue. Workers check the flag between namespaces and drop the result of the pass or cold build in hand; a namespace's cold build at 5M is 1.0-2.6 s, its worst case past the step-down. Uncommitted root entries are lost like any uncommitted entry. The pool frees its heads, since a follower keeping one would apply every change: option (c)'s cost on that voter. The same process builds at most one cold build at a time, and a new term cancels the old one, so flapping elections cost at most one cold build of CPU per election and no stacked peaks. Roots stop meanwhile, and agent rule 3 bounds how long agents publish without one.

**The root entry.** Its contract, the history format the other three quests consume:

1. **Entry.** {n, incarnation, p, root}: namespace, the namespace's incarnation (`state.hh`), the position the root covers, and the 32-byte digest. It takes no position in the namespace's history, so it does not count toward the 2,048-change seal, roll-ups or seal boundaries.
2. **History.** Each voter keeps the latest root per namespace beside the namespace's head in its history. A snapshot carries it, and a cut keeps it, so a follower installed from a snapshot answers with it at once.
3. **Wire.** It rides the W6 frame header as D1's root field, with p as the frame's target. A relay mirrors the latest root with the history base it fetches and each newer entry with the records.
4. **Staleness.** An entry for an incarnation other than the namespace's current one, or at a position older than the root held, is ignored by voters, relays and agents.

## What it buys

| | (a) | (b) | (c) | (d) |
| --- | --- | --- | --- | --- |
| Limit 1: a relay that mis-applies records is caught | no | yes, at the next rooted position (above) | yes, and a nondeterministic voter is caught too | by agents taking record deltas; a cold agent taking pages would hash every record, about 119 s at 5M (23.8 µs per record without SHA-NI) |
| Relay-less agents get pages | Q4's host relay | Q4's host relay | Q4's host relay | Q4's host relay |
| A cold relay | builds from records | builds from records, checked against the authority's root | as (b) | builds |
| Who builds | every relay | every relay and the leader | every relay and every voter | every relay |

What only (b) and (c) buy is the authority's root. Relay-less pages are already bought by Q4 (b), and the pages a relay serves are unchanged.

**Pages from the authority, priced and not recommended.** For the leader to serve pages itself (one serving rule for authority and relays), it would keep D7.3's page window (bounded by 64 positions and the generation's own size: up to 128.8 MB at 5M) and D7.4's canonical full copy (another 128.8 MB, 25.7 MB as sent) on top of its 135-243 MB head, about 500 MB at 5M, plus a Merkle diff per page-delta answer and 25.7 MB of egress per cold relay or agent. Q4's host relay serves the same pages outside the consensus process today. So the authority serves records and roots; relays and the host relay serve pages, by D6's rule.

**A pass-through relay, priced and deferred.** With an authority root, a relay below a building one no longer has to build to be trusted. A pass-through relay mirrors records as today (D7.1), answers every delta as records (dense-distribution Q1 (c): the bytes of D6's choice except large clustered changes, where records are up to 1.10 times the pages), serves the full copy it fetched once from upstream (25.7 MB at 5M, within namespace D6's disk bound), and forwards roots. It saves the build, 604.8 ms of CPU per seal at 5M and the 135-243 MB head, and holds no page window, so it never serves page deltas. It is a later relay mode, not part of (b): (b) adds the leader's build and removes no relay's.

## Failure and recovery

| Event | (b) leader builds | (c) every voter builds | (d) |
| --- | --- | --- | --- |
| Follower restart, snapshot install, catch-up | nothing to rebuild; roots arrive in the log | cold build from the installed state | rehash every record once |
| Leader restart or change | the new leader cold-builds on the pool: the window above; committed roots persist in the log | none: the new leader has its head | none |
| Builder stalls or falls behind | roots stop advancing, commits continue, agents publish unverified tails until their oldest is the bound old, then stop publishing the namespace (agent rule 3); the root-lag metric alarms | the same on one voter; the others keep rooting | none |

The root is never on the commit path: the builder publishes the root of p after p commits.

## Recommendation

(b), the root only, in the shape above: the leader's builder pool (four threads, at most one per namespace, a pass per namespace every 250 ms, below the service loop's priority) keeps its head in memory and proposes each root as a log entry; relays forward roots and check their own; agents publish at the relay's head and verify at the newest root in the same answer. At 5M and rc3's 2,000 changes/s it spends 1.30 cores steady and 1.72 through the burst (strict, `261123-193520-a9b30c`, `261123-193523-d6ce25`; strict CPU reads 1.2-2.2 times exclusive), holds the root lag at p99 0.33 s steady and 0.56 s in the burst, roots everything 3.3-6.2 s after a leader change at 1.0-1.2 GB peak RSS (`261123-193525-e60802`, `261123-193528-1675da`), and moves group commit p50 by ⟨PASS4-DELTA⟩. Four threads, not ten: in the same jobs ten held the same lag (p99 0.40 s steady, 0.55 s in the burst) at 1.4-1.5 times the CPU and 1.7-1.9 times the peak RSS, and shortened the window by 0.1-1.9 s.

Against the alternatives:

1. **(a)**: costs the authority nothing and leaves Limit 1 open; a second relay's root is a cross-check nothing runs.
2. **(c)**: removes (b)'s window after a leader change and catches a nondeterministic voter, at three times (b)'s CPU and memory: 3.9-4.0 cores across three voters at rc3's rate with 250 ms passes (three times 1.30 strict, `261123-193520-a9b30c`, or 1.33 exclusive on ten threads, `261123-193531-962367`), each voter holding its own 135-243 MB head. Leader changes are rare, and under rules 2 and 3 a window that ends inside the bound (15 s derived) costs no visibility.
3. **(d)**: 9.4% of a core per voter at 5M, flat in the store's size, but a cold agent taking pages cannot check it (119 s of hashing at 5M without SHA-NI; SHA extensions or a faster XOF would cut that several times, unmeasured). It is the fallback if the build is ruled too costly for the authority; it closes Limit 1 only for agents taking record deltas.
4. **Inline in the service loop**: ruled out; it stalled the loop for up to 2,650 ms at 5M.
5. **Persisted pages on the authority**: ruled out. Writing and syncing pages beside the journal adds 37.1 ms to group commit p50 at 5M (95% t interval 33.3-40.8, against an A/A band of -1.7 to 1.3; 28% of none's 133.4 ms; `261123-193517-519b4d`, exclusive), both strict jobs' medians agree (+39.9 and +40.2, `261123-193507-f0eafa`, `261123-193510-65f196`), and its builder does not keep up at rc3's rate: one seal takes 1.7-1.9 s of wall against a seal every 1.02 s, and 3.2-3.5 s of backlog remained at each block's end. Pages in memory are simpler, and a restarted leader cold-builds anyway.

## What it changes

| Row | What it says now | What it becomes |
| --- | --- | --- |
| Limit 1 (dense-distribution) | the root is the builder relay's; a relay that mis-applies records serves a consistent wrong state | closed against a faulty relay at the next rooted position (the delay above), as detection, not retraction (agent rule 4); open against a relay that substitutes records and roots (Q1, D157) |
| D1 | "The sender is the relay that built the target"; an authority-computed digest rejected as built "inside the consensus process" | the root is the authority's, from the log, at rooted positions only; relays forward it unchanged; the rejected alternative becomes the design, built off the service loop at an interval rather than at every position |
| D4, last paragraph | steps 2 and 3 are the only defense against a wrong tree, the root being the sender's | step 5 now catches a tree that differs from the authority's; steps 2 and 3 still make a passing transfer the full build of its records |
| D6 | pages or records per update | unchanged; an answer that crosses a new rooted position splits into two frames there |
| D7 | relays build (7.2), keep a window (7.3) and a canonical copy (7.4) | unchanged, plus: a relay checks its root at each rooted position, serves records and refuses its pages where they differ, never strips a root, and builds at rooted positions instead of its head, so its window and copies carry roots |
| Q4 | ruled (b): a host relay; the authority serves records only; (c) "puts builds in the consensus process" | (b) stands for pages; the authority serves records and roots, and builds for the root |
| D34 | Q2 (b), Q3 (a), Q4 (b); reason "builds stay out of the consensus process" | Q2 and Q3 unchanged; Q4 as above; the reason becomes "builds stay off the consensus service loop": a pool in the process |
| D133 | dense-distribution's delivery; DD1-DD4 are quests, landed with relays that fill the root from their own build | the delivery stands; two landed pieces change: the relay's root fill (`src/detent_relay/namespace_relay.cc:414`, `:426`) and the agent's records check (`src/detent_delivery/coordinator.cc:683-692`), which then checks the authority's root and handles the split answer |
| D145 | followers answer history and sessions once the leader confirms their read index | unchanged; root entries are history, so follower answers carry them |

Quests it adds (none dropped):

1. **authority-root-entries** (high: a log entry kind and history format): the root entry's contract above, applied into each voter's history and mirrored by relays. Test: an entry round-trips through history, a roll-up and a snapshot install, and moves no seal boundary.
2. **authority-builder-pool** (high: concurrency): the relay's builder reused on the leader, the loop handoff and step-down above, a cold build on leader start, a root-lag metric and alarm. Tests: interleavings of apply, pass and proposal under TSan; step-down mid-pass and mid-cold-build proposes nothing after and frees the head; the paced pass4 row inside the A/A band.
3. **relay-authority-root** (high: wire): forward the newest root in every answer, check the relay's own build, refuse pages on a mismatch and never strip a root, split answers and build at rooted positions; changes `namespace_relay.cc`. Test: a relay whose mirror applies one record wrong still sends the authority's root.
4. **agent-root-check** (high: wire): agent rules 2-4, rule 3's clock and bound in health; changes `coordinator.cc`. Tests: a failed check publishes nothing and keeps the prior state; a repair from a second source; a relay that loses roots trips the bound; the cases of `test_root_model.py`.

## Limits

1. **One host, macOS, x86-64 without SHA-NI.** SHA-256 dominates the build and (d); a host with SHA extensions changes the absolute CPU, and Linux's fsync changes the sync column and the commit rows' regimes.
2. **Synthetic uniform updates.** Uniform keys are the worst case per change; clustered writes touch far fewer pages.
3. **A single voter that commits below rc3.** This host's durable voter commits one `F_FULLFSYNC` per 64-transaction group, 139-474 transactions/s, so the paced rows hold the builder, not the authority, at rc3's rate; the authority's own apply work at 2,000 transactions/s, three voters, the network and TLS are not in them.
4. **The builder reads its own corpus.** In the commit rows it builds a synthetic corpus, not the records the authority applied, so contention between the builder and apply or history over the same records is not modeled; the cold build reads synthetic records, not the authority's state.
5. **(d) is one construction.** It prices LtHash's shape with SHA-256, not BLAKE2X or another XOF.
6. **Root entries are simulated.** The harness's pass4r arm rides one 62-byte mutation per finished pass in the next group's last transaction. Those mutations take positions in the namespace, which the real entry does not, so the row slightly overstates their apply cost.
7. **The recommended pool beside the commit path is one exclusive job** (⟨PASS4-JOB⟩), with arrivals generated by the harness at rc3's rate rather than applied by the authority (Limits 4).

## Decision (A4 and Q1, decided by the owner on 2026-11-24)

**The decision.** Should the leader build pages to put the authority's root in every answer, and what do agents do while no root exists?

**Facts.** At 5M a seal of 2,048 changes costs the builder 604.8 ms of CPU (`261122-210429-1ed7a8`; 605.8 ms in `261123-193517-519b4d`) and (d) 96.7 ms. The recommended pool (four threads, a pass per namespace every 250 ms) spends 1.30 cores at rc3's sustained rate and 1.72 through its burst, with root lag p99 0.33 s and 0.56 s (`261123-193520-a9b30c`, `261123-193523-d6ce25`, strict). Beside the journal it moves group commit p50 by ⟨PASS4-DELTA⟩; a builder that builds once per seal on ten threads moves it by -0.2 ms [-1.8, 1.3] inside the A/A band of -1.7 to 1.3 (`261123-193517-519b4d`, exclusive); persisted pages add 37.1 ms. A new leader's first root covers everything 3.3-6.2 s after it starts on four threads (`261123-193525-e60802`, `261123-193528-1675da`, strict), 2.1-2.7 s on ten (`261123-193531-962367`, exclusive). Building inline in the service loop stalls it past the election timeout.

**Options.**

1. (a) none: today; Limit 1 stays open.
2. (b) the leader builds for the root; agents publish and verify at the next root (recommended): 1.3-1.7 cores and a 135-243 MB head on the leader (1.0-1.2 GB peak in a cold build), group commit p50 moved by ⟨PASS4-DELTA⟩, a relay's wrong state detected within p99 0.33 s (0.56 s in a burst, 3.3-6.2 s after a leader change), and four quests.
3. (b) with agents waiting for a root: Limit 1 closed without exception; every update pays the root lag in visibility, and every leader change stalls it for the window.
4. (c) every voter builds: no window, three times the CPU.
5. (d) a records commitment: 9.4% of a core per voter and no check for cold agents taking pages, unless a faster hash (SHA extensions or an XOF, unmeasured) makes hashing every record affordable.

**Decided (PLAN D156).** Option 2: the leader builds for the root on a pool of four builder threads off the service loop, one pass per namespace every 250 ms, pages in memory, not persisted; agents publish and verify at the next root. (d) is the fallback if the build proves too costly for the authority.

**Q1, decided (PLAN D157).** Should roots be signed, so Limit 1 closes against a relay that lies rather than one that errs? No, not now: the threat is faulty relays. Signing (the leader signs each root entry, agents hold the authority's key) is its own design when relays run outside the operator's trust.

