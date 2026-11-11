> Status: CURRENT (2026-11-04) M1 W5 evidence, done criteria 5 and 6 of [docs/PLAN.md](../PLAN.md) "M1: on the tip": the input classes through the old and the new build, and the hot-path A/B. Workstream: M1 W5 (`m1/w5-evidence`).

# M1 W5: input-class diffs and hot-path A/B, old against new

Two results need the owner's attention before M1 is called done:

1. **A managed first read is 2.6% slower on the new build** (geomean over 40 keys, 25 rounds, A/A 0.996) and the
   sub-millisecond target does not hold on this host for either build.
2. **Detent now rejects the requests http-parser's strict mode rejects** (bare LF, obs-fold, odd
   `Transfer-Encoding`, missing version): the connection closes with no response, where the old build answered.

Everything else is identical or inside the A/A band. Nothing was fixed on this branch.

## Arms

| | Commit | What it is |
|---|---|---|
| old | `Detent:bf20f94` (2026-11-02 18:06) | The last main commit before `Detent:85253b5`, the first to move a pin. Manifest: 33 Kronuz pins, all behind their heads. |
| new | `Detent:9a5a8c3` plus `tools/w5` and docs (the measured tree; the branch was later rebased on a main that adds `Detent:b547256`, the agent store's change file, which is not measured) | Main after legacy wave 2 removed the classic distributor. Every Kronuz pin at its library's head (`Detent:f7267d5`, pins unchanged since). |

The two arms differ by more than the pins. Between them main took the W2 coroutine rewrite (`task::task`
handlers), the authority on task and its owner loop, the retained-state envelope, the reader's S1 to S4
(verified-page bitmap, read counters), digest-once recovery, the bound-port work and the classic removal. The A/B
measures M1 as a whole; it does not attribute a change to a pin. The old arm's `detentd` still has the classic
profile, which the new arm lacks, so the classic HTTP replay below compares old against `Detent:f7267d5` (the last
main with it, same pins).

Host: Intel Core i9-9980HK, 32 GiB, macOS 15.8.1, x86_64 (both arms and every probe are `Mach-O 64-bit
executable x86_64`), Apple clang 17.0.0, Release, ccache, system LZ4 and Zstandard for both arms, a Ninja build
of each tree in its own build directory. Timing runs were under `quiet exclusive` holds of at most ten minutes;
identity runs and builds under `quiet light`. One `detent_authority_read_response_bench` of about two seconds
was started outside a hold while looking at its output format; it feeds no number below.

## Input classes (criterion 5)

Each driver in `tools/w5/` is one source built into both trees against that tree's own pinned libraries and
prints one line per case; `compare.py` matches cases by id. Reports: `docs/perf/m1-w5-evidence/identity-*.txt`.
Causes are the library commits the upgrade guide names; they were matched to the observed behavior, not bisected,
except where the table says otherwise.

| Class | Cases | Different | What differs, and why |
|---|---:|---:|---|
| compressors, valid streams | 72 + 36 XXH32 | 0 | Compressed bytes, decoded bytes and XXH32 digests (zstd, LZ4, gzip, deflate; one-shot and stream; 0 B to 300 KB; text and random) are identical. |
| compressors, damaged input | 8,614 | 345 | 322 flips and 15 crafted records: `LZ4Exception` becomes `LZ4CorruptVolume` (a subclass). 3 crafted LZ4 records abort the process in the old build and throw `LZ4CorruptVolume`. 2 zstd frames that claim 1 TiB hang (old) and throw `ZstdCorruptVolume`; 2 flips that threw `bad_alloc` throw `ZstdCorruptVolume`; a zlib preset dictionary decoded to nothing and throws `DeflateException`. Guide: `compressors:f157dab`, `8f57675`, `533bee4`. Truncations (2,040), garbage (384), trailing bytes, concatenation: 0 differ. |
| http-parser, rejected requests | 840 | 80 | All 80 in strict mode, none in lenient (each message is fed whole and byte by byte, so 40 messages): 30 requests and 10 responses accepted before are rejected (bare LF, obs-fold, `Transfer-Encoding` other than exactly `chunked`, malformed `Content-Length` or chunk size, version other than 0.9 to 2.0 or missing, `Content-Length` in a trailer; in responses a body after 1xx, 204, 304, `Transfer-Encoding` with `Content-Length`, malformed status or version). New errno names `HPE_CR_EXPECTED` and `HPE_INVALID_TRANSFER_ENCODING`. Guide: `http-parser:21c8f30`, `ab8377c`, `62ac7f3`, `0aeec14`, `e2f23b9`, `5cb4409`. The other 760 cases (every lenient case, valid messages, URL parsing, errno names) are identical. |
| strict-stox | 1,089 | 208 | All 208 are inputs of 64 bytes or more (0 shorter differ): the old build read the first 63 bytes; the new one parses the whole value, with its own `idx` and `ERANGE`. Guide: `strict-stox:7d7f5dd`, `a1b03fb`. |
| enum-reflection | 14,845 | 626 | Only strings forged to share a member's 64-bit hash (member before, miss after: 512 shared probes) and 114 probes of the two members http-parser added. Every member name, case variant, near miss, random string and length probe (14,000+) is identical. Guide: `enum-reflection:7d394c5`, `2a5da3a`. |
| random | 31 | 0 | Ranges stay inclusive and in range (11 ranges, `random_time`, `random_real`, `random_string`), draws stay uniform. The sequence differs between any two runs of either build (no seed API). |
| logger transcript | 476 lines | 6 lines | Everything up to the last step is identical. After `Logging::finish()` the new logger writes the later lines at the call (`after finish`, a slow watchdog, a severe line); the old pin dropped them. Cause: `logger:a48bce8` ("Never lose a line the scheduler refuses", 2026-10-20), which the old pin of 2026-10-17 predates, read from the logger CHANGELOG. The guide's own logger changes (the 1 ms burst delay, timestamps) are not visible in a one-producer transcript. |
| daemon log (classic, `f7267d5`) | 118 lines | 35 | The new daemon logs `committed snapshot version N in X us` for each accepted write (Detent's W2 owner commit, not a library), and a rejected request is no longer logged as a request. The glyphs, columns and message formats are the same. |
| Snapshot JSON (parse, serialize, merge, diff, patch, 300 knobs x 200 users x 5 groups of evaluation) | 112 | 11 | Serialized snapshots, patches, applied results, the evaluation digest and 46 malformed-input error texts are identical. The 11 are `--knob` integers of 64 bytes or more (strict-stox above), accepted now. |
| Wire frames (raw and zstd, snapshot, delta, progress) and local images (1, 7, 1,000, 100,000 records) | 14 files | 0 | Byte-identical (`diff -r`). |
| Persisted `snapshot.json` after 65 HTTP exchanges | 1 | 0 | Byte-identical. |
| Prepared reader metadata of the 5M store (each build's agent publishes its own) | 4 files | 2 | The 320-byte root image is byte-identical, the 1,441 packs are reused unchanged. `selected` differs in the 16-byte generation id; `.read` and `.lease` differ in the generation id and in one 32-byte digest each. Each probe reads the other build's store with no failure (40 reads). |

### Classic daemon over HTTP, old against `Detent:f7267d5` (65 raw requests, one connection each)

55 cases are identical after masking the daemon's own counters, including every zstd, gzip, identity,
conditional, diff and partial-update answer. Ten differ (`classic-replay.txt`): `bare-lf-request-line`,
`bare-lf-header`, `bare-lf-all`, `obs-fold` and `version-1.2` and `no-version` were answered 200 and are now
closed with no response; `te-gzip`, `te-chunked-twice` and `cl-trailer` were answered 400 and are now closed;
`connect`'s 404 carries `Connection: close` where it carried `keep-alive` (http's CONNECT handling).
Setting `lenient_http_headers` restores the old answers. The authority daemon has no old-versus-new replay
because both arms parse with the same strict parser from the first request it serves.

## Hot path (criterion 6)

`tools/w5/ab_rounds.py` runs A, B and A again every round with the order rotating; `ab_compare.py compare` reads
the saved outputs. "old/new" above 1 means new is faster; a row counts only when it clears the A/A band.
The classic detentd is deleted on main, so the daemon rows use the authority path both arms have: the
authority state machine and durable ack benches, the reply encoder, and three real voters over HTTPS.

| Row | old | new | old/new (range) | A/A | n | Verdict |
|---|---:|---:|---:|---:|---:|---|
| `detent_read_bench` 1 thread, ns/op | 260.5 | 262.2 | 0.992 (0.95-1.05) | 1.018 | 6 | noise |
| `detent_read_bench` 4 threads, ns/op | 360.5 | 392.9 | 0.913 (0.89-1.03) | 1.016 | 6 | noise |
| 4 threads again, 14 rounds | 370.4 | 359.0 | 1.000 (0.82-1.11) | 0.991 | 14 | noise |
| `detent_read_bench` 8 threads, ns/op | 431.1 | 415.2 | 1.019 (0.99-1.09) | 1.038 | 6 | noise |
| managed first read, 5M store, geomean of 40 keys | 1,087.0 us median | 1,116.7 us median | 0.974 (per-run 0.943-0.994) | 0.996 | 25 | **new slower** (all 25 runs below 1) |
| first reads below 1 ms (1,000 each) | 147 | 103 | A/A arm 148 | | | target not held by either |
| authority update median | 25.7 us | 26.4 us | 0.924 (0.81-1.18) | 0.930 | 8 | noise |
| authority CPU per update | 26.7 us | 27.9 us | 0.902 (0.81-1.09) | 0.938 | 8 | noise |
| authority load of 100,000 knobs | 687.2 ms | 676.4 ms | 1.017 (0.98-1.14) | 1.018 | 8 | noise |
| durable ack per transaction | 33.5 ms | 33.3 ms | 0.999 (0.98-1.02) | 0.997 | 7 | noise |
| durable ack, rerun | 33.3 ms | 33.3 ms | 0.999 (0.98-1.04) | 0.988 | 9 | noise |
| durable load of 20,000 knobs | 665.6 ms | 676.7 ms | 0.975 (0.89-1.04) | 0.994 | 7 | flagged slower by the tool |
| durable load, rerun | 671.7 ms | 674.2 ms | 0.999 (0.91-1.03) | 1.004 | 9 | noise |
| checkpoint | 232.9 ms | 228.9 ms | 1.002 (0.95-1.05) | 0.999 | 7 | noise |
| authority read reply (2 MB), median wall | 129.1 ms | 129.4 ms | 1.001 (0.98-1.01) | 1.002 | 5 | noise |
| three voters, 16 producers, 20 s: HTTP requests per second | 27.3 | 28.4 | 1.034 (0.82-1.09), new/old | 1.010 | 5 | noise |
| same: mean command latency | 0.597 s | 0.572 s | 1.042 (0.82-1.09) | 1.008 | 5 | noise |

Reading the table:

- **The one loss is the managed first read.** Old against old (the A/A arm) gives 1,089.7 us and 148 of 1,000
  below 1 ms; new gives 1,116.7 us and 103. The geomean over 40 keys is 2.6% in new's disfavor, every one of the
  25 rounds is below 1, and the same direction showed in the first 4-round run (0.969). 5 of 40 keys are flagged
  individually in the shorter run. The cause is not attributed: the new arm differs from the old by W2, the reader's
  S1 to S4 and 30 pins at once. Candidates, not findings: the verified-page bitmap that replaced the lazy-page
  mutex and LRU (`Detent:2ceeff9`), and the shared digest and resource libraries the reader links. A bisect over
  the intermediate main commits would separate them.
- **The sub-millisecond target is not reproduced on this host.** The old build's own median is 1.087 ms and 147
  of 1,000 reads are under 1 ms, so "held" cannot be confirmed here for either build. Earlier acceptance runs
  (`docs/measurements/reader-capsule-managed-5m-20261029`) report medians of 0.70 to 0.80 ms on their host
  state; this run did not reproduce that for the old build, so the comparison above is relative.
- The 4-thread read bench read 0.913 in the first run and 1.000 in a 14-round rerun with a wide spread (0.82-1.11
  per run); `load_ms` read 0.975 and then 0.999. Both first readings were within the A/A spread of the run and are
  not repeated.
- Nothing here is a power or cost figure. The loss is 29.7 us on a 1.09 ms read, paid once per process on its
  first read of a store.

## Rerun

All commands from the repository root; build directories and scratch under `.scratch/`.

```sh
# 1. Two trees, same flags.
git worktree add .scratch/old-wt bf20f94
for t in old new; do
  src=$([ $t = old ] && echo .scratch/old-wt || echo .)
  cmake -S $src -B .scratch/build-$t -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
    -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_BENCHMARKS=ON -DCMAKE_PROJECT_INCLUDE=$PWD/tools/w5/include.cmake
  quiet light bld-$t 60 "build $t" -- cmake --build .scratch/build-$t -j4
done
# 2. Input classes (drivers write .scratch/id/{old,new}); the reports go to docs/perf/m1-w5-evidence.
quiet light id 40 "identity drivers" -- tools/w5/run_both.sh .scratch/build-old .scratch/build-new .scratch/id
tools/w5/identity_report.sh .scratch/id docs/perf/m1-w5-evidence
# 3. Classic replay: needs a detentd that still has the classic profile, one per arm (old, and Detent:f7267d5).
python3 -I tools/w5/http_replay.py --detentd OLD/detentd --out .scratch/replay-old --port 18402
python3 -I tools/w5/http_replay.py --detentd F7267D5/detentd --out .scratch/replay-new --port 18401
python3 -I tools/w5/replay_compare.py .scratch/replay-old .scratch/replay-new
# 4. Hot path (each under quiet exclusive, at most ten minutes).
quiet exclusive ab-read 10 "A/B read" -- tools/w5/run_ab.sh read 6 .scratch/ab/read
quiet exclusive ab-upd 10 "A/B authority update" -- tools/w5/run_ab.sh authority-update 8 .scratch/ab/authority-update
quiet exclusive ab-dur 10 "A/B durable" -- tools/w5/run_ab.sh authority-durable 9 .scratch/ab/authority-durable
quiet exclusive ab-rd 10 "A/B reply" -- tools/w5/run_ab.sh authority-read 5 .scratch/ab/authority-read
quiet light tls 10 "fixtures" -- ctest --test-dir .scratch/build-old -R authority_tls_certificates   # and build-new
quiet exclusive ab-w 10 "A/B writers" -- tools/w5/run_writers_ab.sh 5 .scratch/ab/writers
# 5. Managed first read: a saved 5M store (the preserved capacity-trials/5000000-restore-r12/agent-0), a CA file.
quiet light m5 30 "prepare" -- python3 -I tools/w5/first_read.py prepare --old .scratch/build-old --new .scratch/build-new \
  --source SAVED/agent-0 --ca CA.pem --work .scratch/m5
quiet exclusive ab-fr 10 "A/B first read" -- python3 -I tools/w5/first_read.py measure --old .scratch/build-old \
  --new .scratch/build-new --work .scratch/m5 --cases benchmarks/prepared-pack-access-5m-20261025.json --rounds 25
python3 -I tools/w5/first_read.py cross --old .scratch/build-old --new .scratch/build-new --work .scratch/m5 \
  --cases benchmarks/prepared-pack-access-5m-20261025.json
```

Files in `docs/perf/m1-w5-evidence/`: `identity-*.txt` (the six classes, the store files, the logger transcript
diff), `classic-replay.txt` and `classic-daemon-log-diff.txt`, `ab-*.txt` (one `ab_compare.py` report per row,
with the 40-key first-read report), `first-read-5m-*.txt` (the arm summaries, the prepared files and their
digests). Raw per-run outputs stay in `.scratch/ab/` and `.scratch/m5/` of the worktree.

## Limits

- One host. The i9 is the only machine used; no Apple silicon, no Linux.
- The A/B arms differ by every commit between `bf20f94` and `9a5a8c3`, not by pins alone.
- The first-read row reads each arm's own prepared store, which that arm's agent published; the stores' reader
  metadata differs only in the generation id and the digests that cover it.
- The sub-millisecond target needs the acceptance store state of the reader runs; this host did not reproduce it
  for the old build.
