# reader-s6 review: correctness

Commit: Detent:80cde23b062be51a291268b2428334c021903b1e (code of `Detent:f3261943`; round 2 of
this review, round 1 was at `Detent:d8c01825`)
Verdict: CHANGES
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Scope of this round

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
| C1 | yes | defect | Round 1: `detent_refresh` did not clear a failure remembered by an unattached namespace's first attach. **Fixed at `Detent:c300e5fa`**: a refresh that does not fail stores `UINT64_MAX` in `failed_store` (`core.cc` `detent_refresh`). Confirmed. | Fix: suite 261122-214347-15579c (`refresh_memo`); fails without: MEMO-1 261121-133829-b558f2; probe at head 261123-014705-863bc2 (`probe_refresh_memo` exit 0) | Done. |
| C2 | no | defect | Round 1: a transient cold miss failed the whole generation. Fixed at `Detent:c300e5fa`: resource errnos on `openat`, and `fstat`/`mmap` failures, raise `system_error` (`DETENT_IO`), rethrown past the catch-alls without setting `failed`. | `transient_miss` in 15579c; MISS-1 261121-133832-23f97d | Done. |
| N1 | no | nit | Round 1: ledger refusals reported `DETENT_SOURCE_FAILURE`. Fixed at `Detent:5957f287` (`ReadAdmissionRefused` maps to `DETENT_NO_MEMORY`). | `capped` in 15579c; NOMEM-1 261121-141047-4552ff | Done. |
| N2-N4 | no | nit | Round 1 nits (first lookup refused by a pinned slot, grown-`change` `seen`, A16 baseline), left by the lead. | round 1 | As round 1. |
| C3 | yes | defect | D143's cap refuses a new generation's keys while the handle holds only an unreachable old generation. A lookup pinned on the installed view when a publish commits keeps that view (`core.cc:516-518`); the old view, its `Generation` and every pack it mapped stay charged until the next `Published` outcome, since an `Unchanged` refresh returns before the free scan (`core.cc:473`), so neither the stale reader's own refresh nor `detent_refresh` releases it. Under concurrent lookups that pin is the common case at a commit, so a store whose read set exceeds about half the cap answers existing keys with the fallback and `DETENT_NO_MEMORY` for a whole publish interval. The test comment "frees the unpinned older views" (`test_reader_switch.cc:725`) states what the code does not do. Introduced by this round: with `SIZE_MAX / 4` ledgers the retention cost memory, not answers. | `probe_retained_cap`, job 261123-014705-863bc2: 44,436 of 60,000 keys `no_memory` at rev 2, unchanged after `detent_refresh`, all read at rev 3 | Run the free scan of zero-sum non-current slots on every `refresh_locked` that holds `refreshing`, `Unchanged` and `Refused` included (or at least in `detent_refresh`), so the stale reader's own refresh frees it. Test: the probe's sequence in `test_reader_switch` (`Pinned` park), with a mutant that keeps the early return. |
| C4 | no | defect | `install_atfork` locks `atfork_mutex` on every `detent_open` (`core.cc:256`), and the fork handlers do not take it. A fork while another thread is inside that lock leaves it held in the child, whose next `detent_open` deadlocks. `pthread_once` had this hazard only around the first open. Narrow window, so not blocking. | `core.cc:214-260` | Read an atomic `atfork_installed` (acquire) before locking; lock only to install. Not by taking `atfork_mutex` in `fork_prepare`: libc may hold its atfork lock there while `pthread_atfork` waits on it. |
| N5 | no | nit | `DETENT_MINIMUM_MAPPING_BYTES` equals one full pack plus its bitmap exactly (`core.cc:58`), but the same ledger is charged first for the page table's `State` and slot table (`lazy_page_access.cc:105`, `271`) and then the segment object, each with an allocator header (`allocator.hh:32`). At the floor no pack whose rounded mapping is 1 MiB can be mapped, so the open succeeds and every lookup in such a pack answers `DETENT_NO_MEMORY`. The docs do not promise one readable pack, only the open refusal. | `core.cc:58`, `lazy_page_access.cc:105`, `199-207`, `271` | Say "below which no pack can be read" or add a fixed page-table allowance to the floor; or keep. |
| N6 | no | nit | `reader_capacity_probe` with `refuses` calls `statuses.at("no_memory")`, which throws (exit 134, not 1) when the default cap refuses with another status. | `reader_capacity_probe.cc:111` | Use `find` or `operator[]`. |

## Not run

- No rerun of the gates, TSan, A16, the soak or `reader_capacity_probe` at a 5M scale; no Linux run
  of the new probe (deterministic, single park, Debug on macOS).
- No probe of C4's deadlock (it needs a fork timed inside a mutex hold); read only.
- Main's merges and the security lens's bounds review (`reviews/reader-s6-security.md`).
