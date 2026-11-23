# reader-s6 review: security

Commit: c1c7498e
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-check of the changes since this lens approved `Detent:d8c0182`: fix round 1 (`Detent:c300e5f`,
`Detent:5957f28`), D143's mapping cap (`Detent:07a77ec`), the merge of main (`Detent:78706fc`) and
the Linux UBSan skip (`Detent:a60e089`). The head `Detent:fef8ff5` differs only in `.campaign`.
Attacker model as before: a local user outside `publisher_uid`, a malformed or hostile store in a
trusted directory, a bad C ABI caller, a forking application. The publisher's own account can
already set every value and raise `SIGBUS` (reader.md Limit 5); a defect counts only beyond that.

Round 3 re-check (2026-11-23): this lens accepted a60e089 at `Detent:16a38990`. The delta
`Detent:16a38990..c1c7498e` adds three non-merge commits on the reader: `Detent:dece1118` (C3),
`Detent:b490591d` (C4) and `Detent:c1c7498e` (N6, probe only). Its other files come from main's merge.
That re-check is the next section; the a60e089 re-check below it stands, since nothing else on the
mapped read path changed.

## Round 3: C3, C4 and the cap's refund (c1c7498e)

Evidence relied on: native 261123-020546-339806 and tsan 261123-020547-63d2a8 (A1-A6, A15, the
new `retained` and `fork_mid_open` cases), A14 261123-020608-eda47a, and the correctness review
(APPROVE at c1c7498e). Line numbers are `core.cc` at c1c7498e.

1. C3: can a view be freed while a reader uses it? No. `free_retired(keep)` (200-211) frees only
   slots other than `keep` whose pin sum is zero. At the start of a refresh `keep` is
   `current % ring_size` (389). At a publish it is the new slot (532), as before.
   - Only the holder of `refreshing` stores `current`, so `current` stays `s` for the whole
     refresh. A lookup uses a view only after its recheck `current == s'` succeeds (`Pin::acquire`,
     331-343). Its recheck runs during this refresh, so `s' = s`, and the slot it pins is the kept
     one.
   - Now take a lookup that pinned an older slot and has not yet failed its recheck. Its
     seq_cst increment comes before its seq_cst load of `current`. If that load still returns the
     old value, the increment also comes before the store of the new value. That store
     happens-before this refresh through `refreshing`, so `sum()` sees the increment and the slot
     is kept. Otherwise the recheck fails and the lookup never loads `view`. This is the Dekker
     pair that already guarded the free at publish. C3 only runs it on every outcome.
   - Pins are taken in `lookup` and `testing::stats`. The fork handlers read views only while
     holding every `refreshing`. `answer` runs pinned, and a lookup releases its pin before it
     calls `refresh_locked` (570-571, 583-585).
2. C4: a fork-time race against isolation, or a mapping leaked into the child? No.
   - `detent_open` registers a handle only after `install_atfork` returned true (643, 678).
     That return follows the release store at 276, which follows `pthread_atfork`, so every
     handle in the registry at fork time has the handlers. The acquire load on the fast path pairs
     with that store.
   - The handlers are unchanged: `fork_prepare` holds `registry_mutex` and every `refreshing` and
     freezes each table; `fork_child` zeroes the pins.
   - `atfork_mutex` is still never taken with `registry_mutex`. No handle's state depends on
     the flag, so a stale `false` costs only a lock.
   - The child inherits every parent mapping, as any fork does; `fork_child` keeps their ledger
     entries consistent, as at d8c0182 (A12/A13).
   - Residual liveness nit (S8). The fix covers the steady state. It does not cover the first
     registration racing a fork.
3. The refund under a hostile store: bounded. A freed view drops its generations, and
   `~PageTable` unmaps each segment and returns its ticket. The ring holds 4 views of at most
   5 generations each, and charges never pass the cap, since admission precedes `mmap`. So the
   synchronous work of one refresh is at most the segments the cap admits.
   - C3 only adds frees: a Refused, Unchanged or Failed outcome now refunds unpinned views and
     builds nothing (the refused check still precedes attach, 391-397). No path charges without a
     matching ticket.
   - A lookup thread that wins `refreshing` can pay for those `munmap`s. That cost already
     existed at publish, and S1 sizes it (about 120,000 one-page packs at the default cap on Linux,
     publisher-only).

## a60e089 re-check

### Evidence I relied on

1. The d8c0182 review (previous version of this file) and its probes e54597 and 2101c5, for what is
   unchanged since: the decode and first-touch code is byte-identical at a60e089 apart from main's
   cursor rename (below).
2. CI 44955216762 at `Detent:07a77ec`, linux-asan-ubsan log, for the cause behind the skip.
3. Mutants for the round-1 fixes, at their own commits: MISS-1 23f97d (C2, transient miss),
   CAP-1 a5d80c and CAP-2 9b81e0 (D143), NOMEM-1 4552ff, ATFORK-1 2a7bcc. The suite gates at
   a60e089 (native 15579c, tsan 4432db, asan a1ad79, t3-tsan bff807, A10 c31340, A11 377d11) were
   queued when I wrote this; they and CI at the head are the landing's gate, not this review's.

### What I checked myself

Scripts under `.scratch/detent/review-reader-s6-security/` (`probe.sh`, `probe.cc`,
`post_touch_fuzz.inc`) and `recheck/rerun.sh`, which rebuilds them at the job's commit and reruns
them unchanged.

1. Question 1, a page changed after first touch. `git diff d8c0182..a60e089` leaves
   `decode_node`, `decode_external`, `nodes_end`, `paged_navigation.hh` `verified_page_bytes` and
   `paged_read_state.cc`'s walk unchanged, except `root.cursor.revision` becoming
   `root.cursor.position` (main's W1 cursor; main made the same rename in its own
   `lazy_page_access.cc`, base..`Detent:005f84b`), which bounds the same 8-byte field
   (`paged_layout.cc` `decode_root` offset 56). Main's root format change (DTROOT04, cursor
   `incarnation`, a zero check at 96..144) reads a fixed 320-byte heap copy, not mapped bytes.
   Rerun at a60e089, job 261122-215147-b9dd1a (`recheck/rerun.sh`, ASan+UBSan), passed with the
   same counts as 2101c5: 20,000 post-touch damages, 92,380 bodies inside the segment, 445 absent,
   387,175 refusals, 0 other exceptions, no sanitizer report. The special-file, `change`-size and
   refresh-fork probes and the hook-symbol check also passed, as they did at e54597.
2. Question 2, the verification-to-use window. The new `catch (const std::system_error &) { throw; }`
   in `resolve` (`lazy_page_access.cc:261`) and in the walk (`paged_read_state.cc:112`) only lets
   errors from `open_mapping` through: `openat`, `fstat` and `mmap` raise them
   (`lazy_page_access.cc:154,157,171`) before `verified_page_bytes` sees any byte, and the mapping
   is unmapped by `~Mapping` on that throw. Nothing on the decode, `verify_page` or first-touch
   path makes a system call or throws `system_error` (grep of `paged_layout.cc`,
   `paged_node_step.hh`, `paged_navigation.hh`, `verified_pages.hh`, `paged_read_state.cc`), and
   `verify_page` wraps every exception as `ReadSourceFailure` (`paged_navigation.hh:45-49`). The
   verified bit is still set only after `verify_page` and the first-touch walk
   (`paged_navigation.hh:50-51`). The window stays closed.
3. Question 3, the merge. I compared the quest's own patch before the merge (base `Detent:4195c4a`
   to `Detent:c6b1315`) with it after (main `Detent:005f84b` to a60e089) for `generation.cc`,
   `lazy_page_access.cc`, `prepared_reader.{cc,hh}`, `paged_read_state.cc`, `paged_layout.cc`,
   `paged_layout.hh` and `generation_file_access.hh`. The only differences are main's renames
   (`position`, `NamespaceCursor`), main's `name` parameter threaded through `read_selected`,
   `selected` and `attach`, main's bound `table_bytes(name.empty() ? 1 : 1 << 20)` for `selected`,
   and the flat-image branch main removed. Every `O_NONBLOCK | O_NOFOLLOW` open,
   `checked_or_unlinked`, `mode_only_acl`, the size-before-allocation check in `read_table`
   (`generation.cc:116`), the lease `flock` and identity checks in `attach`, and the
   `root.segments <= limits.segments` and physical-bytes checks (`prepared_reader.cc:160-170`)
   survive. The core attaches with an empty name, so its `selected` bound stays one entry.
4. Question 4, the UBSan skip. The CI log at `Detent:07a77ec` shows
   `system_error:277: runtime error: member call on address ... which does not point to an object of
   type 'std::error_category'`, "object has invalid vptr", then sanitizer CHECK
   `pthread_getattr_np(...) == 0` and "Can't create a socket pair ... (errno: 24)". errno 24 is
   EMFILE: the test exhausted descriptors, and the sanitizer runtime's own probes (a pipe for its
   memory-accessibility check, `/proc/self/maps` for `pthread_getattr_np`) need one. The object is
   libstdc++'s `generic_category()` singleton, constructed in the reader's
   `std::system_error(errno, std::generic_category(), ...)`, so the report is the sanitizer's, not a
   reader defect. The skip is Linux-only, keyed on UBSan's vptr handler being linked
   (`test_reader_core.cc:23,174-179`); macOS ASan+UBSan and Linux native keep the case, and MISS-1
   23f97d shows the test fails without the fix.
5. D143 by source. Admission is charged before `mmap` (`lazy_page_access.cc:168`); a refused
   ticket is `ReadAdmissionRefused`, leaves `failed` unset and maps to `DETENT_NO_MEMORY`
   (`core.cc:100`). The cap is clamped to `SIZE_MAX` and floored at `DETENT_MINIMUM_MAPPING_BYTES`,
   whose value a `static_assert` ties to the segment and bitmap sizes (`core.cc:58-59`); an older,
   shorter options struct gets the default (`core.cc:629-633`).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | gap | Closed in part by D143. Packs, bitmaps and page tables now share one per-handle cap, 512 MiB by default. Two parts stay open, both reachable only by the publisher's account. First, `change` is still mapped whole and outside the cap. Second, the cap counts bytes, not mappings. Mappings are page-rounded, and a pack may be as small as 79 bytes, so the default fits roughly 120,000 one-page packs on Linux. That is above the default `vm.max_map_count` of 65,530. Main's client path has the same exposure. | `core.cc:123,344-347`; `lazy_page_access.cc:168`, `generation_mapping.hh:63-68`, `paged_layout.hh:16` | Owner's call, as before: (a) keep as ruled in D143; (b) cap `change` at the slots the store addresses; (c) a per-handle mapping count. |
| S3 | no | nit | Unchanged: in-place reading after verification is acceptable (Limit 5). The re-check finds nothing that widens it. | items 1-2 | None. |
| S5 | no | nit | Still open: `read_selected`'s `selected` and image records are checked without `mode_only_acl`, unlike every other record (`prepared_reader.cc:40,125`). The merge kept it as it was. | `generation.cc:106-147` | Add `mode_only_acl` there. |
| S6 | no | nit | `mmap` failure now raises `system_error` for every errno. `openat` does that only for the resource errnos (`transient`). A pack that can never map (ENODEV, EACCES, EINVAL) therefore never sets `failed`. Each lookup that reaches it repeats open, fstat, ACL check and `mmap`, then answers `DETENT_IO`. This is bounded work and publisher-only. | `lazy_page_access.cc:33-36,169-171` | Raise `system_error` only when `transient(errno)` after `mmap`; otherwise `ReadSourceFailure`. |
| S7 | no | nit | The UBSan skip returns before the whole of `transient_miss`, not only its exhausted step as the commit message says. The steps before exhaustion are also covered elsewhere, so nothing is lost on the mapped path. | `test_reader_core.cc:174-179` | Optional: skip only the `setrlimit` part, or reword the message. |
| S8 | no | nit | C4 covers opens after the first registration. The child can still deadlock on its first `detent_open` in one window: another thread holds `atfork_mutex` for the process's first registration, and it forks before the release store at 276. The child then inherits a locked mutex and `atfork_installed == false`. The window is one `pthread_atfork` call per process lifetime, and only liveness is at stake: no handle exists yet to leak. | `core.cc:269-278` | Optional: register from a static initializer or `pthread_once`, after which no open locks; or document it with D10. |

S2 and S4 were fixed in round 1 (`Detent:c300e5f`, `Detent:96a069c`; ATFORK-1 2a7bcc).

## Not run

1. Linux. A pack changed on disk after first touch shows in a reader only there, and the skip's
   effect on linux-asan-ubsan is shown only by CI at the head, which was queued at review time.
2. The suite gates at a60e089 (queued); the landing's gate. At c1c7498e, CI and A7-A13, A16 belong to
   the landing's gate, not this review.
4. No new probe for round 3: its three questions are about memory ordering and lock scope. They are
   answered by source above and by the `retained` and `fork_mid_open` cases in the native and tsan
   jobs.
3. A store with more than 65,530 tiny packs, for S1's map count; argued from the code.
