# M1: pins to head (docs/PLAN.md "M1: on the tip", done criterion 1)

Branch `m1/pins-to-head`, cut from `m1/pin-check` (`Detent:1098592`). Follows
`cpp-libraries/docs/UPGRADING-2026-10.md` "Updating an application (for agents)".

## State

All 33 manifest pins equal their library heads and `tools/check_pins.py` reports 0 mismatches.
The first CI run (below) predates the scheduler, logger and http-log repins and had pin-check red. Detent configures, builds and
passes the native suite (237 tests, 100% passed, 1 skipped: `test_local_trusted_paths`, run 3e2e95b
RelWithDebInfo). Local ASan/UBSan and TSan builds were not run; CI run 41791987741 (dispatched on
`Detent:3e2e95b`) ran them: linux-x86_64-gcc and clang, linux-arm64-gcc, macos-arm64, macos-x86_64,
linux-asan-ubsan, linux-tsan, linux-benchmarks, linux-read-counters and source-checks all green;
freebsd and headers skipped by the workflow; pin-check red with the 9 mismatches. No flakes hit.

## Waves

1. Leaves (manifest bumped to head, no library edit needed): atomic_shared_ptr, char_classify,
   compressors, constexpr_phf, endian, flat_map, generation_cache, metrics, radix_router, random_lib,
   split, stash, static_string, strict_stox, threadpool, varint, task (already). Their heads are
   past every "Commits not to pin" range (checked: metrics 4890fc0, compressors 8f57675, logger
   06a27d2, generation-cache 2930a64, strict-stox 199e0be, random 86bd0f5, allocators e38f759,
   http fce24c6).
2. Layer 1 libraries re-pinned: completion, hashes, reactor, repr, sha256, term-color.
3. Layer 2: enum-reflection, journal, strings, cluster.
4. Layer 3 and up: http-parser, consensus, http.
5. Not reachable: scheduler, logger, http-log.

## Library commits (each on the library's main)

| Library | Commit | What | CI run | Result |
|---|---|---|---|---|
| completion | `completion:9acfba3` | metrics db490b4 -> 9db73de, task 6558c2e -> bdafa5f | 41778240864 | all jobs green |
| hashes | `hashes:03ab2d6` | static-string, char-classify, constexpr-phf to heads | 41778269055 | all jobs green |
| reactor | `reactor:ab69cf2` | task -> bdafa5f | 41778274173 | all jobs green |
| repr | `repr:d2eb895` | char-classify -> 25decb5 | 41778277262 | green |
| sha256 | `sha256:f24692f` | endian -> 6b223e2 | 41778282059 | green |
| term-color | `term-color:76a400b` | static-string -> 7eaa8f9 | 41778285115 | green |
| enum-reflection | `enum-reflection:a7202ad` | constexpr-phf, hashes to heads | 41778946627 | green |
| strings | `strings:6799a6e` | char-classify, split, static-string, repr, term-color to heads | 41778955290 | green |
| cluster | `cluster:e4fc57d` | reactor, varint to heads | 41779537676 | green |
| http-parser | `http-parser:fcdad32` | enum-reflection -> a7202ad | 41779542035 | green |
| http | `http:c813531` | radix-router, http-parser, compressors, reactor to heads | 41780753369 | green |
| journal | `journal:e7c8a0f`, `journal:424c6c2`, `journal:bfad97f` | completion -> 9acfba3; the test IOs and `journal/memory_io.h` implement the read-only opens | 41778949860 and 41779506689 (red, abstract IO), 41781790238 | green |
| consensus | `consensus:e647e3c`, `consensus:cef08eb` | journal -> bfad97f, reactor -> ab69cf2; `FaultIO` forwards the read-only opens | 41782179231 (red, abstract IO), 41782585439 | green |

## API changes

- completion made the read-only opens pure virtuals of `IO` (`completion:c315c03`):
  `open_existing_read_only`, `open_reclaim_candidate_read_only` and the two `_into` forms. Every
  `IO` subclass implements them; journal's test IOs and `journal/memory_io.h` did not.

- Detent's tests had three `IO` subclasses missing the same four overrides (`FakeIO` in
  `tests/detent_local/test_inventory.cc` and `tests/detent_delivery/test_completion_scheduler.cc`;
  `Detent:bdef32d` and the following commit). No `src` code changed.

## Blocked on

Nothing. The owner authorized the scheduler, logger and http-log pushes; each was fetched first
(main had not moved) and CI was green on every job before the next.

| Library | Commit | What | CI run | Result |
|---|---|---|---|---|
| scheduler | `scheduler:d57e8fc` | stash -> b7d8876, threadpool -> 63e03a5, random -> f8b9348 | 41794834516 | green |
| logger | `logger:f61d8df` | scheduler -> d57e8fc, term-color -> 76a400b | 41795153407 | green |
| http-log | `http-log:4e4b79f` | http -> c813531, logger -> f61d8df, repr -> d2eb895, strings -> 6799a6e | 41795709745 | green |

`python3 -I tools/check_pins.py`: 33 libraries, 43 shared declarations, 0 mismatches. All 33
manifest pins equal their library heads. cluster is at its head `cluster:e4fc57d`; `legacy/wave2`
drops it and generation_cache.

## Final CI

Run 41796189324 (workflow_dispatch on `Detent:db67185`, branch `m1/pins-to-head`): conclusion
success. Every job green, including pin-check, linux-asan-ubsan and linux-tsan; freebsd and headers
are skipped by the workflow. No flakes hit.

## W5 evidence (branch `m1/w5-evidence`, rebased on `Detent:9a5a8c3`)

Done criteria 5 to 8 of `docs/PLAN.md` "M1: on the tip". Evidence note: `docs/perf/m1-w5-evidence.md`.

**State (2026-11-04):** measured and written (new = `Detent:9a5a8c3` plus the branch; main has since added `Detent:b547256`, not measured); CI on the pushed branch is the last step. Two results are
reported, not fixed: a managed first-read loss and the http-parser strictness (below).

Old is `Detent:bf20f94` (2026-11-02 18:06), the last main commit before `Detent:85253b5`, the first to move a
pin; new is the branch tip (`Detent:9a5a8c3` plus `tools/w5` and docs). Both built Release in separate trees
(`.scratch/old-wt`, `.scratch/build-old`, `.scratch/build-new`), same flags, system lz4 and zstd.

**Done**

1. Identity drivers (`tools/w5/`, `Detent:39aa307`, `Detent:781bc01`, `Detent:db68fba`): compressors, http-parser,
   strict-stox, enum-reflection, random, Detent snapshot JSON and `--just-knob`, distribution frames and local
   images, the logger transcript, a raw HTTP replay of the classic daemon. Results per class in the note; every
   difference has a library commit as its cause.
2. Hot-path A/B with an A/A control under `quiet exclusive` (read bench, authority update, durable ack, reply
   encoder, three-voter writers, managed first read on the 5M store). All inside the A/A band except the first read.
3. CHANGELOG, DEPENDENCIES.md, DEPENDENCY_UPGRADES.md (`Detent:502e2d9`) and the website's dependencies,
   installation and roadmap pages (`Detent:f8c747d`).

**Next:** CI on the pushed branch; the owner's call on the two items below.

**Blocked on** (owner decisions, nothing fixed here)

1. Managed first read is 2.6% slower on the new build (25 rounds, A/A 0.996; median 1,087.0 us old, 1,116.7 us new;
   147 against 103 of 1,000 reads under 1 ms), and the old build does not reach the sub-millisecond target on this
   host either. Options: bisect the intermediate main commits to attribute it (the arms differ by W2, the reader's
   S1 to S4 and the pins), or accept it. A bisect needs a build per step.
2. `detentd` now closes the connection without a response for requests http-parser's strict mode rejects (bare LF,
   obs-fold, `Transfer-Encoding` other than `chunked`, missing or odd version, `Content-Length` in a trailer); the
   old build answered. Options: keep strict (the guide's default), or set `lenient_http_headers` for the HTTP server.
   This narrows what clients can send, so it is the owner's.

**Design vs code:** no mechanism was added or changed; the drivers and scripts are test tooling.

**Process notes:** the `quiet` queue held the builds for hours behind other agents' holds; one two-second
benchmark was started outside a hold (noted in the evidence note).
