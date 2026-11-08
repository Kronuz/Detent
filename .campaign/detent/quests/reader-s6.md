# reader-s6: Reader S6, the lock-free reader core

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-s6`.

## Outcome

`libdetent_reader` is on `main`: the `detent_*` C ABI and its C++ header (`include/detent/reader.h`,
`reader.hh`), lazy attach by the store's directory descriptor, the switch, the ring with one freer,
fork handlers that drain cold page preparation, and Q6 (a)'s empty-store behavior. It reads
untrusted store files without blocking on a FIFO and binds each mapping's identity to the
descriptor it mapped. Values, mutants, separate sanitizer builds, a concurrent fork soak and green
CI on the merged head prove it; the 2026-11-05 security review's blockers are closed.

## Acceptance criteria

Numbering cites `reader/s6:docs/status/reader-s5-s6.md` ("S6 box n", "Next n") and
`docs/reviews/reader-s6-security.md` (SR1, SR2, "blocking n", "pending n").

- A1 D1 C ABI, D2 lazy attach, D4 switch, D5 ring and one freer, D10 fork handlers and the C++ header, checked by values (S6 box 1): `test_reader_core` and `test_reader_switch` in the `native` gate
- A2 Attach takes the store's directory descriptor (S6 box 2): `test_reader_core` in the `native` gate
- A3 Q6 (a): open succeeds on an empty store, lookups return the fallback with `DETENT_UNAVAILABLE`, open retried at most once per 200 ms (S6 box 3): `test_reader_core` in the `native` gate
- A4 T3: switch under 16 readers, injected parks, refusals counted, resources back after close (S6 box 4); T3 at defaults (60 s, 1,000 generations) passes `--repeat until-fail:20` in each separate sanitizer build (Next 4; handoff step 1.2): the `t3-asan` and `t3-tsan` gates
- A5 T6: thread count unchanged; the three fork cases (S6 box 5): `test_reader_switch` in the `native` gate
- A6 T1 syscall and allocation tests on the release library (S6 box 6): `test_reader_syscalls` in the `native` gate; Linux seccomp runs in hosted CI
- A7 `reader_bench` builds and runs: `--generate 1000` and a 1,000-call, 2-thread run of both readers with 0 allocations in the timed loop (S6 box 7): a job that builds and runs it
- A8 A dying mutant per claim (D5-1 to D5-3, D10-1 to D10-3, D2-1, D4-1, Q6-1, FIX-1, FIX-2) and a revert check per fix, FIX-2 in its final form with the deterministic `replaced_selected` case (S6 box 8; Next 2): mutant jobs at the landed code
- A9 [artifact] How-it-works note, fork and cold preparation included (S6 box 8): `docs/mechanisms/reader-core.md`
- A10 SR1: read-only opens of `change`, `selected`, lease and pack files cannot block on a FIFO; the 28 bounded synthetic-file cases pass and ordinary files keep their values (security review blocking 1): a job running the SR1 regression
- A11 SR2: mapping identity comes from the mapped descriptor; a deterministic replacement between mapping and identity capture recovers on explicit refresh and observes later publications (security review pending 1): a job running the SR2 regression
- A12 The fork-reply timeout is closed: the cold-preparation freeze and drain passes its three deterministic two-handle cases, its ordering mutants die, and a concurrent soak of `test_reader_switch` (four copies at a time, eight rounds; 32/32 on 2026-11-07 at `Detent:b1d281c`) passes at the landed code (security review blocking 2; ledger "Cold preparation and fork" 3-4): jobs
- A13 Fork during lazy-page preparation with two live handles: the child's mappings, descriptors and accounting return to their baseline (security review pending 2): a job
- A14 Size and accounting stress of the `change` mapping and the handle ledgers runs bounded, with no change to accepted capacity (security review pending 3): a job
- A15 The wide reader set passes on the merged tree: `^test_(local|delivery|testing|managed|system_managed|prepared|agent|reconnect)` plus the reader tests (Next 5): the `native` gate

CI (S6 box 9, "CI green on `reader/s6`"; security review pending 4) is the landing's CI gate, not a
row: it needs `campaign config ci=github` (REPORT open question O2). The reviews the ledger names
(security review of `prepared_reader.cc` and the core, the scoped architect review of the freeze
mechanism, the proof audit of Next 8) are this quest's lenses: `security` and `correctness`.

## Contracts

Provides the `detent_*` C ABI and `detent::Reader` (`include/detent/reader.h`, `reader.hh`),
`libdetent_reader`, and the D4 reader rule on `change` ownership. Consumes the S5 `change` file
(landed), `selected` and generation records, and `VerifiedPages`. `legacy-h` and `dense-pages`
build on these interfaces (Dense A8: "landed S6 interfaces are preserved").

## Decisions

1. Lead calls Q1-Q6 (a) and R1-R4 verbatim (D26). The ledger's "Design vs code" items are the
   lead's rulings for the reviewer to check: the failure memo keyed by the store word; attach
   re-reads `selected`; `detent_refresh` returns `DETENT_OK` on a refused publish; ownership failures
   map to `DETENT_SOURCE_FAILURE`; no D3 segment sharing across generations yet.
2. History: the security review (`docs/reviews/reader-s6-security.md`) is REJECT at `Detent:78195dd`
   (code `Detent:31ac047`); its blockers and pending checks are A10-A14 and the CI gate. The fresh
   security review is scoped to the code since then.
3. Open (owner): any cap on mapped or heap bytes per handle (Q5, reader S7) changes accepted
   capacity; A14 measures, it does not set a cap.
4. Prior evidence, queue runs and not jobs: `docs/measurements/reader-s6-soak-20261107/` (soak 1 of 24
   failing at `Detent:3d8559a`, 32 of 32 at `Detent:b1d281c`, T3 20/20 under each sanitizer, the FIX-2
   revert killed); A4, A8 and A12 rerun them as jobs at the landed code.
5. Open (lead): `dense-pages` owns `src/detent_reader/**`, `src/detent_local/**`, `tests/**` and
   `benchmarks/**`, so `campaign start reader-s6` needs Dense's file set narrowed or `--force`.

## May land early

None. The C ABI is public, and the security review covers all of it.
