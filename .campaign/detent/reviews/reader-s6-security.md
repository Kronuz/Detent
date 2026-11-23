# reader-s6 review: security

Commit: d8c0182
Verdict: APPROVE
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Attacker model: a local user outside `publisher_uid`; a malformed or hostile store inside a trusted
directory (the publisher's account, or a file replaced under it); a caller of the C ABI passing bad
arguments; an application that forks with live handles. The publisher's own account can already set
every value and raise `SIGBUS` by truncating a mapped file (reader.md Limit 5), so a defect counts
only when it exceeds that.

## Evidence I relied on

The reader sources (`src/detent_reader`, `src/detent_local`, `include/detent`, `tests/detent_reader`)
are byte-identical at `Detent:4eeef65`, `Detent:8200f57` and `Detent:d8c0182`; the merge changes
only `include/detent/owner_loop.hh`. So these passing jobs stand at this commit:

1. Gates at `Detent:8200f57`: native 261121-083336-07165a, tsan -7d077b, t3-tsan -1342d5.
2. Relay ASan attribution: 261121-083356-0bfda2 (head) and -083359-311725 (main), both passed.
3. SR1: A10 261120-224435-1187fe (`test_reader_files`, 35 cases), with mutants SR1-selected, -image,
   -change, -record and -pack killed (journal Evidence).
4. SR2: A11 261120-224435-32e96c (`test_reader_core replaced_change`), with mutant SR2 0b8176 killed.
5. Fork-reply timeout (2026-11-05 blocking 2) and fork during preparation (pending 2): A12/A13
   261120-224435-dd95f8 (three cold two-handle cases, child mappings and descriptors at baseline),
   soak 261120-224435-e7e113, mutants F1-F4 killed.
6. Size and accounting stress (pending 3): A14 261120-224435-5e46f2.
7. Post-verification count damage: ASan dense reads 5dd6f7 (`damaged_counts`, 14 values) and
   BOUND-1 bf6dfc killed.

Earlier review's blockers: SR1 closed (every reader open of `change`, `selected`, image, lease,
`.read` and pack is `O_RDONLY | O_NONBLOCK | O_NOFOLLOW`, then fstat-checked: `change_file.cc:112`,
`generation.cc` `read_selected`, `prepared_reader.cc:29`, `lazy_page_access.cc:146`). Blocking 2
closed by item 5. SR2 closed: `ChangeMapping::open` takes device and inode from the descriptor it
maps (`change_file.cc:118-133`), and the forced refresh compares the name against that
(`core.cc:384-386`). Pending 2 and 3 closed by items 5 and 6. Pending 4 (CI) is the landing's gate,
not this review's.

## What I checked myself

Scripts under `.scratch/detent/review-reader-s6-security/` (`probe.sh build|run|fuzz`, `probe.cc`,
`post_touch_fuzz.inc`, `private_map.c`), ASan+UBSan Debug, each case in a forked child under
`alarm()`:

1. 261121-123744-e54597 (`probe.sh run`), passed:
   - Special files test_reader_files does not cover: the store path as a FIFO and an ancestor as a FIFO
     are refused without blocking; `.read` as FIFO, directory, symlink, hard link, 0666 and short gives
     `DETENT_SOURCE_FAILURE` with the fallback; `change` as a bound unix socket is refused at open.
   - Hostile `change` sizes: 0, 4, 12 and 1 TiB + 4 bytes are refused at open; 8 bytes (store word,
     no slot) opens and the lookup is `DETENT_SOURCE_FAILURE`; 16 bytes, 1 TiB sparse and 64 TiB
     sparse map and answer 7, in 84-172 ms.
   - Registered handle with `change`, then `selected`, swapped for a FIFO while one thread loops
     `detent_refresh` and the main thread forks 40 times: no hang, refreshes refused, 40/40 children
     answer 7, and the parent answers 7 after (stale, per D4).
   - Question 5 from the journal: `libdetent_reader.a` has no `reader::testing`, `window_hook` or
     `failure_text` symbol. The hooks are compiled only into `detent_reader_hooked`.
2. 261121-123745-2101c5 (`probe.sh fuzz 20000 24301`), passed. This goes beyond `damaged_counts`:
   after first touch it writes 1-8 random bytes anywhere in the root page (half of them in the
   header and the external table), and the page ends an exact heap allocation. Over 20,000 damages
   and 480,000 lookups: 92,380 bodies, all inside the segment, 445 absent, 387,175
   `ReadSourceFailure`, 0 other exceptions, and no ASan or UBSan report. That answers journal
   question 1: a page changed after first touch gives an in-page read or a refusal
   (`paged_layout.cc` `decode_node`/`decode_external` bound every read by `end <= bytes.size()`;
   `paged_read_state.cc:63` wraps to a refused `end` on any count too large).
3. `private_map.c`, run on this host in seconds: on macOS a `write()` does not show in a
   `MAP_PRIVATE` mapping the reader has already read. So a pack damaged on disk after first touch
   reaches a reader only on Linux. The end-to-end mode `probe fuzz`, which damages the pack file,
   saw no change on this host (job 261121-114723-71a88f, 0 of 960,000 gets). It is only meaningful
   on Linux, and the heap-exact fuzz in item 2 stands in for it here.
4. By source:
   - Fork: `fork_prepare` takes every `refreshing`, then freezes and drains each reachable
     `PageTable`. `prepare_fork` is idempotent when one generation sits in several views. Nothing
     that holds `refreshing` waits on a frozen table. The verified bit is set after verification and
     never cleared, so a child sees either a fully published segment or none. The child releases the
     registry mutex and every flag. The ledgers are lock-free (`storage.hh`), so a child inherits no
     held lock.
   - C ABI: every pointer and length is checked before use (`core.cc:605-720`; argument cases in
     `test_reader_core` under ASan 598ac8). Lookups catch everything into a status.
   - Bounds from store contents: the descriptors are the directory plus one lease per generation
     (at most 5). Generations are at most ring size + 1. The page walk is bounded by key length.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | gap | Mapped and heap bytes per handle are unbounded. Store contents drive them, and main's client path caps them. The core's ledgers are `SIZE_MAX / 4` (`core.cc:630-632`), while main's default mapping account is 512 MiB (`view.cc:36`). One generation may map up to 2^21 slots (`lazy_page_access.cc:30`) × 1 MiB segments, about 2 TiB, each segment its own `mmap`. On Linux that can exhaust `vm.max_map_count` (65,530) for the host process. `change` is mapped whole: 64 TiB sparse maps in 172 ms. Only the publisher's account can create these files. | `core.cc:630`, `view.cc:36`, `lazy_page_access.cc:30`; probe e54597 (sizes) | This is the owner's call (Decision 3, Q5). Options: (a) keep it unbounded, as now; (b) a per-handle mapped-bytes cap, say main's 512 MiB, which refuses with `DETENT_NO_MEMORY`; (c) a cap on the number of mapped segments per handle, aimed at the map count; (d) cap `change` at the slots the store can address. |
| S2 | no | nit | `register_handle` calls `std::abort()` when `pthread_atfork` fails. That needs ENOMEM on the first open, but it is the one abort reachable from the C ABI. | `core.cc:243-246` | Record the failure in the `pthread_once` body and return `DETENT_NO_MEMORY` from `detent_open`. |
| S3 | no | nit | This answers the lead's question: in-place reading of mapped pages after verification is acceptable. Generation files must be owned by the publisher, have one link, no group or other write, and (packs, leases, records, `change`) a mode-only ACL. So only the publisher's account or root can change them, and that account can already set any value or raise `SIGBUS`. Fuzz 2101c5 shows damage gives a bounded read or a refusal. Residual: concurrent writes to a mapping are formally a data race, so a compiler could reload a bounds-checked field (double fetch). The consequence stays within the publisher's existing power. Option (c), private copies, would only add defense against a publisher that later loosens its own modes. | probe 2101c5; `change_file.cc:16-23`, `generation_file_access.hh` `generation_file_refusal` | None required. Keep it as the documented Limit 5. |
| S4 | no | gap | The journal's A9 evidence cites a "Mapped bytes after verification" section in `docs/mechanisms/reader-core.md`. No such section exists at d8c0182, and `git log -S` finds it in no commit. The note also says "six refused shapes at each of the four entry points" and "`PreparedReaderTesting`", but the test covers five names and the hook is `RecordTesting`. | `reader-core.md:75,81`; `test_reader_files.cc:58` | Add the section (reader.md Limit 5, the in-page bound, the Linux-only visibility) or drop the citation. Fix the counts and the name. |
| S5 | no | nit | Pre-existing on main: `read_selected` checks `selected` and the table's image without `mode_only_acl`, while every other reader record requires it. Only the owner can set an ACL, so this is defense in depth. | `generation.cc` `read_selected`; `prepared_reader.cc:41` | Add `mode_only_acl` there, for one rule across all record opens. |

## Not run

1. Linux. A pack changed on disk after first touch shows in a reader only there (item 3), so the
   end-to-end pack-damage fuzz (`probe fuzz`) has no result on Linux. TSan, sanitizers on Linux and
   seccomp are CI's.
2. A store with more than 65,530 mapped segments, to show S1's map-count exhaustion; it is argued
   from the code.
3. ACL manipulation, a character or block device in place of a store file (creating one needs root),
   and NFS or other slow filesystems blocking a refresh while `fork_prepare` waits on it.
4. CI at d8c0182 (running at review time); the landing's gate.
