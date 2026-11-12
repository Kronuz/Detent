# One knob type: page bytes and read speed (A3)

Main's two-type tree against the one-knob tree on the dense-pages corpora (100K and 5M records,
`/Users/kronuz/code/Kronuz/Detent/.scratch/dense/corpus-{100k,5m}.bin`, SHA-256 in each job's
`inputs.sha256`). The design note ([../design/one-knob-type.md](../design/one-knob-type.md))
defines the columns and draws the conclusion.

## Procedure

`one-knob-type.sh` drives every step as a campaign job; outputs land under
`/Users/kronuz/code/Kronuz/Detent/.scratch/okt/`.

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

Dense model, job 261111-154349-807bef; C++ full builds, jobs 261111-160108-f85bd5 (100K) and
261111-160112-7622b7 (5M).

| Corpus | Body | Pages | Page bytes | Page B/record | C++ full build |
| --- | --- | --- | --- | --- | --- |
| 100K | main | 3,245 | 6,209,298 | 62.1 | 3,245 pages, 6,209,298 B |
| 100K | fixed width | 3,077 | 7,322,418 | 73.2 | not built |
| 100K | varint | 2,874 | 4,942,647 | 49.4 | 2,874 pages, 4,942,647 B |
| 5M | main | 162,367 | 315,432,480 | 63.1 | pending |
| 5M | fixed width | 154,334 | 374,372,124 | 74.9 | not built |
| 5M | varint | 144,544 | 253,048,761 | 50.6 | pending |

## Read speed

Pending the measure jobs.
