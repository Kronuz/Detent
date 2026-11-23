# reader-s6 review: correctness

Commit: Detent:1d7f04f26ec986e0dc3aadcf53ad92e4231ead87 (round 4, test-only. Round 3 was at
`Detent:c1c7498e`, round 2 at `Detent:80cde23b`, round 1 at `Detent:d8c01825`)
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Round 4 (linux-tsan hang)

Re-checked only `Detent:c1c7498e..1d7f04f2`: one commit, `test_reader_syscalls.cc`, both
`SECCOMP_RET_KILL` returns of the child's filter now `SECCOMP_RET_KILL_PROCESS`, plus the header
comment. Diff and the whole test read.

- The hang is deterministic, not this run's bad luck: linux-tsan never finished on this quest. Probe
  `bash .scratch/detent/review-reader-s6-correctness/tsan-hang-runs.sh` (GitHub logs, no job) shows
  runs 44955216762, 45119864837 and 45152925089 each start `test_reader_syscalls`, never finish
  it, and are cancelled 87-107 min later with `ctest` and one `test_reader_syscalls` left to kill.
- Diagnosis: right. The parent's writer threads start TSan's background thread; TSan's fork
  interceptor restarts it in a child whose parent had one user thread (`ForkChildAfter`,
  `start_thread`), so the child is two threads before `prctl(PR_SET_SECCOMP)`, which without
  `SECCOMP_FILTER_FLAG_TSYNC` filters only the calling thread. `SECCOMP_RET_KILL` (=
  `KILL_THREAD`) ends the main thread only when another thread lives; the background thread
  sleeps in a loop, holds `result[1]` through the shared fd table, and the parent's `read`
  (`test_reader_syscalls.cc:127`) never sees EOF. Only one `test_reader_syscalls` in the runner's
  orphan list fits: a process whose leader thread has exited exposes no `/proc/PID/environ`, so
  the runner cannot tag the child. The plain and ASan children are single-threaded, where
  `KILL_THREAD` already ended the process, which is why linux-asan-ubsan and the plain Linux legs
  passed at `Detent:c1c7498e` (run 45152925089).
- The fix ends it: `KILL_PROCESS` is a group exit, every thread dies, the fds close, `read` returns
  and `waitpid` reaps. TSan then reports "signal 31", accepted under `DETENT_SANITIZED` as before.
- The plain proof is unchanged: its child is single-threaded (it exited 0 under the old filter at
  `Detent:c1c7498e` on gcc, clang and arm64), so a disallowed call ends it with SIGSYS under either
  action and line 134 still requires exit 0, 8 bytes and `answer == total`. The allowed path is
  untouched.
- Other ways the parent waits forever: none that a dying child reaches. Any signal death is
  process-wide; `_Exit` is `exit_group`; the only allowed blocking call is an 8-byte `write` to an
  empty pipe. What is left is a child that loops without a syscall, a bug the parent cannot bound
  (N7).
- Kernel: `KILL_PROCESS` needs 4.14 (2017) headers and kernel. CI is Ubuntu 24.04 (6.x); every
  maintained target distro is past 4.14. On an older kernel the unknown top bit is masked out and
  the action decays to `KILL`, the old behavior, so nothing fails that passes today.
- CI at `Detent:1d7f04f2` (run 45189633970) was queued when I wrote this; linux-tsan finishing is
  the proof the hang is gone and is the landing gate, not this review's.

## Round 3 (fix round D146)

Re-checked only `Detent:dece1118` (C3), `Detent:b490591d` (C4) and `Detent:c1c7498e` (N6), plus the
contributor's C3 residual (journal, Blocked on, item 0). Diffs read whole.

- Gates at `Detent:c1c7498e`: native 261123-020546-339806 exit 0, t3-tsan 261123-020547-63d2a8
  exit 0, plain `test_reader_core` 261123-020608-eda47a exit 0.
- C3 fix: `free_retired` frees every zero-sum view outside the installed slot at the start of every
  `refresh_locked`, whatever its outcome, and after a publish outside the new slot. Same Dekker
  argument as the old commit-time scan: a reader can only keep a pin on the slot `current` names,
  and that slot is the one kept; a reader that pinned a stale slot fails its recheck before reading.
  The Refused check now follows the scan, so a zero-sum next slot is freed as before. Test
  `retained` (park at `Pinned` across the commit, cap 1.5 generations, then every rev-2 key);
  RETAIN-1 261123-020610-c38819 at `Detent:0b19b62f` (fix reverted) exits 134 with "revision 2
  reads 15564 of 60000". My `probe_retained_cap` now exits 0: 60,000 of 60,000 at rev 2 (job
  261123-021428-e9d6ef).
- C4 fix: an acquire load of an atomic `atfork_installed` before the lock; the lock only while
  registering. `fork_mid_open` parks an opener at `Registering` and forks; ATFORK-2
  261123-020612-66011f at `Detent:528e67ca`, which moves the window inside the lock and drops the
  fast path, ends its child with SIGALRM (exit 134). Left open: a fork during the very first
  registration (lock held around `pthread_atfork`) still deadlocks a child that opens; that is
  `pthread_once`'s old hazard, once per process.
- N6 fix: `find` in place of `at`; read.
- C3 residual, probed (`probe_midanswer_cap`, same job 261123-021428-e9d6ef, exit 1 by design):
  a lookup parked at `LazyPageWindow::Miss` in rev 1 (past its sequence check) across the commit
  of rev 2 answers rev 1's value (41, correct for its snapshot) and unpins without refreshing. Rev 2
  then reads 15,564 of 60,000 keys, the rest `no_memory` with the fallback, twice over, charged
  1,814,976 of 2,152,716 with 2 generations; one `detent_refresh` releases it: 60,000 of 60,000, one
  generation, charged 1,435,144. No wrong value anywhere.

Ruling on the residual: not blocking (R1). D143's contract is a bound on mapped bytes with
`DETENT_NO_MEMORY` past it (reader.md D3, reader-core.md Bounds); the retired generation is still
mapped, so the charge is true, and reader-core.md step 1 now states when it leaves: at the first
refresh after the pin drops. What made C3 blocking was that nothing short of a publish released
it, `detent_refresh` included; now `detent_refresh` does, the window is bounded by the next
publish or refresh (at most three retired views), and the miss is reported, never silent. The cost
is real, though: under concurrent lookups a commit usually finds a reader mid-answer, so a hot
handle whose read set exceeds about half its cap answers part of the store with fallbacks until
the application refreshes or the publisher publishes. Option (b) in the journal, a lookup refused
with `DETENT_NO_MEMORY` while a non-current slot holds a view running `free_retired` and retrying,
removes that and is what I recommend; (a), keep and document, is acceptable.

## Scope of round 2

The quest's own commits after `Detent:d8c01825` that touch `src/` or `include/` (first parent,
main's merges `Detent:78706fc6` and `Detent:f3261943` excluded): `Detent:c300e5fa` (C1, C2, S1
bounds, S2 atfork), `Detent:5957f287` (N1, `ReadAdmissionRefused`), `Detent:07a77ec2` (D143
mapping cap and `reader_capacity_probe.cc`), plus the test-only `Detent:a60e0894` and the docs
commit `Detent:96a069ce`. `Detent:80cde23b` differs from `Detent:f3261943` only in the journal.

## Evidence I relied on

- C1 at the fix: suite native 261122-214347-15579c (exit 0 at `Detent:a60e0894`, carried to the
  head across a main merge that touches none of its files) runs `test_reader_core`'s
  `refresh_memo`. That test is round 1's probe sequence: hide the selected `.read`, first lookup
  `unavailable` twice, restore, still remembered, `detent_refresh` `ok`, then the lookup must answer
  `ok` with one attach (`test_reader_core.cc:130-145`). It tests the value C1 is about.
- C1 without the fix: MEMO-1 261121-133829-b558f2 at `Detent:a68f7e50`, whose only code change
  from `Detent:96a069ce` is the two-line memo clear in `detent_refresh` (diff read), exit 134 with
  "t:rev: status unavailable, expected ok", the post-refresh check. So the same test fails without
  the fix.
- C2, N1 and D143's `capped`: the journal's MISS-1 261121-133832-23f97d, NOMEM-1
  261121-141047-4552ff (log read: `t:k10 source_failure`, line 208), CAP-1 a5d80c, CAP-2 9b81e0,
  all killed; native 15579c passes them.

## What I checked myself

Read the src/include diffs of all five commits, `lazy_page_access.cc` (miss, publish, resolve),
`allocator.hh` and `storage.hh` (tickets), and `core.cc`'s refresh, lookup, free and open paths at
the head. Probes, rerun with `bash .scratch/detent/review-reader-s6-correctness/recheck.sh` from a
checkout (job 261123-014705-863bc2 at `Detent:80cde23b`, exit 1 by design, see C3):

- `probe_refresh_memo` (round 1's C1 probe, unchanged): exit 0 at the head. `detent_refresh`
  after the restore answers `ok`, the next two lookups `ok` value 1, a publish then value 2.
- `probe_retained_cap` (new): 60,000-key store (two packs), cap 2,000,000. Rev 1 read whole
  charges 1,435,144. A reader parks pinned on the installed view while rev 2 is published and
  `detent_refresh` commits; released, it refreshes itself (Unchanged) and answers 2. Then rev 2:
  generations 2, charged 1,814,976, 44,436 of 60,000 keys `no_memory` with the fallback; after a
  second `detent_refresh` the same 44,436; after rev 3 is published, 1 generation and every key
  reads. Control: one generation fits the cap.

D143 accounting, by reading: every charge is RAII and refunds on every failure path. A pack's
ticket is admitted before `mmap` and refunded by `~Mapping` when `mmap`, the identity `fstatat`,
the segment allocation or the bitmap allocation throws (`lazy_page_access.cc:167-176`,
`199-206`); `StorageAllocator::deallocate` refunds after release (`allocator.hh:39-46`); `State`
(and its slot table) is charged at attach and refunded with the `Generation`. Remap of `change` is
uncharged, as documented. The default is 512 MiB for zero and for an options struct that ends
before the field (`core.cc:628-633`, short-circuit read); a cap above `SIZE_MAX` is clamped. The
floor's static assert matches `VerifiedPages`' word count. No accounting defect found; the
defect is in when a retained generation is released (C3).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no (fixed at Detent:c300e5fa) | defect | Round 1: `detent_refresh` did not clear a failure remembered by an unattached namespace's first attach. **Fixed at `Detent:c300e5fa`**: a refresh that does not fail stores `UINT64_MAX` in `failed_store` (`core.cc` `detent_refresh`). Confirmed. | Fix: suite 261122-214347-15579c (`refresh_memo`); fails without: MEMO-1 261121-133829-b558f2; probe at head 261123-014705-863bc2 (`probe_refresh_memo` exit 0) | Done. |
| C2 | no | defect | Round 1: a transient cold miss failed the whole generation. Fixed at `Detent:c300e5fa`: resource errnos on `openat`, and `fstat`/`mmap` failures, raise `system_error` (`DETENT_IO`), rethrown past the catch-alls without setting `failed`. | `transient_miss` in 15579c; MISS-1 261121-133832-23f97d | Done. |
| N1 | no | nit | Round 1: ledger refusals reported `DETENT_SOURCE_FAILURE`. Fixed at `Detent:5957f287` (`ReadAdmissionRefused` maps to `DETENT_NO_MEMORY`). | `capped` in 15579c; NOMEM-1 261121-141047-4552ff | Done. |
| N2-N4 | no | nit | Round 1 nits (first lookup refused by a pinned slot, grown-`change` `seen`, A16 baseline), left by the lead. | round 1 | As round 1. |
| C3 | no (fixed at Detent:dece1118) | defect | Round 2: a view pinned at a commit kept its generation charged to the D143 cap until the next publish; `detent_refresh` did not release it, so the new generation's keys answered `no_memory`. **Fixed at `Detent:dece1118`** (`free_retired` on every refresh outcome). Confirmed. | Found: 261123-014705-863bc2. Fix: `retained` in 261123-020546-339806 and 261123-020608-eda47a; fails without: RETAIN-1 261123-020610-c38819; `probe_retained_cap` exit 0 in 261123-021428-e9d6ef | Done. |
| C4 | no | defect | Round 2: `atfork_mutex` locked on every open; a child forked mid-open deadlocked on its next open. **Fixed at `Detent:b490591d`** (atomic fast path). Only the first registration still holds the lock, as `pthread_once` did. | `fork_mid_open` in 339806; fails without: ATFORK-2 261123-020612-66011f | Done. |
| N5 | no | nit | `DETENT_MINIMUM_MAPPING_BYTES` equals one full pack plus its bitmap exactly (`core.cc:58`), but the same ledger is charged first for the page table's `State` and slot table (`lazy_page_access.cc:105`, `271`) and then the segment object, each with an allocator header (`allocator.hh:32`). At the floor no pack whose rounded mapping is 1 MiB can be mapped, so the open succeeds and every lookup in such a pack answers `DETENT_NO_MEMORY`. The docs do not promise one readable pack, only the open refusal. | `core.cc:58`, `lazy_page_access.cc:105`, `199-207`, `271` | Say "below which no pack can be read" or add a fixed page-table allowance to the floor; or keep. |
| N6 | no | nit | `reader_capacity_probe` with `refuses` calls `statuses.at("no_memory")`, which throws (exit 134, not 1) when the default cap refuses with another status. | `reader_capacity_probe.cc:111` | **Fixed at `Detent:c1c7498e`** (`find`). |
| R1 | no | defect | C3 residual: a lookup mid-answer at a commit (past its sequence check, e.g. in a cold miss) unpins without refreshing, so the retired view and its generation stay charged until the next refresh or publish. Bounded, reported (`DETENT_NO_MEMORY`, fallback, never a wrong value), released by `detent_refresh`, and described by reader-core.md step 1; see the round-3 ruling. | `probe_midanswer_cap`, job 261123-021428-e9d6ef: rev 2 reads 15,564 of 60,000 with no refresh, 60,000 after one `detent_refresh` | Owner's call: (b) a lookup refused with `DETENT_NO_MEMORY` while a non-current slot holds a view tries `refreshing`, runs `free_retired` and retries (recommended; test: the probe's `Miss` park, no refresh), or (a) keep, with one line in reader.h telling callers `detent_refresh` returns retired generations' charge. |
| C5 | no (fixed at Detent:1d7f04f2) | defect | Round 4 (found by CI): under TSan the seccomp child kept TSan's unfiltered background thread after `SECCOMP_RET_KILL` ended its main thread, so it never exited and the parent blocked on the result pipe until linux-tsan was cancelled. **Fixed at `Detent:1d7f04f2`** (`SECCOMP_RET_KILL_PROCESS`). Plain proof unchanged. | `tsan-hang-runs.sh`: 44955216762, 45119864837, 45152925089; `test_reader_syscalls.cc:111`, `115`, `127` | Done; CI 45189633970's linux-tsan confirms. |
| N7 | no | nit | Nothing bounds the parent's wait: a child that loops without a syscall hangs `read` and `waitpid`, and the test has no ctest `TIMEOUT`; the child also holds ctest's output pipe, so killing the parent alone would not free ctest. | `test_reader_syscalls.cc:101-129`; `tests/detent_reader/CMakeLists.txt:22` | Optional: `alarm(N)` in the child before the filter (SIGALRM kills the whole process, no later syscall needed), or keep. |

## Not run

- Round 4: no Linux run of any kind (no Linux host here); CI 45189633970 at `Detent:1d7f04f2` was
  queued. TSan's fork behavior is from compiler-rt's `ForkChildAfter`, not a trace of the child.

- Round 3: no ASan, Linux or CI result read (CI 45152925089 was running); no `reader_capacity_probe`
  run (its job 261123-020615-e1cb4e was queued).

- No rerun of the gates, TSan, A16, the soak or `reader_capacity_probe` at a 5M scale; no Linux run
  of the new probe (deterministic, single park, Debug on macOS).
- No probe of C4's deadlock (it needs a fork timed inside a mutex hold); read only.
- Main's merges and the security lens's bounds review (`reviews/reader-s6-security.md`).
