# Reader lookup and first read (reader-s6)

The reader core's warm lookup cost, and the cost of its first read in a fresh process. Both were
measured on an Intel Core i9-9980HK running macOS 15 (Darwin 24.6). Each arm reads a store that its
own tree wrote.

## Warm lookup: `reader_bench`

Job `261119-045435-27840a`, strict, interleaved: `Detent:e77b31d` (head) against `Detent:b1d281c`
(before the paged node codec).

The two trees use the same settings:

1. Both run Release `reader_bench`, built with Apple clang.
2. Each tree writes its own store with `--generate 50000`, which is 100K knobs.
3. Each run is `--operation int --reader both --threads 1 --calls 1000000 --repetitions 1`.
4. The job runs six base/head pairs.

The cells show the median over the pairs of the per-thread p50 and p99, in ns per call. Each call
includes the clock read, which costs about 65 ns.

| Case | p50 before | p50 head | Difference | p99 before | p99 head |
|---|---|---|---|---|---|
| core hot | 402.0 | 460.5 | +58.5 | 481.5 | 574.5 |
| pinned hot | 406.5 | 462.5 | +56.0 | 447.0 | 520.5 |
| core scattered | 648.5 | 697.0 | +48.5 | 1052.0 | 884.0 |
| pinned scattered | 688.0 | 701.0 | +13.0 | 1110.0 | 923.0 |

The hot gap comes from the out-of-line node codec (`decode_node`, `slot`, and `lookup`'s own
time). Profile `261118-224115-baceeb` puts it at about 70 ns. That gap
belongs to `paged-decode-one-shape`. The header decode is no longer on the hit path: the walk
reads each page's external count raw.

## First read: where the gap to main was

`docs/perf/first-read-gate.md` describes the gate. At `Detent:e77b31d`, the branch's first read was
23-33 µs slower than main's (`8cd932c`) in all four cells. The cost sat in the first `detent_get`,
not in `detent_open`.

That first `detent_get` read `selected` twice: once to compare generation ids, and once more
inside `PreparedReader::attach`. `detent_open` had already read it once. Each read is two `openat`s
(the table and its image), their checks, two `pread`s and a table decode. Main's Client reads it
once.

Three changes remove those reads:

1. `Detent:c027f04`: attach starts from the table that the refresh read, and reads `selected`
   again only on a retry.
2. `Detent:65afbd0`: a namespace's first attach at the installed view's store word takes the view's
   table, along with the sequence loaded before that table was read (D4). `detent_refresh` always
   reads `selected`.
3. `Detent:2dae6ab`: the store path is stat'ed only when the trust walk fails.

Job `261119-111012-db0806` ran a strict A/B beside five light holds. Each arm ran six rounds of 200
fresh-process first reads after five warmups, in an order rotated each round. Each arm's branch
binary was the gate's `first_read_core` built at the named commit. The branch stores sat at the
same path depth as main's.

Cells are the median of the round medians, in µs, with the median paired difference against
`e77b31d` in parentheses. The last row is the first `detent_get` phase.

| Arm | 1 namespace | 1,000 namespaces |
|---|---|---|
| `e77b31d` | 688.6 | 725.4 |
| `c027f04` (attach from the read table) | 638.5 (-47.8) | 687.0 (-49.9) |
| `65afbd0` (first attach from the view's table) | 612.8 (-72.4) | 652.2 (-82.7) |
| `2dae6ab` (stat on failure only) | 604.3 (-73.3) | 659.8 (-56.4) |
| main `8cd932c` | 652.4 (-29.8) | 712.2 (-24.2) |
| first `detent_get`, `e77b31d` to `2dae6ab` | 298 to 222 | 324 to 251 |

The stat change is within noise. With all three changes, the branch's first read is about 50 µs
faster than main's in both cells. `test_reader_core selected_reads` holds the read count: one
`selected` read at open, none more at the first lookup, and one per switch.
