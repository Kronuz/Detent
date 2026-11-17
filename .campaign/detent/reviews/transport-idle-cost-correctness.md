# transport-idle-cost review: correctness

Commit: consensus:cbcd56371519da36d782bec1c9623cc552a16316
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Both blocking findings are statements, not code: one doc paragraph and one lead decision row. The
bench and its numbers stand.

## Evidence I relied on

- 261117-122812-45a7bf (exclusive, consensus:0d92ef5, `cost`): A1. I re-derived from its log the
  D108 fit (OpenSSL slope 40,833 B, C++ 3,622 B per end at L = 1), the per-host percentages
  (1.4-1.5% under at L = 1, 5.8-6.9% over at L = 4) and the fit residuals (under 700 B). The
  earlier job 067ee0 (consensus:2c8e710) gave the same bytes to the byte, so the counts are
  deterministic.
- 261117-122904-8a23d2 (light, consensus:0d92ef5): suite, 49 of 49.
- 261117-122822-488d1f, 261117-122836-fb319f, 261117-122848-92c892 (exclusive, consensus:0d92ef5):
  A2. I recomputed every round's effect and A/A spread from the logs. Control: median 131 ns of
  [90, 102, 113, 131, 161, 166, 169], band 77 (round 7 hop 483 against 406). Data: median 216 ns,
  band 70 (round 2 hop 855 against 785). Idle wake: median 15,255 ns, band 8,654 (round 2 same
  57,519 against 48,866). The smallest round of each row (90, 185 and 12,886) clears its band, not
  only the median. The runs are ABBA, so linear drift cancels out of the effect. The band is the worst
  single-run spread, which is wider than the noise of a pair-mean difference: it is conservative.
  The stream rows reproduce job 30ce96 (139 and 208 ns).
- CI run 44169921897 (consensus:4c80573): `consensus_transport_idle` passed on all 8 platforms,
  macos-x86_64 included. The only failure is `consensus_transport_peers` (Subprocess aborted) on
  macos-x86_64. This is as the doc states.

## What I checked myself

Scripts are in `.scratch/review-transport-idle-cost-correctness/` in the home checkout.

- `asio_alloc.sh`, job 261117-133750-4b45cc (light, cbcd563). Asio's `aligned_new` calls
  `std::aligned_alloc` around the bench's `operator new` when `ASIO_HAS_STD_ALIGNED_ALLOC` is set
  (asio/detail/memory.hpp:101-112; config.hpp:396-440 sets it for glibc and macOS 10.15+). Rebuilt
  with `-DASIO_DISABLE_STD_ALIGNED_ALLOC`, every count matches the as-built run within 30 B per
  host (fits 52,273 + 2P x 44,454 and 55,262 + 2P x 48,399). On this Mac, Asio holds nothing
  uncounted at idle.
- `warmup.sh`, job 261117-133940-27f2ae (light, cbcd563). This printed the unprinted warm-up
  `cost(2, 1, 100, 0, false)`. The first configuration in a process holds 422,176 B per host and
  the same configuration after it holds 141,160 B. That is 281,016 B per host (562 KB per
  process) of one-time OpenSSL state, all OpenSSL and almost all "before any connection" (the
  first context and certificates). See F1.
- The wheel cut (question 1). By code, the wheel is a member of `task::asio_scheduler::timer_loop`
  (task_asio.hh:180-187). Its block is therefore at least `sizeof(wheel::timer_wheel)` on any
  allocator, so a wheel is never missed. By data, in every A1 configuration the C++ blocks of 64 KiB
  or more per end equal the timer-wheel bytes per host exactly (for example 18 blocks = 235,930 x 5
  at H = 5, L = 4). The per-group C++ figure is printed unfiltered (s3.cxx - before_groups.cxx) and
  reads at most 0.2 B, so nothing large was set apart during the group phase. The idle transport's
  only large buffers are released: the 16 KiB read chunk (connection.h:186-196) and the up to
  64 KiB `sending_` buffer (connection.h:221). See F3 for the case of another commit.
- The doc's numbers against the logs: per-end parts (Connection 464, Link 176), idle CPU
  284-1,291 us/s, the 1.3-2.2% of a core at 100,000 frames/s, and descriptors 1 + 2P. All match.

Question 4: A1 meets D108 as worded at consensus:0d92ef5. An idle group holds at most 0.2 B, 0
descriptors and 0 timers, and every frame was delivered. An idle end holds 44,454 B (L = 1) and
48,394 B (L = 4), both within 49,152 B. The per host is at worst 6.9% over 52 KB + 2P x 44 KB,
within 10%. This holds for the per-host count as defined, which excludes the process's one-time
OpenSSL state (F1) and the scheduler's wheels (which the doc discloses).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The per-host count excludes the process's one-time OpenSSL state. `run_cost` first runs an unprinted warm-up configuration. A host's first TLS context and certificates then hold 281,016 B per host (562 KB per process), about 4x D108's 52 KB fixed part. Neither the doc's "How it is counted" nor D108 says so, so "per host" reads as everything an idle host holds for its transport. | job 261117-133940-27f2ae; bench/transport_idle.cc:638 | Name the warm-up and its measured size in the doc (one paragraph: "a host process also holds about 550 KB of OpenSSL state built once, set apart by a warm-up run"). Whether D108's per-host budget should cover it is the lead's call. |
| F2 | yes | gap | A2 says "measured on the Linux gate host". No Linux host was used, and the doc says the criterion "is met on this Mac instead". The journal cites the lead's instruction, but no PLAN.md decision row records it, and the quest file still names the Linux gate host. As written, the doc claims a pass the criterion does not give. | docs/perf/transport-idle-cost.md:12-13; PLAN.md D106-D109 (none on A2's host); quest A2 | Lead: record the host change (a D row, or amend A2). Doc: "A2 names the Linux gate host; this Mac stands in for it (D1xx)", not "is met". |
| F3 | no | gap | Any block of at least `sizeof(wheel::timer_wheel)` (40,640 B) is set apart as a wheel, by usable size. At this commit nothing else reaches that size at idle (see above). A transport block that did would vanish from the per-end and per-group counts silently. transport-identity, due to land first, adds a per-connection sorted `opened` vector (16 B per group) and a per-loop `unordered_map` of groups. These pass 40,640 B at about 2,540 groups per connection. | bench/transport_idle.cc:93-101; `git diff origin/main...quest/detent/transport-identity -- include/consensus/transport/host_transport.h` | Set wheels apart by requested size in a narrow window (`n` in [sizeof(timer_wheel), +1 KiB), remembering those pointers for delete), or assert that the number of large blocks equals the number of loops with a wheel. |
| F4 | no | gap | The A1/A2 evidence is at consensus:0d92ef5, before transport-identity. That quest adds per-group and per-connection state to host_transport.h (`Stored`, `opened`, `closes`, the open frame). A1's 64 B per idle group may not hold after the merge. | campaign status (land transport-identity first); transport-identity diff above | After merging main, rerun 45a7bf and the three hop rows at the merged head. Update the doc's numbers and commit, or record the miss. |
| F5 | no | nit | Two limits on what A1's checks prove. The 64 B per-group check is an average over G x H groups, so a few retained `Lane::Group` entries (about 150 B each) would pass. "No frame per idle group" rests on every frame being delivered and none dropped, not on a held-frame check. Yet the doc says ctest "fails when an idle group holds ... a frame". | bench/transport_idle.cc:584-590; doc:18-19 | Doc: "fails when a frame is not delivered". Optionally expose and check the lanes' group-map sizes. |
| F6 | no | nit | At L = 4, "each loop that runs TLS adds per-thread OpenSSL state, which grows with the ends" is an attribution with no measurement behind it. The effect inflates the slope (conservative for the per-end check) and deflates the intercept. | doc A1 paragraph | Say it is inferred, or count OpenSSL bytes per thread. |
| F7 | no | nit | Byte counts are called exact on glibc too. There, Asio's handler memory goes through `std::aligned_alloc`, which `operator new` does not see. On this Mac that hides nothing at idle (4b45cc), but CI prints no numbers, so Linux is unverified. | asio config.hpp:428-435; job 4b45cc | Doc: "exact on this Mac"; or interpose `aligned_alloc` too. |

## Not run

- Nothing on Linux: no Linux byte counts and no Linux timing. CI's pass on Linux gives the limits only.
- I did not rerun the A2 timing rows. The logs' arithmetic is verified, and no number of mine
  decides the verdict.
- I did not check the transport-identity merge itself (F4 is a prediction from its diff, not a
  measurement).
- No sanitizer runs (CI's).

## Round 2

Commit: consensus:e0ae1b92abc364f03a3cc4771b7b1009022096fa
Verdict: APPROVE
Date: 2026-11-17

consensus:e0ae1b9 is docs only (`git diff cbcd563 e0ae1b9`: docs/perf/transport-idle-cost.md), so
the round-1 evidence and probes still apply to the code.

- F1, resolved. "How it is counted" now names the warm-up, its configuration and its measured
  size (422,176 against 141,160 B per host, probe 27f2ae), and it says D108 budgets the steady
  state. PLAN.md D111 (Detent:206d5aa0) records the same.
- F2, resolved. The doc says "this Mac stands in for it (D111)" and no longer says "is met". D111
  records the change, and quest A2 names the Mac (Detent:fd73d79d).
- F5-F7, resolved in wording: "on average over G x H groups", "when a frame is not delivered",
  "inferred, not counted per thread", and "exactly on this Mac" with the glibc caveat.
- F3 and F4 stand as non-blocking: the wheel cut, and rerunning A1/A2 after merging
  transport-identity.

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F8 | no | nit | The doc and D111 call the one-time cost "about 281 KB more per host, once" and attribute it to "a host's TLS context". Later hosts in the same process also build their own contexts and certificate stores without paying it, so most of it is process-wide OpenSSL state. The probe split 562 KB over the warm-up's 2 hosts, so a one-host process may pay up to about 562 KB. | probe 261117-133940-27f2ae; doc "How it is counted" | Say "about 562 KB per process, once (281 KB per host of the warm-up's two)", or split it with a one-host warm-up. |
