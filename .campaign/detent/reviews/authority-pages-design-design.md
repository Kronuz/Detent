# authority-pages-design review: design

Commit: Detent:65576604291cf2552abb38d123e06141ed0285ce
Verdict: CHANGES
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The direction holds: an authority-built root is the only option that closes Limit 1 for agents
taking pages, the build belongs off the service loop (inline stalls it in every job), and pages in
memory are simpler than persisted ones. What does not hold yet is the doc's account of what (b)
costs at rc3 load and after an election, which serving paths actually carry the authority's root,
and what the decision rows become. Those are text and measurement fixes, not a re-plan.

## Evidence I relied on

- Seal rows: `261122-210428-976f07`, `261122-210429-1ed7a8` (exclusive, `bf55a09`), strict reruns
  `261123-103432-c20472`, `261123-103435-b1e0ae`, `261123-103438-3b96b1`, `261123-102616-53d93e`,
  `261123-073248-5fdf61`. I re-read their JSON: the doc's per-seal p50s, bytes, relocations, (d)
  digest and the derived 59% / 2.95 cores / 1.65 seals/s / 19.5 s / 48 s arithmetic match.
- Commit rows: the eight strict jobs `261123-1025xx/1026xx` at `300f296b`. I re-tabulated every arm
  (`.scratch/detent/review-authority-pages/arms.py`); the medians and paired deltas in the doc are
  computed correctly from them. What they measure is the subject of F1.
- The NoChange fix and its fails-without pair (`261123-102550-885bdd`, `261123-102546-6e81cf`).

## What I checked myself

Scripts under `/Users/kronuz/code/Kronuz/Detent/.scratch/detent/review-authority-pages/`:
`bench/probe_bench.cc` is the quest's harness plus `coldpool` (a cold build on N threads) and
`satN` commit arms (N threads building 2,048-change seals back to back over their own 500,000-key
namespace while the arm runs); `build.sh` builds it against `$JOB_SRC`; `probe.sh coldpool|coalesce|sat`.

- `261123-145400-63b0f2` (strict, at `65576604`; `probe.sh coldpool` then `coalesce`). A 5M
  cold build (10 x 500,000) on 1 thread took 13.4 s and 16.3 s wall (12.3, 14.8 s CPU); on 2
  threads 8.6 s; on 4, 6.2 s; on 10, 4.6 s and 5.1 s wall at 1,807-1,844 MB peak RSS against
  426-492 MB on one thread. One builder pass over 18,432 changes (9 seals behind) cost 2,694 ms
  CPU p50, and over 102,400 (a 10 s burst) 4,523 ms, against 872 ms for one 2,048-change seal in
  the same job: 9 seals cost 3.1 times one seal, 50 seals 5.2 times.
- `261123-145405-24d1dd` (strict, at `65576604`; `probe.sh sat`): the commit path while 4 or 16
  threads build 500,000-key seals back to back (97-232 s of CPU in 23-26 s, the host's 16 hardware
  threads saturated), order none, sat16, sat4, none, sat4, sat16, none. Group p50: none 133.7 /
  144.6 / 139.8 ms; sat4 154.2 / 139.2; sat16 146.1 / 139.0. p99: none 357.5 / 230.9 / 329.5; sat4
  368.9 / 357.9; sat16 259.0 / 311.3. Group max: none 923.9 / 402.4 / 386.1; sat4 480.2 / 375.4;
  sat16 342.3 / 406.5. The probe's `worst_service_gap_ms` for the sat arms (2.4-7.3 s) is my
  artifact: the harness does not reset its last-pump time between arms, so the first gap spans the
  pool's own cold builds. Groups run back to back, so no stall inside a sat arm exceeded its group
  max, 480 ms.
- Re-read of every 5M cold-build sample in the quest's jobs: one exclusive (`1ed7a8`: 9.00 s CPU,
  12.04 s wall with sync); nine strict (`b1e0ae`, `5fdf61`, `3b96b1`, and the `cold` and
  `cold-write` lines of the four 5M commit jobs): 11.1-20.1 s CPU, 11.5-36.2 s wall.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The commit rows do not run the rc3 write load and their noise is larger than the stated floor. Every arm committed 139-474 tx/s, so 5 builder seals fell in 22-58 s (0.09-0.23 seal/s, a fifth to a tenth of rc3's 0.98, a twentieth of the burst); the builder thread was busy 5-15% of the run. Group latency is `F_FULLFSYNC`-bound and bimodal (about 135 ms or 260-300 ms) and switches inside a job under strict holds, which share the device: in `afdabb` none 134.6, thread 139.3, then inline 294.0, write 289.5; in `d23ae1` every arm reads 262-287 ms. So "the commit rows compare arms inside one job, which this does not bias" (doc line 9) is false for `afdabb`, and write's "+13.1 ms" is the median of {+154.9, +19.1, +7.0, -2.1}: not resolved. The thread arm's conclusion survives: my saturating pool (`24d1dd`) moved group p50 by -5 to +21 ms against the adjacent none arm, with p99 259.0-368.9 ms against none's 230.9-357.5 and group max 342-480 ms against 386-924. | `arms.py` over the eight `261123-1025xx/1026xx` jobs; `261123-145405-24d1dd` | State the commit rows' rate (about 0.2 seal/s) wherever their deltas are quoted; report write as "not resolved above a ±130 ms regime noise" and rule out persisted pages on simplicity and the 3.0 s root lag, not +13.1 ms; cite `24d1dd` (or a contributor rerun of it) for the builder at full load. |
| F2 | yes | gap | The root lag after a leader change is understated and its consequence for Limit 1 is not stated. The doc gives "about 9 s at 5M" from one exclusive cold build; nine strict samples read 11.1-20.1 s CPU, my one-thread runs 13.4-16.3 s wall, and seals keep committing during the cold build, so a single builder that builds per seal then trails by about 9 more seals (another 13 s at the doc's own 0.67 seal/s drain). A pool of 10 cut the cold build to 4.6-5.1 s, at 1.8 GB peak RSS against 0.43-0.49 GB, a spike the doc's memory row does not show; one coalesced pass over 9 seals then takes 2.7 s. Every answer in that window carries no authority root, and the doc leaves open (line 111) whether agents wait for it or publish records "verified against the next root". Waiting stalls visibility for that window, against SCALE_TARGETS' p99 publish-to-agent-visible under 1 s; publishing leaves Limit 1 open for the window, at every election and leader restart. | `1ed7a8`, `b1e0ae`, the 5M commit jobs' `cold` lines; `261123-145400-63b0f2` | Give the window with measured parts (pool cold build plus one coalesced pass: about 8 s on this host, at the RSS above); state the agent's rule when an answer has no authority root, and what Limit 1 becomes under it ("closed except for one window per leader change", or a visibility stall with its length), so the owner rules on it. |
| F3 | yes | gap | "The root in every answer is the authority's" is not specified on every serving path. (i) Followers serve relays: D145 (voter-read-freshness) has followers answer history and layouts once a leader confirms their read index, but under (b) a follower holds no root ("nothing on followers"). (ii) A relay fills the frame header from its own build today (dense-distribution D1: "the sender is the relay that built the target"), and the doc keeps relays that build "as a cache"; nothing states that a relay forwards the authority's root verbatim and refuses to serve pages whose root differs, which is the only thing that catches a relay that mis-applies records. Multi-tier relays need the same rule per hop. (iii) Positions committed between the old leader's last root and the new leader's cold build never get a root. (iv) The root is unauthenticated, so closure holds against a faulty relay that forwards headers, not against one that substitutes them. | doc lines 111, 131; dense-distribution.md:103, :158; PLAN.md D145 | State per path where the root comes from and what a relay and an agent do when it is missing or differs. One option that answers (i) and (iii) at about 32 B per touched namespace per seal: the leader proposes each built root as a log entry, so every voter holds and serves it and it survives a leader change. Say which threat model Limit 1's closure covers. |
| F4 | yes | gap | The pass-through relay, on which "(b) moves the build rather than adding it" (doc line 101) rests, is neither specified nor priced, and the Q4 row contradicts it. Q4 becomes "the authority adds roots, not a page server", yet a cold relay "takes the leader's canonical full copy" and pass-through relays serve "the authority's pages". A relay that does not build still needs D7.3's page window and D7.4's canonical copy, so either the leader serves page deltas, a window of superseded pages and a relocated full copy (none in the memory or wire columns; the wire column says +32 B), or the relay builds. Per update, pages from upstream are the cost D7.2 rejected: at 5M, 10,000 uniform keys are 108 KB of records against 6.71 MB of pages (dense-distribution Q1), and a 2,048-change seal rewrites 11.2 MB here. | doc lines 59, 93, 99-101, 132; dense-distribution.md D7.2-D7.4, Q1 | Specify what a pass-through relay fetches from the leader and when (full copies only, with records for deltas, is the cheap reading), and price the leader's added memory, CPU and egress per relay; or drop pass-through from the recommendation and restate (b) as an added cost, 59% of a core at rc3 on the leader. |
| F5 | yes | gap | The decision rows do not say what changes. D34's recorded reason is "builds stay out of the consensus process", and Q4 (b) was recommended because its option (c), "the authority builds pages", "puts builds in the consensus process"; (b) is that option on threads in the same process. The doc says D34 is unchanged and Q4 (b) "stands". D1's sender sentence and its rejected alternative "an authority-computed digest", D4's last paragraph, and D7.2/D7.4 change too and are not listed; D133, the current reading of D33 under which `dense-distribution-build` landed relays that build and fill the root, is not cited. | doc lines 127-136; PLAN.md D34, D133; dense-distribution.md:103-107, :158, :221-230, :365-371 | Rewrite the table: Q4 reopened (the authority builds, in-process off the service loop); D34's reason amended from process to loop; D1, D4 and D7 edits named; D133 cited; and the quests added with the landed code each one changes (the relay's root fill, the agent's check). |
| F6 | no | gap | The burst is sized per seal, but coalescing removes most of it. The doc's 2.95 cores and 19.5 s lag assume one build per seal; one pass over a 10 s burst's 102,400 changes cost 4.5 s on one thread (5.2 times a single seal, not 50), and 9 seals cost 3.1 times one. So one coalescing builder holds burst root lag at a few seconds on one core, and the pool earns its place only for the cold build (F2). That bounds the burst: CPU is capped by the pool size and the excess becomes root lag, never commit latency. | `261123-145400-63b0f2` | Replace "2.95 cores at burst" and "a pool divides this" with coalescing's numbers; name the pool's bound (threads, priority) and the root-lag metric (Limits 5). |
| F7 | no | nit | The inline arm's 2,650 ms is one job of four (971, 1,104, 1,507, 2,650 ms); the ruling stands on p99 in all four (1,020-2,303 ms against none's 234-490). | `arms.py` | Quote the range. |
| F8 | no | gap | The decisive CPU numbers (604.8 ms per seal, 9.0 s cold) are one exclusive job each, at `bf55a09`; strict runs read 1.2-2.2 times them. The doc says so; the 59% and 2.95-core figures could carry the strict range beside them. | `1ed7a8`, `b1e0ae` | An exclusive rerun at head, or the range. |
| F9 | no | nit | Build CPU includes the records encode (`authority_pages_bench.cc:265-302`: the CPU window opens before `encode_session_frames`), though journal ruling 3 says encode is timed apart. It inflates (b) slightly, so it is conservative. | file:line | Say so, or move the CPU start after the encode. |
| F10 | no | gap | (d)'s dismissal rests on 119 s for a cold agent to check pages, from SHA-256 without SHA-NI; a faster XOF or SHA extensions change it several times over. Limits 4 notes the construction; the decision request does not. | doc lines 91, 121 | One line in the decision request. |
| F11 | no | gap | The builder in the commit rows reads its own corpus, not the authority's applied records, so contention between the builder and apply or history for those records is not modeled. | `authority_pages_bench.cc:617-660` | A line in Limits. |

## Not run

- No exclusive rerun of the seal rows at head; the decisive per-seal CPU (604.8 ms) and cold build
  (9.0 s) remain one exclusive job each (F8).
- No Linux or SHA-NI host, no 3-voter replicated run, no small (2-4 core) host: the pool's effect
  on election timing on a small authority host is inferred from an 8-core probe only.
- No correctness or security review of the harness beyond what the numbers depend on.
- Reviewer ran as a subagent of the lead, not a session titled `detent · reviewer
  authority-pages-design design`.
