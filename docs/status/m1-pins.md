# M1: pins to head (docs/PLAN.md "M1: on the tip", done criterion 1)

Branch `m1/pins-to-head`, cut from `m1/pin-check` (`Detent:1098592`). Follows
`cpp-libraries/docs/UPGRADING-2026-10.md` "Updating an application (for agents)".

## State

Stopped at W4 contention: all 33 manifest pins equal their library heads, but `tools/check_pins.py`
still reports 9 mismatches, all through scheduler (see Blocked on). Detent configures, builds and
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

- scheduler (`scheduler:f080858`) pins stash 8fa5d2a, threadpool eb4751a, random e040c2b. Bumping it
  is W4's (Xapiand campaign); the push was refused by the permission classifier and not retried.
  The logger (pins scheduler 1e5ccc9) and http-log (pins logger 801105c) follow.
  `check_pins.py` still reports: scheduler (stash, threadpool, random), detent_logger (scheduler,
  term_color), http_log (http_lib, detent_logger, repr, strings). The fix is one commit each, in
  that order, with scheduler first; repin scheduler with `.scratch/repin.py`-style edits (three
  GIT_TAGs in `scheduler/CMakeLists.txt`). The Xapiand campaign's mains for these four libraries
  showed no open PR and no branch newer than 2026-06 besides merged ones; the refusal came from the
  permission system, not from a visible conflict.
- cluster is pinned at its head `cluster:e4fc57d`; `legacy/wave2` drops it and generation_cache.
