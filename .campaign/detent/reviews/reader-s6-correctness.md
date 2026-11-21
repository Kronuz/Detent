# reader-s6 review: correctness

Commit: Detent:d8c01825bd9f3dec285a37ea02e5612c3b0f2351
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Gates at `Detent:8200f57` (code-identical to the head): native 261121-083336-07165a, tsan
  261121-083336-7d077b, t3-tsan 261121-083336-1342d5, all exit 0. They carry A1-A6 and A15 and
  the T3 switch under TSan (risk 2's interleavings).
- A8, A10-A14 mutants and runs as listed in the journal's Evidence (D5-1..3, D10-1..3, F1-F4, cold
  dd95f8, soak e7e113); `campaign status reader-s6` shows them passed at `Detent:782374e` with
  only a main merge since. I did not rerun them.
- A16: exclusive 261120-225554-5ee6cc (stopped) and 261120-225556-0233d1 (running) at
  `Detent:45db5f7`, BASELINE main `Detent:cb97ef5`; logs read. `git diff 45db5f7 d8c0182` under
  `src include cmake` touches only authority, relay, delivery, `owner_loop.hh` and `reply_pump`,
  none of which `first_read_phases` (`detent::local_client`) or `first_read_core`
  (`detent::reader`) includes, so the rows stand for the head's timed code. The head reruns
  261121-113842-fb5a53 and -b8c489 were still queued.

## What I checked myself

Read `src/detent_reader/core.cc` whole, the diffs of `lazy_page_access.cc`, `prepared_reader.cc`,
`paged_navigation.hh`, `paged_read_state.cc`, the C and C++ headers, `docs/mechanisms/reader-core.md`,
`docs/perf/first-read-gate.sh`, and the fork and Q6 tests. Probes, rerun with
`bash .scratch/detent/review-reader-s6-correctness/probe.sh NAME` from a checkout (exit 0 is the
documented behavior, 1 the defect):

- `probe_refresh_memo` (job 261121-114518-fe78c5, exit 1): open, hide the selected generation's
  `.read`, first lookup `unavailable`; restore, `detent_refresh` returns `ok`; the next two lookups
  are still `unavailable`; only a publish brings `ok`.
- `probe_transient_miss` (job 261121-114630-3d1d63, exit 1): a 60,000-knob store (2 packs); after
  the first read, descriptors exhausted (`EMFILE`), the first cold-segment lookup returns
  `source_failure`; with descriptors back, that key and the already-mapped `t:k0` both return
  `source_failure`, `detent_refresh` returns `ok` and changes nothing; only a publish recovers.

Risks, answered:

1. Switch and snapshot: a lookup answers from one `Generation` (one root, one `PageTable`) under a
   pin held until `answer` returns (`core.cc:537-542`); segments unmap only in that generation's
   destructor, reached from `free_view` after a zero counter sum (`core.cc:172-183`, `355-366`,
   `500-509`). No mix of pages, no page freed under a pin.
2. Ring: the reader's seq_cst increment and `current` recheck against the publisher's seq_cst
   `current` store and counter scan; `current` is 64-bit and compared whole, so a reused slot fails
   the recheck (no ABA); stripe sharing is additive, so a non-atomic sum cannot read 0 under a held
   pin; the freer never touches the slot `current` names. No logic race found beyond TSan's view.
3. Fork: lock order is `registry_mutex` then each `refreshing`; no path holds `refreshing` while
   taking the mutex or entering `Preparation`, and `publish` never nests a second preparation, so
   prepare cannot wait on itself. `prepare_fork`/`parent_after_fork` are idempotent for a
   generation shared by several views. A verified bit is set only after `verify_page` and the node
   walk (`paged_navigation.hh:43-50`); a segment is reachable only after its release CAS. The child
   sees whole segments or none.
4. Q6 (a): the 200 ms bound holds under contention because `refresh_locked` rechecks
   `store_try_at` under `refreshing` (`core.cc:374-380`), so a loser that wins later does not
   reopen. Error paths: C1, C2 and N1 below.
5. A16: exclusive, interleaved pairs (order flipped per cell), one pinned toolchain checked by
   `check()` and `file`, Release, counters off, each arm on its own tree's store. Branch medians
   50-55 us faster and pooled p99 below main's in all four cells (0.830/0.875, 0.890/1.021,
   1.109/1.139, 1.001/1.109 ms), so the pass does not rest on D142's floor. Sound.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | yes | defect | `detent_refresh` does not clear a failure remembered by an unattached namespace's first attach. With no entry, a forced refresh rebuilds an equal view and returns `Outcome::Unchanged` (`core.cc:453-460`), which leaves `failed_store` set (only a publish clears it, `core.cc:497-499`); the no-entry lookup then returns the memo (`core.cc:552-554`) until the store word moves. With the publisher stopped that is forever, against reader.md D4 ("waits for the sequence to move or for `detent_refresh`") and reader-core.md ("until the store word moves or `detent_refresh` runs"). | probe_refresh_memo, job 261121-114518-fe78c5 | Clear the memo when a forced refresh does not fail (Unchanged or Published). Test: the probe's sequence in `test_reader_core` (`values` or `unreadable_table`), with a mutant that keeps the memo. |
| C2 | no | defect | A transient failure on a cold segment miss (`EMFILE` on `openat`, `mmap` failure) marks the whole generation failed (`lazy_page_access.cc:148-150`, `159-161`, catch-all `251-253`) and reports `DETENT_SOURCE_FAILURE` ("failed verification") rather than `DETENT_IO`. Every later lookup in that generation fails, mapped segments included, and `detent_refresh` cannot recover it, since an unchanged generation id keeps the same `Generation` (`core.cc:431-432`). Pre-existing: the catch-all and the `openat` refusal are on main's `lazy_page_access.cc`; the quest carries them into the new C ABI. | probe_transient_miss, job 261121-114630-3d1d63 | Lead's call, here or a follow-up: throw a `system_error` for `openat`/`fstat`/`mmap` errno failures and exempt it from `failed` (as `StoragePressure` is), so it maps to `DETENT_IO` and the next lookup retries. |
| N1 | no | nit | Lookup-time `bad_alloc` and ledger pressure become `ReadSourceFailure` (`lazy_page_access.cc:247-250`) and so `DETENT_SOURCE_FAILURE` (`core.cc:80-96`), not `DETENT_NO_MEMORY` as reader-core.md "Status mapping" and reader.md line 130 say. The ledgers are unbounded today (`core.cc:630-632`), so only `bad_alloc` reaches it. | `lazy_page_access.cc:247-250` | Rethrow those two as they are, or correct the docs. |
| N2 | no | nit | A first lookup whose refresh is refused by a pinned next slot answers `DETENT_UNAVAILABLE` with the fallback for a knob that exists, and the backoff extends that to later first lookups (`core.cc:558-559`, `570-572`). D5's refusal, rare; reader-core.md step 5 reads as if no first lookup answers a default. | `core.cc:558-572` | Say so in reader-core.md, or keep as is. |
| N3 | no | nit | The grown-`change` path records the entry's `seen` as 0 (`core.cc:395-396`, `448`), so the next lookup refreshes once more. Conservative and correct. | `core.cc:418-429` | None needed. |
| N4 | no | nit | The A16 head reruns keep BASELINE `Detent:cb97ef5` while main is at `Detent:4195c4a`; main's code since is outside the timed binaries, so the comparison is unchanged. | job spec 261121-113842-fb5a53 | None needed. |

## Not run

- No rerun of the gates, mutants, soak or A16; no Linux or sanitizer run of the probes (both are
  deterministic Debug probes on macOS).
- Hostile input and the C ABI's validation: the security lens.
- `prepared_reader.cc`'s attach retry loop beyond the paths the probes and tests reach; the
  format-refusal merge (journal risk question 4).
