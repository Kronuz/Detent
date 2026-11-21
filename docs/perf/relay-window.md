# Relay pages tier at 5M: builds over the head against full builds

Status: measured, one run per arm, macOS x86-64 Release, each under `quiet strict` beside light
holds. The spread is unmeasured, so treat differences under about 10% as noise.

Verdict: building each head over the previous one (D7.2) cuts a head move's build CPU from 1.2-1.5 s
to 0.06-0.9 s per 500K-key namespace, 1.5-24x less, scaling with the keys changed rather than the
namespace. The cold build costs no more (11.4 s against 13.6 s of CPU for 10 namespaces). Live heap
after eight moves is 740 MB against 787 MB, and peak resident 1,522 MB against 1,640 MB. Cold peak
resident is 9% higher (1,380 MB against 1,263 MB) at equal live heap.

## Method

- Harness: `benchmarks/relay_window_bench.cc` (`detent_relay_window_bench`), which drives
  `NamespaceRelay`'s public API over a scripted upstream that serves real history files. It keeps
  those files in memory, so the starting heap (about 280 MB) is upstream and mirror state, not pages.
- Workload: 10 namespaces of 500,000 keys (5M; a history file holds at most 500,000 changes). A cold
  pages reader of every namespace builds all ten. Then eight moves each write one seal to the first
  namespace and ask a cold pages reader of it, which builds that head inside the answer:
  `s1000 c50000 s10000 c200000 s100 s1000 c50000 s10000` (`s` scattered keys, `c` a contiguous range).
- Command: `detent_relay_window_bench DIR 10 500000 s1000 c50000 s10000 c200000 s100 s1000 c50000 s10000`.
- Before is `Detent:eab7477c` (full builds, a window of the last 4 builds, each with its packs and
  transfer) with the same bench file added. After is this branch's head.
- Build is the answer's wall and CPU time, which is the build at the head plus encoding the answer.
  Heap is `malloc_zone_statistics` in use; resident and peak come from `task_info` and `ru_maxrss`.

## Results

| Step | Before CPU | After CPU | Before heap | After heap | Before resident | After resident |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| start | | | 275 MB | 282 MB | 320 MB | 344 MB |
| cold, 10 builds | 13,610 ms | 11,429 ms | 813 MB | 821 MB | 1,263 MB | 1,380 MB |
| s1000 | 1,225 ms | 172 ms | 761 MB | 731 MB | 1,277 MB | 1,385 MB |
| c50000 | 1,335 ms | 259 ms | 812 MB | 739 MB | 1,348 MB | 1,403 MB |
| s10000 | 1,358 ms | 484 ms | 775 MB | 726 MB | 1,431 MB | 1,417 MB |
| c200000 | 1,394 ms | 912 ms | 789 MB | 740 MB | 1,467 MB | 1,505 MB |
| s100 | 1,507 ms | 63 ms | 824 MB | 743 MB | 1,518 MB | 1,505 MB |
| s1000 | 1,433 ms | 163 ms | 872 MB | 747 MB | 1,560 MB | 1,505 MB |
| c50000 | 1,454 ms | 229 ms | 872 MB | 753 MB | 1,611 MB | 1,518 MB |
| s10000 | 1,461 ms | 466 ms | 787 MB | 740 MB | 1,640 MB | 1,522 MB |

Peak resident equals resident at every step in both arms.

## Reading

- A head move's CPU tracks the keys it changes: 63 ms for 100 scattered keys, 912 ms for a 200,000-key
  range, where a full build of the namespace costs about 1.4 s whatever changed. Over the eight moves,
  2,748 ms against 11,167 ms.
- The pages tier's live heap (heap minus start) settles at 445-470 MB after, against 486-597 MB
  before. The window holds generations that share segments and counts only bytes older positions
  alone reach, where the old window held four full builds' packs and transfers.
- Resident memory climbs in both arms above the live heap: freed build buffers stay mapped. After
  flattens at about 1.5 GB; before keeps climbing to 1.64 GB.
- The cold peak is 117 MB more resident at the same live heap. It is unattributed: the cold build
  keeps the build's segments plus the canonical copy's transfer in both arms, so allocator layout is
  the likely cause, not retained data.
