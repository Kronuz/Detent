# One knob type: page bytes and read speed (A3)

Main's two-type tree (Detent:555b6379) against the one-knob tree (Detent:ea9d4147, jobs at Detent:dab00c10) on the
dense-pages corpora (100K and 5M records,
`/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-{100k,5m}.bin`, SHA-256 in each job's
`inputs.sha256`). The design note ([../design/one-knob-type.md](../design/one-knob-type.md))
defines the columns and draws the conclusion.

## Procedure

`one-knob-type.sh` drives every step as a campaign job; outputs land under
`/Users/kronuz/code/Kronuz/Detent/.scratch/okt/ea9d4147/`; the converted corpora are in
`.scratch/okt/corpora` (`CORPORA` in each job's environment).

1. `corpora OUT` (light): `one-knob-type-corpus.py` rewrites each pinned corpus's bodies as the
   fixed-width and the varint one-knob body; the census JSON counts definitions and body bytes.
2. Dense model (light): `dense-pages-model.py pack CORPUS 4096 --rule=height` for main's, the
   fixed and the varint corpora.
3. `pages OUT SIZE` (light, `--ref BASELINE=<main>`): `test_local_dense_fullbuild` from each
   tree's own Release build, main on main's corpus and the one-knob tree on the varint corpus, and
   the dense model again.
4. `prepare OUT` (light, `--ref BASELINE=<main>`): Release builds of both trees with clang 23.1.2,
   `one_knob_read` (`one-knob-type-read.cc`, `-DONE_KNOB_MAIN` in main's tree) and 1-namespace
   stores of both corpora from each tree's own exporter.
5. `measure PREPARED OUT SIZE N-N` (exclusive, one D51 pair per job): each run clones its arm's
   store, reads it into the page cache, times 7 passes of 200,000 sampled reads (no group, a
   ramped group, a forced group and an unlisted one) after a warm pass, and 60 fresh-process first
   reads, each after a warm-up process. The first arm alternates pair by pair.
6. `report OUT SIZE`: per metric, the paired differences (one-knob minus main), each arm's spread
   (median absolute difference of consecutive runs) and the noise floor (the larger spread); NO
   WORSE when the median paired difference is at most the noise floor.

## Page bytes

Dense model, job 261111-154349-807bef; C++ full builds of both trees and the model again at the
head, jobs 261111-201652-e744f8 (100K) and 261111-201652-1b9d0e (5M). The varint roots
(1f457c20… on 100K, 8a2e8747… on 5M) equal those at Detent:b9bf610c and Detent:432d75a4.

| Corpus | Body | Pages | Page bytes | Page B/record | C++ full build |
| --- | --- | --- | --- | --- | --- |
| 100K | main | 3,245 | 6,209,298 | 62.1 | 3,245 pages, 6,209,298 B |
| 100K | fixed width | 3,077 | 7,322,418 | 73.2 | not built |
| 100K | varint | 2,874 | 4,942,647 | 49.4 | 2,874 pages, 4,942,647 B |
| 5M | main | 162,367 | 315,432,480 | 63.1 | 162,367 pages, 315,432,480 B |
| 5M | fixed width | 154,334 | 374,372,124 | 74.9 | not built |
| 5M | varint | 144,544 | 253,048,761 | 50.6 | 144,544 pages, 253,048,761 B |

The C++ full build matches the model byte for byte for both trees at both sizes. The same jobs
report admission memory, the decoded snapshot before the build: 15,412,448 B (main) against
17,970,224 B (one-knob) on 100K and 783,597,840 B against 908,076,272 B on 5M: 16% more, about
25 B per definition, the size of the added `optional<int64>` rollout value (not isolated); peak
21.8 MB against 23.3 MB and 1.10 GB against 1.16 GB (+7% and +6%). The same runs report admission
at 0.20 s against 0.24 s (100K) and 7.5 s against 8.7 s (5M), and full-build CPU at 0.30 s
against 0.28 s and 12.7 s against 12.4 s. One run each in a light job beside other load: an
observation, not a timed result; the build path is not one of A3's metrics.

## Read speed

Prepared builds and stores, job 261111-201510-0027ae; pairs 0-5, jobs 261111-201515-c9a550,
261111-201520-86effa, 261111-201525-efcbe6, 261111-201530-3605db, 261111-201535-254d6a,
261111-201539-7b4315 (100K) and 261111-201550-b4eb67, 261111-201555-ac7f70, 261111-201600-cbb858,
261111-201605-e35e36, 261111-201609-b99e4c, 261111-201613-6fb580 (5M); verdicts, jobs
261111-201545-be6229 (100K) and 261111-201618-c669dd (5M). Each arm's statistic per pair is the
median of its 60 first reads and the median of its 7 steady passes; the table gives the median
over the six pairs.

| Corpus | Metric | Main | One-knob | Paired median | Noise floor | Pairs faster | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100K | first read | 645.0 us | 600.5 us | -44.4 us (-6.9%) | 87.0 us | 4 of 6 | no worse |
| 100K | steady read | 1,212 ns | 1,099 ns | -126 ns (-10.4%) | 30 ns | 6 of 6 | no worse |
| 5M | first read | 716.6 us | 575.5 us | -123.5 us (-17.2%) | 132.0 us | 5 of 6 | no worse |
| 5M | steady read | 1,929 ns | 1,769 ns | -179 ns (-9.3%) | 212 ns | 5 of 6 | no worse |

Per pair, main / one-knob (first read in us, steady read in ns):

| Metric | Pair 0 | Pair 1 | Pair 2 | Pair 3 | Pair 4 | Pair 5 |
| --- | --- | --- | --- | --- | --- | --- |
| 100K first | 645.4 / 606.0 | 667.5 / 591.7 | 580.6 / 648.6 | 673.8 / 574.3 | 558.7 / 626.4 | 644.5 / 595.1 |
| 100K steady | 1,187 / 1,108 | 1,219 / 1,078 | 1,222 / 1,091 | 1,205 / 1,119 | 1,191 / 1,070 | 1,243 / 1,110 |
| 5M first | 1,428.5 / 575.0 | 708.2 / 541.5 | 608.6 / 653.9 | 725.0 / 576.0 | 593.0 / 558.0 | 734.1 / 636.1 |
| 5M steady | 2,117 / 1,686 | 2,046 / 1,899 | 1,774 / 1,889 | 1,812 / 1,631 | 1,809 / 1,632 | 2,073 / 1,851 |

Every metric meets D51's rule (median paired difference at most the noise floor). Only the 100K
steady read improves by more than the noise floor; the other three are inside it, so they show no
regression and no claimed gain. Main's 5M first read in pair 0 (1,428.5 us) is an outlier the
median absorbs; without that pair the paired median is -98.0 us, still inside the floor.

An earlier run of the same procedure on the tree at Detent:b9bf610c (prepare 261111-162827-7366e8,
verdicts 261111-183007-d14279 and 261111-183020-636dd8) gave the same verdicts: -11% and -9.5% on
100K, -22% and -7.0% on 5M; its 5M first read crossed the floor (-154.1 us against 49.6 us), which
this run does not reproduce.
