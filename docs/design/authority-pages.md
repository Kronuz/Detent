> Status: DRAFT (2026-11-23), a measured design record for an owner decision (A4, open). Every number cites the campaign job that produced it.

# Authority pages

Should the authority build its own trie and pages, so that authorities and relays both serve pages or records by the range asked for?

- Author: Germán Méndez Bravo. Created 2026-11-21.
- Base: exclusive seal rows at `Detent:bf55a09`; commit rows and strict seal reruns at `Detent:07ac6b6` (`main` at `Detent:1941ec8` merged). The relay builder and the seals path are unchanged between them. Harness: [`bench/authority_pages/`](../../bench/authority_pages/) (`authority_pages_bench.cc`, `run.sh`, `check.py`).
- Host: Intel Core i9-9980HK (8 cores, x86-64, no SHA-NI), macOS 15 (Darwin 24.6), APFS, release build; each row a strict or exclusive `quiet` hold, as its job says. Strict rows run beside other jobs' light holds: the strict reruns of the seal rows read 29-64% more build CPU than the exclusive ones with identical bytes written, so the exclusive rows give the absolute CPU and the strict ones confirm the ordering; the commit rows compare arms inside one job, which this does not bias.
- Depends on [dense-distribution.md](dense-distribution.md): D1 (one root digest, two encodings), D6 (pages or records per update), D7 (what a relay builds and serves), Limit 1, and Q4.

## Abstract

Today the authority builds no pages. A relay builds them from the records it mirrors, and an agent verifies a transfer against that relay's root digest. The root therefore proves that agents agree with their relay, not with the authority (dense-distribution Limit 1). A deployment with no relay runs one on the authority's host (Q4, ruled (b)).

Four options were priced: (a) no pages (today); (b) the leader builds; (c) every voter builds; (d) the authority keeps only a records commitment, an incremental multiset hash, with no pages. A seal is the authority's unit of positioned changes (2,048 transactions, `src/detent_authority/state.hh:28`); the rc3 target of 2,000 transactions/s sustained and 10,000/s burst (`docs/SCALE_TARGETS.md:117`) is about 1 seal/s and 5 seals/s.

At 100K keys a 2,048-change seal costs the builder 78.8 ms of one core (p50) and rewrites 2.42 MB of pages; the records commitment of (d) costs 97.3 ms, because it hashes 2 KiB per record twice (job `261122-210428-976f07`). At 5M the build costs 604.8 ms and rewrites 11.2 MB (19.0 MB with relocations), while (d) stays at 96.7 ms: (b) is 59% of a core at the sustained rate and 2.95 cores at the burst, beyond one builder thread; a cold build takes 9.0 s (job `261122-210429-1ed7a8`). On a durable authority a builder thread moves group commit p50 by -2.0 ms (-5.0 to +4.8) at 5M, inside the noise; writing pages costs +13.1 ms, and building inline stalls the service loop for up to 2.65 s, past the election timeout (four strict jobs per corpus, `261123-102555-afdabb` and the rows below).

*Recommendation:* (b) the leader builds, on a pool of builder threads off the service loop, with pages in memory and the root carried in the existing frame-header field. It closes Limit 1, and with relays passing pages through it moves the build that every relay pays today onto one voter instead of adding it. (d) is six times cheaper at 5M but cannot be checked by a cold agent taking pages; (c) removes a 9 s root lag per election for 1.2 more cores always on.

## The question

The authority is a replicated state machine: a leader orders transactions, voters apply them, and every voter holds the same records at each position. Pages are a deterministic function of those records (dense-pages D9: the build is canonical), so any builder that applies the same records names the same root. Who builds decides only who can vouch for the root and who pays.

| Option | Who builds | What it stores | Root agents check |
| --- | --- | --- | --- |
| (a) none | relays | nothing | the relay's |
| (b) leader | the leader, on a builder thread | its head generation | the authority's, in the frame header (D1) |
| (c) every voter | each voter, on a builder thread | each its head generation | the authority's; voters cross-check each other |
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
| (d) records digest CPU, p50 | 97.3 ms (85.9-106.4) | 96.7 ms (85.4-141.8) |
| Idle seal (16 changes): CPU, wall, written | 11.6 ms, 99.1 ms, 118 KB; (d) 1.0 ms (`261123-102616-53d93e`, strict) | 33.8 ms, 265.1 ms, 171 KB; (d) 1.8 ms |
| Head generation in memory | 11.6 MB heap, 55.4 MB peak RSS | 135.1 MB cold, 243.1 MB after 30 seals; 618.8 MB peak RSS |
| Cold build (restart, new leader), one thread | 203.6 ms CPU, 300.1 ms wall, 2.54 MB | 9.00 s CPU, 12.04 s wall, 128.8 MB |
| Strict rerun at head: build CPU p50, (d) p50, written | 129.1 ms, 165.6 ms, 2.42 MB (`261123-103432-c20472`) | 783.1 ms, 117.6 ms, 11.21 MB (`261123-103435-b1e0ae`); idle 28.1 ms (`261123-103438-3b96b1`) |

At 100K a 2,048-key uniform seal touches nearly every leaf, so each seal rewrites about 95% of the store; this is the worst case per byte, and the 5M rows give the realistic one.

At rc3's sustained 0.98 seal/s (2,000 / 2,048), each cell 100K / 5M:

| Option | Authority CPU | Memory | Disk written, if pages are persisted | Wire, authority to relay |
| --- | --- | --- | --- | --- |
| (a) | 0 | 0 | 0 | records, as today |
| (b) | 7.7% / 59.1% of a core, leader only | 11.6 MB / 135-243 MB | 2.34 MB/s / 18.6 MB/s; 0 in memory | +32 B per answer (the D1 root field) |
| (c) | as (b) on each of 3 voters: 23% / 177% of a core in all | as (b), each | as (b), on each voter | as (b) |
| (d) | 9.5% / 9.4% of a core on each voter, so a leader change costs nothing | 2 KiB per namespace | 0 | +2 KiB per namespace answer, or +32 B for its hash |

(b) grows with the store and (d) does not: (d) pays per change (two 2 KiB expansions), (b) rewrites each touched leaf and its path. At 5M a uniform seal rewrites 11.2 MB of the 128.8 MB store, and every 15 seals or so all ten namespaces relocate together (128.8 MB in one seal), which is why the mean is 19.0 MB.

At the 10,000/s burst, 4.9 seals/s, (b) needs 38% of a core at 100K and 2.95 cores at 5M. One builder thread builds 1.65 seals/s at 5M (3,390 transactions/s), so a 10 s burst leaves it 32 seals behind: the root of the burst's last seal comes 19.5 s after that seal commits, and the backlog drains in about 48 s at the sustained rate. The ten namespaces build independently, so a pool of builder threads divides this by up to ten (not measured here). The lag delays roots, never commits (below).

**Commit latency and elections.** The commit rows run four arms on one durable single-voter authority (100,000 keys, positions and history on, 64 producers committing 160 groups of one-change transactions per arm, 5 authority seals): none, a builder thread, a builder thread that writes and syncs, and the build inline in the service loop. Each authority seal triggers one builder seal of 2,048 changes over the 100K or 5M builder corpus. A row is four strict jobs, one per rotation of the arm order (each arm once in each position, so warm-up and neighbours fall on every arm alike); `check.py combine` gives each arm's group commit p50 as the median over the four jobs, and its paired delta against the none run of the same job. They are read against the consensus heartbeat of 100 ms and election timeout of 1,000-2,000 ms (`consensus/types.h:114-115`).

| Arm, 4 rotations | 100K builder corpus | 5M builder corpus |
| --- | --- | --- |
| Jobs | `261123-102553-3c089f`, `-102558-6f96c6`, `-102605-a63417`, `-102610-fdd616` | `261123-102555-afdabb`, `-102602-d23ae1`, `-102608-7ae2b2`, `-102613-0c4d8c` |
| none: group p50 (min-max), p99, worst gap | 137.3 ms (130.4-430.6), 261.5 ms, 1,300.7 ms | 145.0 ms (134.6-268.2), 375.6 ms, 1,291.6 ms |
| thread: paired delta p50 (min-max), p99, worst gap, root lag max | -1.9 ms (-143.4 to +9.4), 349.8 ms, 418.2 ms, 278 ms | -2.0 ms (-5.0 to +4.8), 351.8 ms, 1,499.0 ms, 1,991 ms |
| write: same | +2.0 ms (-135.9 to +7.4), 332.7 ms, 663.2 ms, 762 ms | +13.1 ms (-2.1 to +154.9), 476.9 ms, 1,199.1 ms, 3,042 ms |
| inline: same | -0.2 ms (-132.4 to +3.8), 554.5 ms, 1,624.8 ms, 0 | -1.9 ms (-17.2 to +159.4), 1,215.7 ms, **2,650.4 ms**, 0 |

The p50 is the median of the four jobs' per-arm group commit p50 (64 one-change transactions per group); the delta is each arm's p50 minus the none run of the same job, so a slow job (the first 100K rotation, where none read 430.6 ms during warm-up) moves every arm alike and drops out. The noise floor is the none arm's own spread and the deltas' ranges.

Read against the 100 ms heartbeat and the 1,000-2,000 ms election timeout:

1. **thread**: no measurable effect on commits. Its paired delta is -2.0 ms (-5.0 to +4.8) at 5M, inside the floor, and its worst gap stays within none's. Roots trail commits by at most 2.0 s at 5M (the builder's queue, at this harness's 0.2 seals/s).
2. **write**: persisting pages beside the journal costs commits: +13.1 ms median at 5M, up to +154.9 ms, and the root lags up to 3.0 s, because the builder's `F_FULLFSYNC` of 11-19 MB per seal shares the disk with the journal's syncs. Pages stay in memory.
3. **inline**: ruled out. At 5M it stops the service loop for up to 2,650.4 ms, past the longest election timeout, and its p99 is 1,215.7 ms against none's 375.6 ms. At 100K it is harmless in p50 but its worst gap, 1,624.8 ms, already reaches the timeout range.

The harness drives storage synchronously inside its service loop, so the none arm's gap includes each fsync; the deltas between arms are the measure, not the gaps themselves.

## What it buys

| | (a) | (b) | (c) | (d) |
| --- | --- | --- | --- | --- |
| Limit 1: a relay that mis-applies records is caught | no | yes, by every agent, at no agent cost: the root it already computes must equal the authority's | yes, and a nondeterministic voter is caught too | by the relay itself and by agents taking record deltas; a cold agent taking pages would hash every record once to check it, about 119 s at 5M (23.8 µs per record), so in practice not |
| Relay-less agents get pages | through Q4's host relay | from the leader, or from the host relay | from any voter | no |
| Boot: a cold relay | builds from records | takes the leader's canonical full copy, no build | from any voter | builds |
| Boot: a cold agent with no relay | Q4's relay | same transfer, 25.7 MB, no build | same | records and a build |
| One serving rule for authority and relays (D6) | no | yes | yes | no |

Relay-less pages are already bought by Q4 (b): the host relay builds the same pages at the same machine cost as (b), outside the consensus process. What only (b) and (c) buy is the authority's root, and a relay that can pass pages through instead of building them.

A pass-through relay still mirrors records to answer D6's record deltas (dense-distribution D7.2), so it skips the build, not the records stream.

The build is paid somewhere in every option but (d). Under (a) each relay, Q4's host relay included, pays 604.8 ms of CPU per seal at 5M; under (b) the leader pays it once and every relay that passes pages through pays nothing. With at least one pass-through relay, (b) moves the 5M cost onto the authority rather than adding it.

## Failure and recovery

| Event | (b) leader builds | (c) every voter builds | (d) |
| --- | --- | --- | --- |
| Voter restart | nothing on followers; a restarted leader cold-builds | cold build, or reload persisted pages | rehash every record once |
| Snapshot install, catch-up | nothing on followers | cold build from the installed state | rehash once |
| Leader change | the new leader cold-builds; roots lag by one cold build, commits do not | none: the new leader has its head | none |

The root is never on the commit path. The builder publishes the root of position *p* after *p* commits, so an answer for a position whose root is not ready waits for it or goes out as records with the root following. A cold build takes 203.6 ms of CPU at 100K and 9.00 s at 5M on one thread (12.04 s with writes and sync). Under (b) a new leader's roots therefore lag its election by about 9 s at 5M; answers in that window go out as records, which a relay applies and an agent verifies against the next root. (c) removes that lag for three times (b)'s sustained CPU, 1.77 cores against 0.59 at 5M; elections are rare enough that 9 s of late roots per election does not pay for 1.2 cores always on, and a builder pool shortens the cold build the same way it shortens a burst.

## Recommendation

(b): the leader builds on builder threads, keeps its head generation in memory, and fills D1's root field in the answers it gives relays. Agents then check the authority's root at no cost beyond the hashing they already do. The commit rows confirm the shape: a builder thread leaves group commit p50 inside the noise floor (-2.0 ms, -5.0 to +4.8, at 5M), writing pages on the authority's disk does not (+13.1 ms, up to +154.9), and an inline build stalls the loop for 2.65 s.

Against the alternatives:

1. **(a)**: costs the authority nothing and leaves Limit 1 open; every relay, Q4's host relay included, pays the build (604.8 ms per seal at 5M). A second relay's root is a cross-check nothing runs.
2. **(c)**: closes Limit 1 and removes (b)'s root lag after an election (9.0 s of cold build at 5M), at three times (b)'s CPU and memory: 1.77 cores and 3 × 135-243 MB at 5M. Elections are rare; 1.2 cores always on does not pay for 9 s of late roots each.
3. **(d)**: 9.4% of a core per voter at 5M against (b)'s 59%, flat in the store's size, but a cold agent taking pages cannot check them (hashing every record, about 119 s at 5M). It is the right fallback if the owner rules the build too costly for the authority; it closes Limit 1 only for agents taking record deltas.
4. **Inline in the service loop**: ruled out. At 5M it stopped the service loop for 2,650.4 ms, past the 2,000 ms election timeout.
5. **Persisted pages**: ruled out on the authority. Syncing 11-19 MB of segments per seal costs +13.1 ms median commit latency at 5M (up to +154.9 ms); a restarted leader cold-builds instead (9.0 s).

Burst sizing: one builder thread builds 1.65 seals/s at 5M (3,390 transactions/s), under the 4.9 seals/s burst. The ten namespaces build independently, so (b) runs one build per namespace on a small pool, and a builder that is behind may build several seals' changes in one pass; both are the implementation quest's to measure.

What it changes, if accepted:

| Row | Becomes |
| --- | --- |
| Limit 1 | closed: the root in every answer is the authority's |
| Q4 | (b) stands for page serving; the authority adds roots, not a page server, and the host relay may pass pages through |
| D6 | unchanged: pages or records per update, now from relays that may pass through |
| D33, D34 | unchanged: builds stay out of the consensus loop, on builder threads |

Quests it would add: an authority builder pool and root publication (the relay's builder, reused, one build per namespace, with a root-lag metric); a relay pass-through mode that serves the authority's pages; and the agent's check of the authority's root. None dropped.

## Limits

1. **One host, macOS, x86-64 without SHA-NI.** SHA-256 dominates both the build and (d); a host with SHA extensions changes the absolute CPU, and Linux's fsync changes the sync column.
2. **Synthetic uniform updates.** Uniform keys are the worst case per change; clustered writes touch far fewer pages (dense-distribution's table).
3. **Single voter.** The commit rows run one durable voter; network and TLS are not in them.
4. **(d) is one construction.** It prices LtHash's shape with SHA-256, not BLAKE2X or another XOF.
5. **A silent failure under (b):** if the builder thread stalls, roots stop advancing while commits continue. Nothing here alarms on root lag; an implementation needs a lag metric.

## Decision request (A4, the owner's)

**Facts.** A seal costs the builder 78.8 ms of one core at 100K and 604.8 ms at 5M (59% of a core at the sustained rate, 2.95 cores at the burst); the records commitment costs 97.3 and 96.7 ms. A new leader's cold build takes 9.0 s at 5M. Relay-less pages already come from Q4's host relay; only an authority-built root closes Limit 1 for every agent. A builder thread moves group commit p50 by -2.0 ms (-5.0 to +4.8) at 5M; persisted pages add +13.1 ms, and an inline build stalls the service loop for 2.65 s.

**Options.** (a) none; (b) the leader builds; (c) every voter builds; (d) a records commitment only.

**Recommendation.** (b) on a builder pool, with (d) the fallback if the build is ruled too costly for the authority.

**What deferring costs.** Nothing on the machines: (a) is today's state. It keeps Limit 1 open, so a relay that mis-applies records serves a consistent wrong state to every agent below it, and it keeps every cold relay building.
