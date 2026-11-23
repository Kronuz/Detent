> Status: DRAFT (2026-11-22), a measured design record for an owner decision (A4, open). Every number cites the campaign job that produced it; cells marked *pending* wait on the named job.

# Authority pages

Should the authority build its own trie and pages, so that authorities and relays both serve pages or records by the range asked for?

- Author: Germán Méndez Bravo. Created 2026-11-21.
- Base: `Detent:bf55a09` (`src/` identical to `main`). Harness: [`bench/authority_pages/`](../../bench/authority_pages/) (`authority_pages_bench.cc`, `run.sh`, `check.py`).
- Host: Intel Core i9-9980HK (8 cores, x86-64, no SHA-NI), macOS 15 (Darwin 24.6), APFS, release build; every row an exclusive `quiet` hold.
- Depends on [dense-distribution.md](dense-distribution.md): D1 (one root digest, two encodings), D6 (pages or records per update), D7 (what a relay builds and serves), Limit 1, and Q4.

## Abstract

Today the authority builds no pages. A relay builds them from the records it mirrors, and an agent verifies a transfer against that relay's root digest. The root therefore proves that agents agree with their relay, not with the authority (dense-distribution Limit 1). A deployment with no relay runs one on the authority's host (Q4, ruled (b)).

Four options were priced: (a) no pages (today); (b) the leader builds; (c) every voter builds; (d) the authority keeps only a records commitment, an incremental multiset hash, with no pages. A seal is the authority's unit of positioned changes (2,048 transactions, `src/detent_authority/state.hh:28`); the rc3 target of 2,000 transactions/s sustained and 10,000/s burst (`docs/SCALE_TARGETS.md:117`) is about 1 seal/s and 5 seals/s.

At 100K keys a 2,048-change seal costs the builder 78.8 ms of one core (p50) and rewrites 2.42 MB of pages; the records commitment of (d) costs more, 97.3 ms, because it hashes 2 KiB per record twice (job `261122-210428-976f07`). The 5M rows are *pending*.

*Recommendation (provisional until the 5M rows land):* (b) the leader builds, off the service loop, with pages in memory and the root carried in the existing frame-header field. It closes Limit 1 for one core-fraction on one voter, keeps leader changes off the commit path, and leaves Q4's host relay as the page server. (d) is dominated at 100K: it costs more CPU than (b) and does not let a cold agent check what it receives.

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
| Build CPU, p50 (min-max of 30) | 78.8 ms (63.4-93.2) | *pending `210429-1ed7a8`* |
| Build wall with writes and sync, p50 | 165.7 ms, of which sync 88.3 ms | *pending* |
| Pages written, p50 | 2.42 MB (store 2.54 MB) | *pending* |
| Relocations | 56 of 120 namespace builds | *pending* |
| (d) records digest CPU, p50 | 97.3 ms (85.9-106.4) | *pending* |
| Idle seal (16 changes) | not run | *pending `210431-3080d5`* |
| Head generation in memory | 11.6 MB heap, 55.4 MB peak RSS | *pending* |
| Cold build (restart, new leader) | 203.6 ms CPU, 2.54 MB | *pending* |

At 100K a 2,048-key uniform seal touches nearly every leaf, so each seal rewrites about 95% of the store; this is the worst case per byte, and the 5M rows give the realistic one.

At rc3's sustained 0.98 seal/s (2,000 / 2,048) and 100K keys:

| Option | Authority CPU | Memory | Disk written | Wire, authority to relay |
| --- | --- | --- | --- | --- |
| (a) | 0 | 0 | 0 | records, as today |
| (b) | 7.7% of a core, leader only | 11.6 MB | 0 in memory; 2.36 MB/s if pages are persisted | +32 B per answer (the D1 root field) |
| (c) | 7.7% of a core on each of 3 voters | 11.6 MB each | as (b), on each voter | as (b) |
| (d) | 9.5% of a core on each voter, so a leader change costs nothing | 2 KiB per namespace | 0 | +2 KiB per namespace answer, or +32 B for its hash |

At the burst, 4.9 seals/s, (b) is 38% of a core for its 10 s; the builder thread falls behind and catches up, which delays roots, never commits (below).

**Commit latency and elections.** The commit rows interleave four arms on one durable single-voter authority (100,000 keys, positions and history on): none, a builder thread, a builder thread that writes and syncs, and the build inline in the service loop. They report group commit p50/p99 and the longest gap between two service calls, against the consensus heartbeat of 100 ms and election timeout of 1,000-2,000 ms (`consensus/types.h:114-115`).

| Arm, 3 rounds each | 100K builder corpus | 5M builder corpus |
| --- | --- | --- |
| none: group p50 / p99, worst gap | *pending `210432-93640e`* | *pending `210434-c73864`* |
| thread | *pending* | *pending* |
| write | *pending* | *pending* |
| inline | *pending* | *pending* |

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

## Failure and recovery

| Event | (b) leader builds | (c) every voter builds | (d) |
| --- | --- | --- | --- |
| Voter restart | nothing on followers; a restarted leader cold-builds | cold build, or reload persisted pages | rehash every record once |
| Snapshot install, catch-up | nothing on followers | cold build from the installed state | rehash once |
| Leader change | the new leader cold-builds; roots lag by one cold build, commits do not | none: the new leader has its head | none |

The root is never on the commit path. The builder publishes the root of position *p* after *p* commits, so an answer for a position whose root is not ready waits for it or goes out as records with the root following. A cold build at 100K takes 203.6 ms of CPU, under the 1,000 ms election timeout; the 5M cold build is *pending* and decides whether (b)'s leader-change lag is acceptable or (c) is needed.

## Recommendation

(b), provisionally: the leader builds on a builder thread, keeps its head generation in memory, and fills D1's root field in the answers it gives relays. Agents then check the authority's root at no cost beyond the hashing they already do.

Against the alternatives:

1. **(a)**: costs nothing and leaves Limit 1 open. A second relay's root is a cross-check nothing runs.
2. **(c)**: closes Limit 1 and removes the leader-change lag, at three times (b)'s CPU and memory. It pays only if the 5M cold build is long enough that a lag after each election matters.
3. **(d)**: costs more CPU than (b) at 100K (97.3 against 78.8 ms per seal) and cannot be checked by a cold agent. A faster expansion (an XOF instead of SHA-256 counter mode) would change the CPU row, not the second point.

What it changes, if accepted:

| Row | Becomes |
| --- | --- |
| Limit 1 | closed: the root in every answer is the authority's |
| Q4 | (b) stands for page serving; the authority adds roots, not a page server |
| D6 | unchanged: pages or records per update, now from relays that may pass through |
| D33, D34 | unchanged: builds stay out of the consensus loop, on a builder thread |

Quests it would add: an authority builder thread and root publication (the relay's builder, reused), and the agent's check of the authority's root. None dropped.

## Limits

1. **One host, macOS, x86-64 without SHA-NI.** SHA-256 dominates both the build and (d); a host with SHA extensions changes the absolute CPU, and Linux's fsync changes the sync column.
2. **Synthetic uniform updates.** Uniform keys are the worst case per change; clustered writes touch far fewer pages (dense-distribution's table).
3. **Single voter.** The commit rows run one durable voter; network and TLS are not in them.
4. **(d) is one construction.** It prices LtHash's shape with SHA-256, not BLAKE2X or another XOF.
5. **A silent failure under (b):** if the builder thread stalls, roots stop advancing while commits continue. Nothing here alarms on root lag; an implementation needs a lag metric.

## Decision request (A4, the owner's)

**Facts.** At 100K a seal costs the builder 78.8 ms of one core and 2.42 MB of writes; the records commitment costs 97.3 ms. Relay-less pages already come from Q4's host relay; only an authority-built root closes Limit 1. The 5M rows are *pending*.

**Options.** (a) none; (b) the leader builds; (c) every voter builds; (d) a records commitment only.

**Recommendation.** (b), confirmed or revised by the 5M rows.

**What deferring costs.** Nothing on the machines: (a) is today's state. It keeps Limit 1 open, so a relay that mis-applies records serves a consistent wrong state to every agent below it, and it keeps every cold relay building.
