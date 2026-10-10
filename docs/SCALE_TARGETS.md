**Scale targets: 1M knobs, 10k publishers, 250k agents, sub-millisecond reads**

**Author:** [Germán Méndez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-10-10
**Status:** Requirements, for the implementing agent. Supersedes the planning workload in [SCALING.md](SCALING.md).

## Abstract

Detent's planning workload (500,000 knobs, 300 publishers, 50,000 agents) under-reads what it has to carry. The targets are:

| Dimension | Target | Previous planning number |
| --- | --- | --- |
| Knobs | **1,000,000** | 500,000 |
| Concurrent publishers | **10,000+** | 300 |
| Agents (hosts reading knobs) | **250,000+** | 50,000 |
| Final read latency, warm (application call to value) | **under 1 ms** | p99 under 10 µs, lookup only |
| Cold reads and startup | **Measured and reported** | Not measured |

The read path already meets the latency target for C++ (about **1.4 µs** per lookup in a 500,000-knob mapped image). Every other dimension needs design work. The biggest items:

- Relays become multi-tier.
- A stateless ingestion tier sits in front of the authority, which drops its hand-listed credentials.
- Local updates become incremental instead of rewriting the whole image.
- Non-C++ languages get the mapped reader.
- Readers stop re-validating a whole image on open.

Every number below that isn't marked as measured is an arithmetic projection.

## Ground rules

1. Detent is greenfield; the [library-boundary ground rules](LIBRARY_BOUNDARIES.md#ground-rules) apply. Change limits, formats and APIs freely when a target calls for it.
2. Keep 3 or 5 voters. Voters are the authority's storage servers; publishers are clients and agents read through relays, so neither ever votes, and more voters only slow writes. Do add **voter replacement** (a Raft membership change, or at least "replace voter N") so a dead server doesn't require exporting into a new group.
3. Every claim gets a reproducible benchmark in `benchmarks/` with its artifact (`*.json`): hardware, build flags, corpus, percentiles, and cold or warm. Tie fleet-level results to CPU cores and dollars.

## Where the system stands

### Knobs: 1,000,000

| Limit today | Value | Where |
| --- | ---: | --- |
| Authority live keys | 500,000 | `src/detent_authority/state.hh` |
| Local image records | 500,000 | Image admission limits |
| Local image size, string bytes | 256 MiB, 128 MiB | Image admission limits |
| Legacy JSON snapshot | 100,000 knobs, 16 MiB | `InitOptions::max_knobs`, `kDefaultMaxBytes` |

Measured at 500,000 knobs (`benchmarks/local-image-measurements.json`): a 14.2 MB wire snapshot and a 41.0 MB local image. Building the image takes 296–304 ms and validating it 84–93 ms. At 1,000,000 knobs that projects to about 28 MB on the wire, an 82 MB image, and about 0.6 s plus 0.2 s per complete rebuild.

Every update still rebuilds and rewrites the complete image on every agent; incremental pages are Stage 5C. At 250,000 agents, one knob change costs the fleet roughly 250,000 × 0.8 s ≈ **55 CPU-hours**. That's a projection, but it's why incremental, page-level publication is a requirement at this scale, not an optimization. Namespace subscriptions shrink it further: an agent should hold only what its applications read.

Required:

1. Raise authority, image and codec limits to 1,000,000 knobs with headroom (1.5M), and measure memory and checkpoint time at 1M.
2. Make a one-knob update cost proportional to the change on every tier: authority apply (already true), delta encode, relay cache and agent publication. Measure it at 1M.
3. Retire the 100,000-knob JSON path as a scaling path; it remains a bootstrap and admin format only.

### Publishers: 10,000+

| Limit today | Value |
| --- | --- |
| Producer sessions | 1,024 (`producers` in `state.hh`) |
| Credentials | Hand-listed in `authority.json`; changing them needs a restart |
| Outstanding transactions per producer | 1 |
| Producers grouped per Raft entry | 64, within 5 ms |
| Measured durable throughput | ~490 transactions/s: one voter, local disk, 64 grouped producers, no HTTP, TLS or replication |
| Retained results per producer | 256 (up to ~20 KiB per producer row) |

At 10,000 producers, the retained-result state alone projects to about 200 MB in every checkpoint and image. Replicated throughput has not been measured at all.

Required:

1. **Ingestion tier.** Stateless gateways terminate publisher TLS, authenticate, authorize namespaces, and batch into the authority. The authority sees hundreds of gateway sessions, not 10,000 publishers.
2. **Identity from policy, not a list.** Authenticate publishers by mTLS or a signed token, map them to namespaces through a policy, and reload without restarting. The authority keeps per-producer retry state only for what actually needs exactly-once results; reconsider the 256-result window at this scale.
3. **Throughput target.** Define and qualify a sustained and a burst rate. The proposed baseline is **2,000 transactions/s sustained and 10,000/s burst for 10 s**, across 3 replicated voters with TLS, with p99 commit latency reported. Revise it if real publishing patterns say otherwise.
4. **Namespace partitioning.** Multiple consensus groups, each owning a set of namespaces, as described in `SCALING.md`, for when one group can't meet the write target. Transactions stay single-namespace.

### Agents: 250,000+

Today agents connect directly to the authority. Relays (Stage 5B) have a design GO, and their qualification plan covers "at least 500 same-bundle subscribers".

At 250,000 agents and 500 to 5,000 sessions per relay, the fleet needs 50 to 500 edge relays. Cold bootstrap of 250,000 agents at about 28 MB each moves about **7 TB** before compression. A fleet-wide restart has to be paced, not absorbed.

Required:

1. **Multi-tier relays:** authority → regional relays → edge relays → agents. Relays may subscribe to relays. Measure sessions per relay and its CPU, memory and bytes at 1M knobs.
2. **Storm control:** jittered reconnects, bounded snapshot admission per relay, `source_behind`-style deferral, and snapshot sharing (one encoded snapshot serves every agent of a bundle).
3. **Propagation target:** p99 publish-to-agent-visible under **1 s** through two relay tiers in steady state, with the cold-start curve reported separately (time until 50%, 99% and 100% of agents are current).
4. **A fleet harness** that simulates 250,000 agents across enough hosts, connections and file descriptors to be real. A single-host loopback result does not qualify.

### Reads: under 1 ms warm, and cold measured

Measured today:

| Read path | Per read | Source |
| --- | ---: | --- |
| C++, managed mapped image, 500,000 knobs, random keys | ~1.4 µs | 100,000 lookups in 143–145 ms, 3 runs |
| C++, legacy shared memory, 4 threads | 27.6 ns | `detent_agent_shm_bench` |
| C++, in-process `detent::Client` | 149 ns | `detent_read_bench` |
| Shell, `detent get`, warm agent | 5.2 ± 0.4 ms | 300 runs, `hyperfine` |

All of the in-process numbers are loop averages from one Intel Mac; no percentiles exist. `detent get` is dominated by starting a process.

Required:

1. **Mapped reader for every language.** Bind `detent::local_client` in Python and Rust, so Python and Rust applications read the mapped image in-process. Document the image and descriptor as a stable reader ABI, so other languages can map it directly.
2. **No full validation on open.** A reader currently verifies the image checksum and validates every row before its first lookup: 84–93 ms of row validation at 500,000 knobs, projected near 200 ms at 1M plus the checksum. The agent validates once at publication, and readers trust its selection (ownership, link count, mode and lease checks) instead of re-validating. Keep bounds checks on every lookup, so a corrupt image fails safely rather than reading out of range.
3. **A sub-millisecond shell path.** Either a resident reader (a client library a long-running process loads once), or a `detent get` startup budget under 1 ms measured cold. Spawning a process is unlikely to reach that, so treat `detent get` as the script interface and make the libraries the latency interface.

## Measurement plan

Measure at 1,000,000 knobs, on Linux, macOS and FreeBSD, with release builds. Report p50, p99, p999 and max. Warm runs come after the first read with a populated page cache. Cold runs follow a page-cache flush (`sync; echo 3 > /proc/sys/vm/drop_caches` on Linux, `purge` on macOS) or a reboot.

| Measurement | Definition | Target |
| --- | --- | --- |
| Warm final read | Application call to returned value, mapped image, single and concurrent readers | p99 under 1 ms (expect µs) |
| Warm read during publication | The same, while the agent publishes a new generation every 100 ms | p99 under 1 ms |
| Cold first read, C++ | Process start to first value, existing generation on disk | Report |
| Cold first read, Python and Rust | The same, through the bindings | Report |
| `detent get` cold and warm | Wall time per invocation | Report; under 1 ms if a resident path exists |
| Library open | Open a generation and acquire the first view, before and after the open-validation change | Report |
| Agent restart | Process start to serving reads from the durable generation | Report |
| Agent cold bootstrap | Empty state to current, through the relay tiers | Report, with bytes moved |
| Relay restart | Start to serving its bundles | Report |
| Authority restart | Recovery to accepting transactions at 1M knobs and 10,000 producers | Report |
| Fleet cold start | 250,000 agents reconnecting at once | Curve to 50%, 99% and 100% current |

"Report" targets become numbers once the first measurement exists. Record each in `benchmarks/*.json` and summarize in this document.

## Next steps

1. Raise limits to 1M and measure the current pipeline at 1M knobs, to establish the baseline.
2. Remove full validation on open, and add the Python and Rust mapped readers. These are the read-latency items, and they're independent of the distribution work.
3. Finish Stage 5B relays with multi-tier support and the 250,000-agent harness.
4. Stage 5C incremental publication, measured as fleet CPU per one-knob change.
5. Ingestion tier and policy-based identity, then the replicated throughput qualification.
6. Voter replacement in `consensus`.

Update `SCALING.md`, the website's Benchmarks and Roadmap pages, and `README.md` as each target is measured.
