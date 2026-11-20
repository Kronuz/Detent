# Dense distribution: a cold agent from a warm relay, pages against records

Status: 100K measured on the pre-review-fix head (two jobs, three rounds each). 5M is held for the
head with the review fixes (F1, F2, F4/D137), which move the relay to serving pages from DD2's
canonical copy.

Verdict at 100K: pages beat records, but not greatly. Time to ready is 1.4-1.5x shorter, agent CPU is
46% lower, peak memory is about 35% lower, and the wire is 10-16% smaller. Bytes hashed are equal.
Pages win by skipping the build (build, pack, validate), about 0.3 s of agent work at 100K. A 100K
boot takes half a second either way, and most of it is fixed cost that both arms pay.

## Method

- Harness: `dense-distribution-boot.py`, driven in a job by `dense-distribution-row.sh` after the
  build step `dense-distribution-bins.sh`. One authority restores the fixture (`cp -c` clones of
  `capacity-trials/100000-main`: 100,000 records in 4 namespaces, seeded at Detent:ea1fddc). One relay
  mirrors it. Once the relay reports its mirror ready, a priming pages agent boots (the relay builds
  its pages lazily, on the first pages request), then a priming records agent. Then come the
  measured agents, one at a time, each a fresh process with an empty store, in the order P R R P P R.
  All of them use that one relay.
- The arms are the same build. The pages agent is the head's `detent-agent`. The records agent is the
  head with its opens' `out.pages = true;` set to false by `dense-distribution-bins.sh`, so it never asks for
  pages. The harness aborts unless every pages agent takes page transfers and hashes 0 bytes to
  build, and every records agent takes no transfer and builds.
- Ready means the agent's `selected` table holds every namespace at the authority's
  `namespace_revision`. CPU, disk bytes written and peak physical footprint come from
  `proc_pid_rusage` at ready. Socket bytes come from `nettop`. Bytes hashed and transfers come from
  the agent's log.
- The profile comes from one more agent per arm, outside the timed rounds. It is stopped about 13 ms
  after launch, `sample` attaches at 1 ms, and then it resumes. `dense-distribution-profile.py`
  splits the busy samples (idle waits excluded, blocking syscalls included) by the enclosing phase.
- Host: i9-9980HK, macOS 15.8.1, release build with clang, `quiet strict`.

## Results, 100K

Jobs: 261120-122719-955a5e (Detent:673f1d5f) and 261120-123557-6e3e90 (Detent:57bf4be1). Both trees'
`src/` equals Detent:eab7477c, the pre-review-fix head. Each cell lists the three measured agents.

| Per cold agent | Pages | Records | Pages vs records |
| --- | --- | --- | --- |
| Ready, job 1 (s) | 0.588 / 0.592 / 0.579 | 0.935 / 0.684 / 0.836 | median 0.588 vs 0.836: 1.42x faster |
| Ready, job 2 (s) | 0.476 / 0.480 / 0.576 | 0.727 / 0.782 / 0.627 | median 0.480 vs 0.727: 1.51x faster |
| Agent CPU, job 2 (s) | 0.256 / 0.264 / 0.282 | 0.490 / 0.506 / 0.395 | median 46% less |
| Bytes in (socket) | 185,932 | 205,706 to 222,038 | 10-16% fewer |
| Bytes hashed | 5,032,572 (0 to build, 4,853,237 to check) | 5,052,471 (4,851,757 to build) | equal |
| Disk bytes written | 2,560,000 to 2,568,192 | 2,568,192 | equal |
| Peak footprint (MB) | 13.5 / 13.6 / 14.1 | 21.1 / 21.6 / 18.8 | about 35% less |
| Relay CPU per agent (s) | 0.129 / 0.140 / 0.157 | 0.220 / 0.242 / 0.181 | about 35% less |

- First pages agent: the relay builds its pages for it. That agent took 1.29 s and 1.55 s to ready,
  with 0.72 s and 0.84 s of relay CPU. The first records agent took 1.34 s and 1.55 s.
- Each pages agent received 4 transfers of 2,470,101 raw bytes, which arrive as about 186 KB on the
  wire.

### Where the agent's time goes (busy samples, about 1.2 ms each, job 2's profiled agents)

| Phase | Pages | Records |
| --- | --- | --- |
| Build (trie, leaf bodies) | 0 | 113 |
| Pack | 0 | 117 |
| Validate own build | 0 | 53 |
| Receive and verify the transfer | below the profile's resolution | n/a |
| Publish (writes, syncs, links) | 17 (108 in a local run) | 107 |
| Reactor, TLS, decode, status | 58 | 72 |
| Pack scrub (after ready) | 53 | 56 |
| Total busy | 128 | 518 |

By leaf, records spend 228 samples in detent code, 73 in memory, 63 in SHA-256 and 75 in device
syncs. The pages agent's SHA-256 is 5 samples, although its counter reports as many bytes checked as
the records agent builds.

- What pages skip: build, pack and validate, 283 of the records agent's 518 busy samples. Receiving
  and verifying the transfer is too small to show up at 100K.
- What both pay: process start and the store's recovery, TLS and the session, publication's device
  flushes, and the background scrub. Publication swings between runs (17 to 108 samples on the pages
  side) with `F_FULLFSYNC` latency.
- So pages cannot greatly beat records at 100K: the build work pages skip is about 0.3 s of busy time and 0.25 s of wall in a 0.5-0.8 s boot,
  and the rest is fixed.

## Open

1. 5M, one confirmation round per arm (D85), on the head with the review fixes. There, the build is
   most of the records agent's time: 14 s of CPU and 18.4 s to ready on main (warm-relay-agent-boot,
   job 261120-042638-33dce4). That run decides "greatly". Queue it at that head with
   `campaign job submit --quest dense-distribution-build --build "bash docs/perf/dense-distribution-bins.sh" --level strict --minutes 10 --env DD_PROFILE_S=60 -- bash docs/perf/dense-distribution-row.sh 5000000-flat-main records pages`.
2. The hash counters: the pages agent reports 4.85 MB checked, about the bytes the records agent
   builds, yet it profiles 12x fewer SHA-256 samples. Either the check runs on a faster SHA path or
   the counter includes bytes the scrub hashes after ready. Find out which before reading "bytes
   hashed" as cost.
3. The relay never went CPU-quiet between agents: it polls the authority every 10 ms. Each agent
   started after a bounded 5 s wait, recorded as `relay_quiet: false`.
