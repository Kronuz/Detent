> Status: measured at 100K, 1M and 5M (2026-11-12; the fixed part 2026-11-17); the warm second plan at 500K and 5M (2026-11-19).

# A delta's plan, linear in the keys it changes

A block answer's plan phase grew superlinearly with the store: a 0.5 C delta planned in 0.75 s at 100K
(50,035 keys, 15 µs per key) and 512.8 s at 5M (2,499,245 keys, 205 µs per key; job 261112-144057-f1c829),
while the 5M full copy planned in 10.6 s.

## Cause

`TrieOverlay::Impl::remove` (`src/detent_local/paged_trie_overlay.cc`) runs once for every base node the
overlay converts to a fresh one. The nodes of one page share its reference, so it skips a page already
removed, and it found that page with a linear `std::find` over `removed_extents`, every page removed so far.
A delta changing a fraction f of a store of P pages converts about two nodes per changed key and touches
most pages once f passes a few percent, so each key costs O(P) reference comparisons and the plan
O(keys x P). The `removed_set` index declared beside the vector, and emptied by `discard`, was never filled.

Profile (macOS `sample`, 15 s of the plan, 0.5 delta at 1M on main's code, job 261112-170345-ea0ce8, plan
44.9 s under a light hold): `remove` is the top frame in 7,828 of the 10,346 working samples (76%); next is
`verify_page_structure` in 818 (8%, first-touch verification, once per page); `TrieOverlay::step`,
`TrieOverlay::keys` (the in-page key count of a converted inline node, bounded by the page) and page
decoding follow, none above 4%.

## Fix

`remove` finds a removed page in `removed_set`, keyed by the page's placement (segment, offset), the key
the overlay assembler's `extent_index` already uses for the same pages. `removed_extents` keeps its order,
so `touched_pages()` and the published bytes are unchanged. A counting comparator exposes the index's
comparisons as `TrieOverlay::removal_probes()`.

## Counting probe

`tests/detent_local/test_delta_plan_removal_probes.cc` changes every definition of a 2,000- and a 32,000-key
store (20 and 320 pages removed) and counts the extent comparisons per changed key:

| Build | 2,000 keys | 32,000 keys | Growth for 16x the pages |
| --- | --- | --- | --- |
| Main's linear search (the mutant) | 11.7 | 178.3 | 15.3x |
| The index | 8.4 | 23.3 | 2.8x |

The test requires growth of at most 4x.

## Before and after

Plan phase of the 0.5 C delta (seed 1009), per size. Before is main's code, after the fix's; the 100K and 1M
before figures come from the identity job (261112-170932-ee31ea: main and the fix on clones of one prepared
store, light hold), the after figures from the strict rounds, and the 5M before from job 261112-144057-f1c829.

| Corpus | Keys changed | Plan before | Per key before | Plan after (strict) | Per key after | vs 100K |
| --- | --- | --- | --- | --- | --- | --- |
| 100K | 50,035 | 0.782 s | 15.6 us | 0.675 s (261112-170955-436de9) | 13.5 us | 1.0x |
| 1M | 499,922 | 23.89 s | 47.8 us | 6.919 s (261112-170958-40ef49) | 13.8 us | 1.03x |
| 5M | 2,499,245 | 512.8 s | 205 us | 34.855 s (261112-175733-62e242, exclusive) | 13.9 us | 1.03x |

Apply wall (plan, decode, encode, publish) after the fix: 0.788 s at 100K, 7.394 s at 1M, 36.986 s at 5M (14.8 us/key). Per-key plan cost
at 1M is within 3% of 100K, inside the 2x bound. Other phases are unchanged within noise (1M: decode 0.32 s,
publish 0.15 s; the full copy plans in 0.21 s at 100K and 2.14 s at 1M). Peak RSS at 1M: 820 MB before,
817-828 MB after. Each removed page now charges about 56 bytes to the storage ledger (about 8 MB at 1M, and
about 40 MB at 5M); the margin is noted, not consumed.

Plan time per changed key stays flat from 100K to 5M (13.5, 13.8, 13.9 us), so the plan is linear in the keys
it changes: 512.8 s becomes 34.9 s at 5M (14.7x). What remains is a different question: at 5M a 0.5 delta
still costs 2.5x a full copy's apply (36.986 s against 14.864 s, whose plan is 10.759 s). That is T9's
question, not this fix's.

Byte identity (job 261112-170932-ee31ea): every file of the published store is identical between main and
the fix at 100K (f 0.05, 0.5, 1; 30, 32, 32 files) and 1M (f 0.5; 206 files).

## The fixed part: moved definitions and repeated page reads

After the index, a uniform f 0.05 delta at 5M still planned in 13.80 s, as long as a full copy (job
261112-184757-fefadb). Profile jobs 261117-075622-1f24d9 (1M) and 261117-075635-2d7760 (100K) show why. A
uniform delta touches nearly every leaf page and rewrites 81-82% of the store's bytes, so the plan is
proportional to the store. The cost splits into re-packing every touched cluster (39-44%), re-validating every
definition on a new page (23-28%) and first-touch verification of each old page (about 15-20%). DTPAGE03 is
kept (D107), so the cuts are per rewritten byte:

1. `WarmGraphValidator` walks the old tree beside the new one. A new definition whose old leaf has the same key
   and bytes is matched, and neither side is read. Every other definition is validated as before.
2. `TrieOverlay` reads each old node once per change, not twice, and counts an inline node's keys once.
3. `checked_page_bytes` matches a page already verified in this mapping by its raw header fields.

The published bytes are unchanged. Job 261117-094340-baa902 found main's and the fix's stores identical at
100K (f 0.05, clustered 0.05 and 0.5: 28, 22 and 30 files) and at 1M (f 0.05 and clustered 0.05: 188 and 114
files).

### Before and after

Plan CPU of the f 0.05 deltas (seed 1009). A uniform delta spreads its keys over the store. A clustered one
(`cF`) changes one run of adjacent keys, so its keys share leaf pages. At 100K and 1M, main (Detent:186d5f1d)
and the fix (Detent:bd51ade2) ran interleaved on clones of one prepared store: strict job
261117-094335-a15eaa, 5 rounds at 100K and 3 at 1M, run beside 7 light holds. At 5M, the after figures come
from the exclusive round 261117-094351-8b7ee4. The before figures come from main's 5M round
261112-184757-fefadb, which recorded no plan CPU and had no clustered arm.

| Corpus | Arm | Keys | Rewritten bytes | Plan CPU before | Plan CPU after | ns per byte, before to after | Cut |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100K | uniform | 5,065 | 4.09 MB | 0.288 s [0.264, 0.295] | 0.346 s [0.243, 0.394] | 70.3 to 84.6 | -20%, within noise |
| 100K | clustered | 5,000 | 0.28 MB | 0.065 s [0.046, 0.075] | 0.053 s [0.041, 0.064] | 232 to 191 | 17.7%, within noise |
| 1M | uniform | 50,208 | 41.5 MB | 2.823 s [2.816, 3.160] | 2.706 s [2.250, 2.891] | 68.1 to 65.3 | 4.1% |
| 1M | clustered | 50,000 | 2.73 MB | 0.545 s [0.526, 0.593] | 0.380 s [0.375, 0.396] | 199 to 139 | 30.2% (worst against best 24.6%) |
| 5M | uniform | 250,986 | 207 MB | apply CPU 12.14 s | 10.35 s (apply CPU 11.72 s) | (not recorded) to 50.1 | apply CPU 3.5% |
| 5M | clustered | 250,000 | 13.6 MB | (no arm) | 1.961 s | (no arm) to 144 | (none) |

The 100K pairs (fix over main: 1.34, 1.34, 0.89, 0.99, 1.20) are inconclusive under the shared hold, so the
1M rows are the uniform result: a 4.1% median cut over 3 runs whose ranges overlap, so no clear gain; not 30%.

The clustered delta meets its bound at every size. 2 x keys x 13.8 µs is 0.138 s at 100K (worst run 0.064 s),
1.380 s at 1M (0.396 s) and 6.90 s at 5M (1.961 s). Its plan cost per key is flat: 10.6, 7.6 and 7.8 µs at
100K, 1M and 5M. The uniform delta's plan grows with the bytes it rewrites, not with its keys: 54 µs per key at
1M and 41 µs at 5M.

Other phases are unchanged within noise. At 1M, decode takes 0.04-0.06 s, and publish takes 0.36-0.56 s for a
uniform delta and 0.11 s for a clustered one. At 5M the uniform delta now applies in 13.95 s against the full
copy's 15.83 s in the same round (0.88x), but most of that ratio is the full copy running slower in that round:
the delta's own apply improved 3% (14.38 s to 13.95 s), and main's round had 14.38 s against 14.50 s (0.99x). The
clustered delta applies in 2.49 s (0.16x).

### Counting probe

`tests/detent_local/test_delta_plan_moved_definitions.cc` builds a store, changes every 20th definition, and
counts the warm validator's leaf reads. With the fix, a delta makes 2 leaf reads per changed key, the new
definition and its old one, at every size: 2K, 32K, 600K (job 261117-121521-315097), 100K and 1M (job
261117-121529-6da30a). At 1M that is 50,000 validated and 50,000 old reads, with 950,000 definitions moved
unread. With the match disabled (the mutant), every definition is read on both sides: 40 reads per changed key
at 100K (job 261117-121537-35314f).

### Where a uniform delta's plan goes now

Profile job 261117-094355-e06b99 sampled 3 runs of the fix at 1M (uniform f 0.05) with macOS `sample`. That
gave 8,684 samples under `PagedDeltaPlanBuilder::Work::step`, with these inclusive shares:

1. SHA-256, `kronuz::sha256` `compress_portable`, 24.6%. That is the scalar path: this host's i9-9980HK has
   no SHA-NI, while target hosts run the SHA-NI kernel (`sha256_x86.cc`) or the ARMv8 one (`sha256_arm.cc`),
   so this share is smaller there. The share splits into first-touch verification of old pages (9.5%), the new pages'
   commitments in `PagedPacker` (7.9%) and the new packs' checksums in `SegmentBuilder` (7.2%).
2. First touch of old pages other than SHA-256: a 30.6% share in `checked_page_bytes`, minus the 9.5% above.
   `verify_page_structure` takes 9.8%. The first `decode_page_header` of each page takes about 10%, although
   it does constant work per page. That suggests faults on the mapped base segments, an inference the samples
   cannot separate.
3. Packing other than SHA-256: about 14% (`PagedPacker::Impl::combine` 5.0%, cluster allocation about 5%).
4. Warm validation of the new pages: 22.1%. Most of it is node decoding (6.3% self in `decode_node`), the old
   walk (`old_advance` 6.5%) and `PageValidator` (4.6%). Main spent 7.7% in `WarmGraphValidator::Impl::leaf`
   (job 261117-075622-1f24d9), including `validate_definition`; a moved definition no longer reaches it.
5. The overlay walk outside first touch: about 9%, of which `TrieOverlay::Impl::keys` is 4.5%.

The work removed was about 8% of main's plan by its profile, and the strict A/B measured a 4.1% median at 1M, within the runs' spread. The rest is per rewritten byte, and most of it lies outside
this quest's reach:

1. SHA-256 is a quarter of the plan on this host only because it runs the portable kernel. Target hosts have
   accelerated kernels, so no SHA-256 work follows from it.
2. The commitments and checksums the format requires (about 15% here) are fixed by DTPAGE03.
3. Re-packing the unchanged clusters a uniform delta touches would need a DTPAGE03 change (D107).

D110 restates A2 on these numbers. It requires the clustered delta's 30% cut and its 2x bound at every size.
For the uniform delta, it requires the measured cut, stated with where the CPU goes: 4.1% at 1M, as above.

## A warm second plan on one mapping

Every plan above is the first plan on a freshly opened store: its pages are verified on first touch and read
from the mapping for the first time. A store that has planned once keeps the mapping and its verified-page
bitmap (`VerifiedPages`, `checked_page_bytes`), so a second plan on it finds those pages verified and faulted in. The harness plans
one delta three times on one recovered generation without publishing, so every plan has the same base:
A (seed 1009) cold, A again (warm, same delta), then B (seed 1010, the same shape, a different delta).
Odd rounds run A, A, B and even rounds B, B, A, so the cold plan is each delta in turn.

Plan wall time, median [min, max] over the rounds (5 at 500K, 3 at 5M), and the paired ratio of the warm plan
to the cold plan of the same round. Commit Detent:f6145feb (the harness at Detent:39155f00, plus the journal),
exclusive jobs, no other load on the host (a strict smoke ran beside light holds; no round did). Host as in
Method.

| Corpus | Arm | Keys | Cold | Warm, same delta | Warm / cold (wall) | Warm / cold (CPU) |
| --- | --- | --- | --- | --- | --- | --- |
| 500K | uniform 0.05 | 25,070 | 1.110 s [1.082, 1.136] | 0.735 s [0.709, 0.759] | 0.667 [0.624, 0.701] | 0.798 |
| 500K | uniform 0.5 | 249,922 | 2.218 s [2.162, 2.838] | 1.772 s [1.742, 1.839] | 0.804 [0.621, 0.829] | 0.873 |
| 500K | clustered 0.05 | 25,000 | 0.155 s [0.132, 0.159] | 0.120 s [0.107, 0.130] | 0.781 [0.756, 0.821] | 0.860 |
| 5M | uniform 0.05 | 250,986 | 12.101 s [11.687, 13.209] | 8.141 s [7.713, 8.294] | 0.660 [0.628, 0.673] | 0.804 |
| 5M | uniform 0.5 | 2,499,245 | 24.729 s [24.499, 27.528] | 19.445 s [18.648, 20.924] | 0.760 [0.754, 0.794] | 0.881 |
| 5M | clustered 0.05 | 250,000 | 1.730 s [1.522, 1.733] | 1.341 s [1.306, 1.369] | 0.790 [0.755, 0.881] | 0.869 |

A uniform delta's second delta (B after A) plans in the same time as A again: 0.730 s against 0.735 s (500K,
0.05), 7.672 s against 8.141 s (5M, 0.05), 19.756 s against 19.445 s (5M, 0.5). A and B rewrite the same bytes
there (197 MB at 5M, 0.05), and a uniform delta touches nearly every page, so the second delta finds the
first one's pages already verified. For a clustered delta A and B are different runs of adjacent keys that
rewrite different bytes (12 and 6 MB at 5M), so the "different delta" plans of that arm are not a like-for-like
comparison and are left out of the table; the same-delta ratio is its measure.

Reading:

1. The warm win holds at the full size and is the same size at both: a second plan on the mapping takes
   0.66 of the cold wall time at 0.05 uniform (500K and 5M), 0.76-0.80 at 0.5 and 0.78-0.79 for the
   clustered delta. The wall-time ranges of the cold and warm plans do not overlap in any arm. The 500K 0.5 ratio's low end,
   0.621, is its first round, whose cold plan took 2.838 s against 2.162-2.241 s in the other four; the median
   is not moved by it.
2. For the uniform deltas, half of the wall time saved is CPU and half is waiting. At 5M, 0.05 uniform, the cold plan spends
   10.1 s of CPU in 12.1 s of wall time and the warm one 8.1 s in 8.1 s: the first-touch work costs 2.0 s of CPU
   (20%) and about 2.1 s of wall time outside the CPU (page faults on the mapped base segments), which a warm
   plan does not wait for. The warm wall time equals the warm CPU time in every arm; the clustered delta's cold plan has almost no
   wait (0.013 s at 500K, 0.152 s at 5M), so its 0.78-0.79 ratio is CPU.
3. The saving grows with the pages touched, not with the keys: 4.0 s at 5M, 0.05 uniform (250,986 keys), 6.1 s at
   0.5 (2,499,245 keys), 0.36 s clustered. The 0.5 delta's relative win is smaller, consistent with
   its packing and validation (not measured here) growing against the same first-touch cost.
4. This host's SHA-256 is the portable kernel (see "Where a uniform delta's plan goes now"), so the first-touch
   CPU, which includes the portable SHA-256 of old pages, is an upper bound for hosts with SHA-NI or ARMv8 kernels; the
   wall-time share (faults) is not.
5. What a warm plan is: the second plan on a mapping whose pages the first plan verified, in one process.
   The numbers cannot separate the verified-page bitmap from the process's warm page cache and heap; both
   are what a second plan on an open store gets. A cold OS file cache (a first plan after a reboot) is not
   measured: the harness clones the prepared store with `cp -c` and the files were read by the prepare
   step before. The cold plan here is therefore the first plan on a store whose files are in the OS cache,
   and a first plan after a reboot would cost more.

Jobs: 500K corpus 261119-145602-9c34e4 (`corpus-500000.bin`, SHA-256 ae28985f...d629: the 5M corpus's
definitions with local number below 500,000, every namespace record, 500,000 definitions and 4 namespaces),
prepares 261119-152534-fa3505 (500K) and 261119-152536-eb759c (5M), smoke 261119-152539-acabbc (500K 0.05: cold
2.012 s, same 1.405 s, other 1.547 s, under 5 light holds), rounds: 500K rounds 1-5 261119-153419-124e73,
5M rounds 1, 2, 3 261119-153422-107aa3, 261119-153424-6e0a4a, 261119-153426-bc9994. The 5M cold plans agree
with the fixed part's measurement (uniform 0.05: 10.1 s of CPU against 10.35 s).

Harness: `tests/detent_local/test_delta_plan_scaling_warm.{cc,sh,cmake}` and
`test_delta_plan_scaling_subcorpus.py`. `test_delta_plan_scaling_warm.sh build`, `prepare` and `round N ARM ...`
build it, prepare the store and run one process per arm.

## Method

Harness: namespace-measure's T9 (`docs/perf/namespace-measure-t9.*` on `quest/detent/namespace-measure`).
Corpora: `corpus-100k.bin` (SHA-256 3214561e...a5740), `corpus-5m.bin` (bb87ae5d...c835), and a 1M corpus
derived from the 5M one: its definitions whose local number is below 1,000,000 with every namespace record,
1,000,004 records (b7f66891...6284). Host: Intel i9-9980HK, macOS 15, x86-64 Release build.
The fixed-part rows build this branch's harness (`docs/perf/namespace-measure-t9.*`) against each side's
library and interleave base and fix runs on clones of one prepared store, alternating order by round; plan CPU
and rewritten bytes are the harness's `plan_cpu_s` and `plan_bytes`.
