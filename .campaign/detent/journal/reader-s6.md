# reader-s6: journal

State: ready for review (round 3: C3, C4, N6). Head `Detent:c1c7498` (pushed; journal commits after it not pushed, to keep CI). Done: plain `test_reader_core` 261123-020608-eda47a exit 0; fails without: RETAIN-1 261123-020610-c38819 exit 134, ATFORK-2 261123-020612-66011f exit 134. Waiting on jobs: native 261123-020546-339806, t3-tsan 261123-020547-63d2a8, probe 261123-020615-e1cb4e; CI 45152925089 (linux-asan-ubsan, linux-tsan) and Docs 45152924574 at `Detent:c1c7498`.
Question for the security reviewer (lead, 2026-11-19): "Is in-place reading of mapped pages after verification acceptable given who can write the mapping (file ownership and permissions of generation files)? If not, the fix is option (c), private copies, as its own quest." What the code provides instead is memory safety: any mapped bytes, damaged or not, give a read inside the page or a refusal (`docs/mechanisms/reader-core.md`, "Mapped bytes after verification", added at `Detent:96a069c`).
Risk questions for the reviewers:
- Security: (1) `Detent:09be4f1` places a page's nodes end from its raw external count: can a page changed after first touch (a Linux private mapping shows file writes until copy-on-write) steer any `decode_node`/`decode_external` read outside the page? (2) SR1: does any open of `change`, `selected`, the image, lease or pack files still block on a FIFO or follow a planted symlink? (3) SR2: can a replacement between `mmap` and the identity `fstat` bind a mapping to the wrong file? (4) Fork: can a child see a half-prepared page or inherit a ledger entry for a mapping it lacks? (5) Is `testing::stats` compiled only with `DETENT_READER_TEST_HOOKS`?
- Correctness: (1) Ring and one freer: can a view be freed while a pin holds it (D5's seq_cst reload; STATS-1 58fade shows the hook now pins)? (2) Is a page's verified bit published only after `verify_page` and the first-touch walk complete, and read with acquire? (3) Q6 (a): is the empty-store reopen at most once per 200 ms under 16 concurrent readers? (4) Does the merge of main's format refusal (`Detent:c9eac84`) leave the reader refusing old formats the same way as main? (5) A16's split: does `count` plus a short exclusive `measure` still test D49's bound?
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing
- Fix round 3 (2026-11-23, D146): correctness re-check at `Detent:80cde23` (CHANGES, C3 blocking).
  - C3 `Detent:dece111`: `free_retired(keep)` frees every view outside slot `keep` that no lookup
    pins; `refresh_locked` runs it first on every outcome (Published, Unchanged, Refused, Failed)
    and again after a commit. The next-slot refusal check follows it; `AfterFree` still fires when
    the next slot held a view that was freed. Test `test_reader_core` `retained` (the reviewer's
    probe sequence): cap = 1.5 x one generation's whole charge, a lookup parked at `Pinned` across
    rev 2's commit, then after the pin drops and a `detent_refresh`, all 60,000 rev-2 keys must
    answer their values and one generation stay. Local (macOS Debug): fixed 60,000 of 60,000,
    charged 1,435,136 of 2,152,704; RETAIN-1 15,564 of 60,000, generations 2, exit 134 (the
    reviewer's 44,436 refused, reproduced).
  - C4 `Detent:b490591`: `atfork_installed` is atomic; an open reads it (acquire) and locks only to
    register. New hooked-build window `Registering` at that check. Test `fork_mid_open`: a thread
    parked at `Registering` inside `detent_open`, fork, the child opens and reads under a 10 s
    alarm. Local: fixed child exit 0; ATFORK-2 (lock first, window inside the lock) child signal 14.
  - N6 `Detent:c1c7498`: the probe counts `no_memory` with `find`.
  - Mutants at `refs/mutants/reader-s6/{RETAIN-1,ATFORK-2}`, each `Detent:c1c7498` with only its
    fix removed from `core.cc` (`.scratch/detent/round3/mutate.py`).
- Fix round 2 (2026-11-22, lead): C1, file set, A9's row, CI at `Detent:c6b1315`, t3-asan width.
  - C1 was fixed in round 1 (`Detent:c300e5f`, `refresh_memo`, MEMO-1 b558f2 `--fails-without`
    killed); the probe's sequence is that test's. Confirmed again at the merged head (Fixes since the review).
  - CI 44955216762 at `Detent:07a77ec` (code of `Detent:c6b1315`): linux-asan-ubsan failed
    `test_reader_core`, the rest green, linux-tsan cancelled behind it. Cause: `transient_miss`
    (this quest's C2 test) exhausts descriptors; on Linux UBSan's vtable check
    (`IsAccessibleMemoryRange`) needs a pipe, fails with EMFILE, reports "invalid vptr" on
    `std::generic_category()` in the `system_error` constructor, and the report's own
    `pthread_getattr_np` CHECK aborts ("Can't create a socket pair ... errno: 24"). The reader is
    correct; the test is this quest's. Fix: the test skips its exhausted step, saying so, when
    `__ubsan_handle_dynamic_type_cache_miss` is linked on Linux. Native Linux, macOS native and
    macOS ASan+UBSan keep it.
  - msg-17 (t3-asan at ~974% CPU): `test_reader_switch` reads `DETENT_READER_SWITCH_READERS`
    (default 16). Check change for the lead: t3-asan's ctest prefixed with
    `DETENT_READER_SWITCH_READERS=4`, job `--cpus 4`.
  - Merged main (361 files): conflicts in `generation.cc`, `lazy_page_access.cc`,
    `prepared_reader.{cc,hh}`, `first_read_gate.py`. Main's W1 cursor (`position`), flat-v1
    removal and named table entries (D8) taken; the branch's FIFO-safe opens, unlinked-table
    retry (`read_table` returns nullopt) and `attach` kept, `attach`/`selected` gain main's `name`.
    `first_read_gate.py` stays deleted (the branch retired it for `docs/perf/first-read-gate.sh`).
  - For the security re-check, code since `Detent:d8c0182` on the in-place mapped read path:
    1. `lazy_page_access.cc` segment miss: `openat` resource errnos, `fstat` and `mmap` failures
       raise `system_error` (C2), before any mapping exists; `mmap` failure no longer sets `failed`.
    2. `lazy_page_access.cc` `resolve`: `catch (system_error) { throw; }` ahead of the catch-all, so
       a `system_error` during a page's verification or first-touch walk leaves `failed` unset and
       the page unmarked; the verified bit is still set only after both. To check: no decode or
       verify path over mapped bytes throws `system_error`.
    3. `paged_read_state.cc` `lookup`: the walk over mapped pages rethrows `system_error` as is
       (DETENT_IO) instead of `ReadSourceFailure`.
    4. D143 mapping cap (`core.cc`): one ledger for mappings and page-table allocations; a refusal
       is `ReadAdmissionRefused` (DETENT_NO_MEMORY). Bounds what is mapped, not what is read.
    5. The merge: the first-touch walk's revision bound reads `root.cursor.position` (main's W1
       cursor) in place of `revision`; same check.
    Not on that path: C1's memo clear, `install_atfork`, the open option and its floor
    (`reader.h`/`.hh`), `testing.hh` (stats and the atfork hook, hooked build only).
- Review fix round 1 (2026-11-21, lead): correctness CHANGES, security APPROVE with notes, both at
  `Detent:d8c0182`. Fixed C1, C2, S1, S2, S4 in one batch; S3, S5, N1-N4 left (lead). Fixes and
  their jobs: Fixes since the review.
  - Mutants at `refs/mutants/reader-s6/{MEMO-1,MISS-1,CAP-1,CAP-2,ATFORK-1,NOMEM-1}`, each its fix
    head minus that fix; CAP-1, CAP-2 and NOMEM-1 at `Detent:07a77ec`, since `capped` changed. The
    old CAP-1 91d1e5 was killed (exit 134) before D143; old NOMEM-1 f1d342 cancelled.
  - D143 at `Detent:07a77ec` (owner, Q5 (a)): reruns of everything at `Detent:5957f28`: probes
    778bbf 467e2e, asan db1e45, t3-asan cab068, 251480, A7 8b8f2e, dense ASan 96138b, A10 ba1861,
    A11 aff2cc, A12/A13 e66178, soak ac5058, A14 1dd2b9, A16 exclusive 39c857 1b45ad, counts
    8dc6f3 b20d9d, f8389b→44e871, 6cdc1f→008a38; gates f01043 5021f2 4c4f68 (`campaign verify`).
    The 5M probes 3eb46b/a46cfb never ran (cancelled); `tests/detent_reader/reader_capacity_probe.cc`
    replaces their scratch probe and exits 1 unless the raised cap reads every key.
    `docs/READER_CAPACITY_ARCHITECTURE.md` names no reader core, so it is unchanged.
  - Done at `Detent:07a77ec`: plain ae1873 exit 0 (`t:k10` no_memory at 379,880 charged; raised
    cap reads every key at 1,435,184). Killed, exit 134: CAP-1 a5d80c (line 197, limit not
    512 MiB), CAP-2 9b81e0 (line 200, minimum - 1 opened), NOMEM-1 4552ff (line 208, `t:k10`
    source_failure).
  - Stale evidence rerun at `Detent:96a069c`: asan 98673d, t3-asan 016ccc, 0f6416→cc8a4f,
    A7 7af780, dense ASan 136428, A10 c2a20c, A11 ab36b9, A12/A13 ef3805, soak 335e22, A14 84e679;
    A16 (the fix touches the cold-miss path): exclusive 79f55a (stopped), 2a46f5 (running),
    counts 8870f6, 150e4e. CI 44952617120 at the head; 44925224917 at `Detent:8200f57` still runs
    (the relay ASan attribution's Linux check), left alone.
  - Reruns moved to `Detent:5957f28` (N1 touches `lazy_page_access.cc`): probes 14df94 ed843e,
    asan eedf6e, t3-asan 7471fd, 432553, A7 134472, dense ASan 10c10f, A10 cca438, A11 3216f4,
    A12/A13 0d3d32, soak fca7ec, A14 719374, A16 exclusive 75f7c2 (stopped) 1bf4b0 (running),
    counts 660e6d ddca74; gates f8813d 4b053e 8a8b0e.
  - Lead's 5M question (S1 cap at 512 MiB): the cap refuses, it does not evict, and so does main.
    Main `Detent:13148dc`: `view.cc:31-32` default account 512 MiB, `:47` used when
    `ViewLimits::mapping_account` is unset (the library client, `client_source.cc:158`);
    `lazy_page_access.cc:129` maps through `SegmentMapping`, which admits at
    `segment_mapping.cc:28`; the `StoragePressure` becomes `ReadSourceFailure` at
    `lazy_page_access.cc:223-224`. No eviction anywhere in the page access. Main's 5M runs set the
    account: `tests/detent_system/presets/capacity-5000000.json` has `mapping_bytes` 3 GiB
    (`managed_host.cc:153-154`, `reader_capacity.cc:60`); delivery's default is 512 MiB
    (`configuration.hh:9`). So main's default client has the same 5M problem; the reader core,
    with no capacity option, cannot take the 3 GiB the preset gives the agent's readers.
    Probes 3eb46b (2M records) and a46cfb (600K) read every key with the core at default bounds and
    the client view at default `ViewLimits`, and give mapped bytes per record for 5M.
  - (Superseded by N1 above.) S1 makes N1 reachable. A lookup refused by the mapped-bytes cap answers
    `DETENT_SOURCE_FAILURE` (the page access reports admission as `ReadSourceFailure`), where
    reader.md D3 says `DETENT_NO_MEMORY`; reader-core.md "Bounds" states what the code does. A
    reader-only fix is a `ReadSourceFailure` subclass for admission that `classify` maps to
    `DETENT_NO_MEMORY` (client and delivery unchanged). Also: reader.md D3 sizes the 5M store at
    1.51 GB mapped, above the 512 MiB cap; main's client default has the same cap.
- Coordinator (2026-11-21): gates at `Detent:283b583` passed (native f65acc, tsan 2603c5,
  t3-tsan 9e75c5). CI 44910311745 there: linux-asan-ubsan failed `test_relay_service`, linux-tsan
  and macos-x86_64 cancelled; the rest green.
  - The failure is no sanitizer report: `test_relay_service.py:786`, "relay C failed a sync on a
    source that refused" (`sync_failures` 1, `syncs` 1, `ready` 1, 4 files fetched). The relay's
    health does not print `state.error`, so the cause is not in the log. Not in 8 other recent
    failed CI runs (dense-distribution-build, loop-idle-waits, journal-idle-groups,
    reader-s6 d0bdc01).
  - The relay's sync uses `detent_local_view`, where the branch changes `generation.cc`,
    `paged_layout.cc`, `change_file.cc` and the lazy page access, so the branch is not excluded by
    the code. Attribution: ASan+UBSan x20 on macOS at the merged head 0bfda2 and main 311725. If
    neither reproduces, CI 44925224917 at the head is the Linux check.
  - Merged main at `Detent:8200f57` (loop-idle-waits and 29 more files, clean). A16's rows stand:
    the merge changes `owner_loop.hh`, `reply_pump.*`, `detent_relay/*`, `detent_authority/service.cc`,
    `detent_delivery/coordinator.cc`, bench and tests; the timed binaries link only
    `detent::local_client` (`detent_local/client_source.cc` over `detent_local_view`) and
    `detent::reader`, and none of those sources or `docs/perf/first-read-gate.sh` changed.
    Gates forced at the head (`campaign verify --force`, per the coordinator).
- Resumed 2026-11-21 (lead: jobs idle). Head `Detent:7bcab3a`; its code equals `Detent:4eeef65`
  plus docs/perf/first-read-gate.{md,sh} (D142); main `Detent:cb97ef5` has no code change since
  the merge base. All jobs at `Detent:4eeef65` passed: native ef4078, tsan cafe29, t3-tsan 74eeff,
  asan 598ac8, t3-asan 93c07d, reruns 0f6416 5dd6f7 cccaca dd95f8 32e96c 1187fe 5e46f2 e7e113,
  count 3cc4bb. Gates rerun at the head (lead): f65acc, 2603c5, 9e75c5. CI 44876979057 was
  cancelled; 44910311745 dispatched at the head.
  - A16 exclusive at `Detent:45db5f7` (same code as the head), BASELINE main `Detent:cb97ef5`,
    6 runs x 200 reads per arm per cell: 5ee6cc (stopped) and 0233d1 (running) exit 0. Per cell,
    paired median difference / median floor; branch / main pooled p99; main's run-p99 spread
    (D142 floor):

    | Cell | Paired diff / floor | Branch p99 | Main p99 | Main p99 spread | Bound |
    |---|---|---|---|---|---|
    | stopped, 1 ns | -50.1 / 38.1 us | 0.830 ms | 0.875 ms | 0.186 ms | 1.061 ms |
    | stopped, 1,000 ns | -54.4 / 34.3 us | 0.890 ms | 1.021 ms | 0.283 ms | 1.304 ms |
    | running, 1 ns | -50.4 / 43.8 us | 1.109 ms | 1.139 ms | 0.279 ms | 1.418 ms |
    | running, 1,000 ns | -54.5 / 26.4 us | 1.001 ms | 1.109 ms | 0.380 ms | 1.489 ms |

    The pass does not rest on the floor's size: in every cell the branch's pooled p99 is below
    main's, and its median 50-55 us faster. One branch run (stopped 1 ns, run 2) had a 1.486 ms
    p99 with 7 reads over 1 ms; main's runs reach 1.27 ms in the running cells, so it is this
    host's tail. Same shape as the dc004dc exclusive pair (9f40bf/5fb83e). Not code, no fix.
- Lead's D142 (2026-11-20): `Detent:45db5f7` gates p99 on main's pooled p99 plus main's run p99
  spread in the cell (max minus min); the paired difference keeps the median floor; timed runs are
  exclusive. Strict bd2b1e/256548 cancelled; exclusive 5ee6cc and 0233d1 queued. Main merged at
  `Detent:782374e` (campaign files only).
  - On the d0bdc01 strict numbers D142 passes stopped 1 ns (1.162 against 1.135 + main's spread)
    and fails only the loaded running 1 ns cell; the exclusive runs decide.
  - ASan `test_local_paged_disk_store`: head c9dd6e and main ac4fc8 each passed 40 of 40 on macOS,
    so it does not reproduce here on either.
    The CI failure is Linux ASan only so far; the head's CI decides whether it recurs. Per the lead:
    if main fails too, it is pre-existing and goes to the file's owner quest; if only the branch, mine.
- Resumed 2026-11-20 (lead: jobs at `Detent:d0bdc01` done). Passed there: native 5054f8, tsan
  2c8619, asan e0f54b, t3-asan 952dba, A7 fbc572, dense ASan 6cf066, A12/A13 630328, A10 d86533,
  A11 67abda, A14 0e51eb, soak 69ff23, count bc7996, `unreadable_table` d5b8ee. CI 44834111994: both
  Linux gcc arches green (T3 fixed), linux-asan-ubsan red (below).
  - t3-tsan f5e222 failed repeat 11 of 20: the cold-fork child died by SIGALRM (signal 14, its 5 s
    hang guard) while the host was loaded (that repeat 395 s against 190-380 s). `Detent:160dade`
    gives the child 30 s (the mode 60 s) and prints its step on the alarm, so a real hang names it.
  - CI linux-asan-ubsan at d0bdc01: `test_local_paged_disk_store` aborted, "invalid paged disk
    publication accepted" (a `rejects` saw no throw). Not in the file set; scenario 8's
    mapping-pressure admit races asynchronous mapping releases (its own 10 s wait loop below it). It
    passed at dc004dc, e77b31d and 2dae6ab. Attribution: 40 ASan runs at head c9dd6e and at main
    ac4fc8.
  - A16 strict at d0bdc01 (D140): 29d977 (running) and 481c35 (stopped) exited 1. Paired difference
    passes in all 4 cells (-109, -156, -46, -47 us). p99 fails 2 cells: running 1 ns, branch 29.5 ms
    against main 4.6 ms (both arms had 2-108 ms run p99s and medians of 1.4 ms, against 0.6 ms
    exclusive: the strict hold shared the host with light jobs); stopped 1 ns, branch 1.162 ms
    against main 1.135 + floor 0.007 ms. Requeued as bd2b1e and 256548 at the merge.
- Lead's message (2026-11-20, D140): main merged at `Detent:13f15b7` (campaign files only).
  `Detent:f3f76e4` gates A16 on D140: per cell, the paired difference at most the noise floor and the
  branch's pooled p99 at most main's plus the floor; 1 ms is reported only. The gate runs at strict
  (script header and `docs/perf/first-read-gate.md` say so).
- Linux gcc CI failure, found while getting CI onto the head: CI at `Detent:dc004dc` failed T3 on
  both gcc arches (and arm64 gcc at `Detent:ae09997`): readers' `detent_get` returned non-OK
  (`test_reader_switch` lines 93 and 245). Clang Linux and macOS passed; at `Detent:2dae6ab` and
  `Detent:e77b31d` it passed, so it is intermittent. Diff 2dae6ab..dc004dc has nothing in the reader.
  - Cause (from the code): `refresh_locked` swallowed `GenerationUnavailable` from `selected()` (three
    reads a publish storm can outrun) and built a view with no table and no entry, dropping an
    attached namespace; every lookup then answered `DETENT_UNAVAILABLE` until a read succeeded,
    against D4's stale answer. Pre-existing on the branch since `Detent:8f6da7f`.
  - `Detent:0717b9b` prints a refused lookup's status in T3; `Detent:d0bdc01` rethrows when an
    entry is attached, so the refresh fails at its store word and lookups answer stale.
  - Guard: `test_reader_core unreadable_table` (attached, publish, `selected` renamed away: lookup
    and revision answer 1, refresh unavailable, then 2 after it returns). Mutant DROP-1
    (`refs/mutants/reader-s6/DROP-1`, `Detent:1136d31`) removes the rethrow: killed, acc366 (exit
    134); plain d5b8ee passed.
  - Linux gcc confirmation is CI's at `Detent:d0bdc01` (the failure was 3 of 6 gcc runs before).
- Close-out (2026-11-20): merged main at `Detent:1920bd0` (clean; main's src changes are the
  detentd rename, PackScrub counts and test fixtures, none in the read path). The W1 cursor work from
  dense-distribution-build has not landed yet; merge again if it does.
  - At `Detent:dc004dc`: t3-tsan f8389b passed (20 repeats; the line-649 child failure did not
    recur), count 7acc18, dense ASan edcde5 and soak 3cdb4e passed.
  - A16 exclusive 9f40bf (stopped) and 5fb83e (running), BASELINE `Detent:8cd932c`, exited 1. The
    paired difference passes in all 4 cells (median -51.5, -12.3, -16.0, -31.5 us against floors
    67-80 us); the pooled p99 below 1 ms fails in 3 cells for both arms: branch / main pooled p99
    1 ns stopped 0.823 / 0.811 ms, 1,000 ns stopped 1.032 / 1.175, 1 ns running 1.151 / 1.319,
    1,000 ns running 1.137 / 1.212 ms. Main fails the same bound by more, so it is this host's
    tail, not the branch.
  - Requeued A16 at strict (lead, D135) against current main `Detent:6ae09ee`: 921c21, 9c14ff;
    count e021a9 (light).
- A16 attribution (2026-11-19, lead's step 1-4):
  - Phase: the first `detent_get`, not `detent_open`. In 4a5487/2e857f the branch's lookup phase is
    306-311 us (stopped) and 352-383 us (running) with 180-235 us system time, against main's lookup
    of 161-229 us; open is 390-427 us against main's runtime+attach+thread of 515-540 us. The
    branch's first `detent_get` runs `refresh_locked`, which read `selected` twice: once in
    `PreparedReader::selected` and again in `PreparedReader::attach`, after `detent_open` had read it
    once already. Main's Client reads it once. Each `read_selected` is two `openat`s (table and
    image), fstats, two `pread`s and a table decode.
  - Fixes: `Detent:c027f04` attach starts from the table the refresh read (re-reads only on a
    retry); `Detent:65afbd0` a first attach at the installed view's store word and `change` takes
    the view's table and the sequence loaded before it (D4's own rule; `detent_refresh` always
    reads); `Detent:2dae6ab` stats the store path only when the trust walk fails.
  - A/B db0806 (strict, beside 5 light holds; 6 rotated rounds x 200 fresh first reads; branch
    stores at main's path depth, `.scratch/a16/ab.py`), median us and paired difference against
    e77b31d: 1 namespace e77b31d 688.6, c027f04 638.5 (-47.8), 65afbd0 612.8 (-72.4), 2dae6ab 604.3
    (-73.3), main 652.4 (-29.8); 1,000 namespaces 725.4, 687.0 (-49.9), 652.2 (-82.7), 659.8 (-56.4),
    main 712.2 (-24.2). Lookup phase 298 to 222 us (1 ns) and 324 to 251 us (1,000 ns). The stat
    change is within noise (kept: one path lookup less, same statuses). The head is about 50 us
    faster than main in both cells, so nothing is inherent: Blocked on gains no item.
  - Guard: `test_reader_core selected_reads` (`Detent:36f8722`) counts `selected` reads: 1 at open,
    0 more at the first lookup, 1 per switch, 1 when a publish lands between open and first lookup.
    Mutants SEL-1 (attach ignores the refresh's table) 48b91f and SEL-2 (first attach re-reads)
    6f99e1, `.scratch/mutants-sel.json`.
  - Merged main at `Detent:10020fa` (clean). A16 keeps BASELINE `Detent:8cd932c`: main's read path
    is unchanged since (`git diff 8cd932c 3d0571cd -- src include`: authority service, disk_store
    and store_core only).
- Rebench 27840a (strict, 6 interleaved pairs, 1 thread, e77b31d against b1d281c), median p50 /
  p99 ns: core hot 402.0 to 460.5 (+58.5) / 481.5 to 574.5; core scattered 648.5 to 697.0 (+48.5) /
  1052 to 884; pinned hot 406.5 to 462.5 (+56) / 447 to 520.5; pinned scattered 688 to 701 (+13) /
  1110 to 923. The hot gap is the out-of-line node codec, which the lead's ruling sends to
  paged-decode-one-shape; scattered is +13 to +49 ns over 659.
- Results recorded 2026-11-19 (resumed after the lead's msg-1 ruling, option b):
  - `Detent:e77b31d` reapplies `Detent:09be4f1` (raw external count places the nodes end) and
    replaces mutation 3 with `damaged_counts`: the root page ends an exact heap allocation; 14 values
    of the node count (offset 14) and external count (offset 12), in and out of bounds, each after
    first touch; every lookup of all 1,001 rows returns a body inside the segment or a
    `ReadSourceFailure`. ASan+UBSan 817ba5 passed: 6,406 results, 7,608 refusals.
  - Mutant BOUND-1 (decode_external drops `require(index < externals)`) killed under ASan, bf6dfc:
    `heap-buffer-overflow paged_layout.cc:66 in decode_child` (external count 0 puts the entry past
    the page). HDR-2 is now the code, so it is no longer a mutant.
  - native f68cf2 at `Detent:2fb2a41` (header decode still in lookup) passed.
- Results recorded 2026-11-19:
  - msg-1 bench dd2b26 (exclusive, `Detent:518085f`, no header decode in lookup), 1 thread median
    p50: core hot 467.5 (reps 467 463 485 468), scattered 711.5 (reps 719 702 704 729); aa9860 at
    `Detent:18adf2b` 526 and 805; before 393 and 659. The decode is 59 ns hot and 94 ns scattered.
  - Profile baceeb (light, `Detent:b1d281c` against `Detent:18adf2b`, hot 1 thread), self time as ns
    per call (share times p50), core: `decode_node` +42 (0 before: the old lookup walked nodes
    inline), `decode_page_header` +28, `validate` (header and reference) +22, `verified_page_bytes`
    hit compare +27, `lookup` self +25, `decode_external` +9 and `slot` +6, `decode_definition_header`
    +7; offsets: `decode_reference` -22, `PageTable::State::resolve` -16 and `find` -11. Pinned
    matches within 3 ns. So the +133 ns is: header decode and its validate about 50-59, the shared
    out-of-line node codec (`decode_node`, `slot`, more `lookup` self) about 70, and the hit compare
    with reference validation net about +20 against the old resolve.
  - `Detent:09be4f1` (raw nodes end) broke native 132c68: `test_dense_reads` mutation 3 sets a
    verified page's node-count high byte (`root[14] = 0xff`) after first touch and requires the next
    lookup to refuse it; only `validate(PageHeader)` does. Reverted at `Detent:fa32973`.
  - A16 under the old gate at `Detent:518085f`: count 842f21 passed (no branch failure; main refused
    1 read in 2 cells, light, so its timings are not evidence). Breakdown 0992ec (exclusive,
    running): 1 namespace +34 us against a 56 us floor; 1,000 namespaces +72.6 us against a 53 us
    floor (branch medians 720-742 us, main 614-721 us), so it would fail D121 too.
  - t3-tsan 279a47 (`Detent:518085f`) failed the third repeat at `test_reader_switch.cc:649`: the
    cold-fork child exited nonzero with no message of its own (T3 itself held 4 of 4). t3-asan
    9c507c and tsan b41c94 passed. `Detent:2fb2a41` prints the child's status, exit and signal.
  - D121 (owner): `Detent:74a0913` gates each cell on the median paired difference at most the noise
    floor and the p99 of the cell's 1,200 pooled branch reads below 1 ms; 200/200 is printed only.
- Results recorded 2026-11-18 (resumed session):
  - Line-275 attribution, proven: the STATS-1 run symbolized (473f09) ends with descriptors 6 of 4,
    the two extra both `/dev/ptmx`, which is the pseudo-terminal TSan's macOS symbolizer runs `atos`
    on to symbolize the race reports. The same build with `symbolize=0` (fe4089) ends 4 of 4; head
    under TSan (ab35b3, no race, so no symbolizer) 4 of 4; head native (f94397) 3 of 3. No leak.
  - TSan race: `Detent:69ea114` introduced it (A14's `change_bytes` stat reads `view->change`
    unpinned). TSan at `Detent:b1d281c` 01ef48 exit 0; at `Detent:32e65d8` a39a36 exit 8 with the
    race at `core.cc:724` in `testing::stats`; only `Detent:69ea114` touches `change_bytes` between.
    Fixed at `Detent:c471f2f`; STATS-1 58fade killed (exit 8, race at `core.cc:725`).
  - msg-1 A/B b9981e (strict, 6 interleaved pairs, beside 7 light holds), HDR-2 (raw external count,
    no header decode) against `Detent:18adf2b`, median p50 ns: core hot 759 to 658.5 (-100.5),
    pinned hot 768 to 649 (-119), core scattered 1300 to 1075.5 (-224.5), pinned scattered 1315.5
    to 1075 (-240.5). The decode is 13-15% of a hot lookup and 17-18% of a scattered one. Scaled to
    aa9860's exclusive numbers (core hot 1 thread 526, scattered 805; before 393 and 659), removing
    it predicts about 455 hot and 665 scattered: scattered recovered, hot still about 60 ns (16%)
    over. `Detent:09be4f1` makes HDR-2 the code; dd2b26 runs aa9860's command at the head.
  - Profile ea0609 failed because `prof.sh` returned the status of `wait` on the bench it killed
    (143), not a missing path: the first sample (`before-core`) was written. `prof.sh` now checks the
    sample file instead; resubmitted as baceeb. It attributes the remaining hot gap at `Detent:18adf2b`.
  - A16 stopped 369849 (exclusive, `Detent:751b648`) failed both cells, and main misses too: branch
    below 1 ms per run 200 195 200 198 200 199 and 200 200 200 200 199 198; main 199 200 199 199 199
    200 and 198 200 200 200 199 200; main failed 0. Paired median difference +27 us (1 namespace) and +45 us
    (1,000 namespaces), under the noise floors 52 and 53 us. Every slow read, either arm, slows in every
    phase at once with faults unchanged (branch open 300 to 590-875 us and lookup 240 to 410-570 us,
    user and system both; main attach 340 to 630-1,180 us): the process was slowed, not a code path.
  - Lead (owner-approved): A16 split. `Detent:518085f` adds `first-read-gate.sh count` (light: fails
    only on a branch failure, totals main's refusals): 842f21. The exclusive `measure ... running`
    keeps the timing and slow-read breakdown (10 min): 0992ec. Submitting 0992ec superseded e69c8b.
- Lead reorder (2026-11-18): TSan race, then A16, then msg-1.
- TSan (3c4a1b, ea7399; both warnings are the same pair). Read of 8 bytes by T18, the test's
  stats thread (`switching`, `test_reader_switch.cc:695`), in `testing::stats` (`core.cc:724`:
  `view->change` through a view loaded relaxed from the ring, unpinned). Previous write by a
  lookup thread (T5, T13) in `refresh_locked` (`core.cc:402`: `built->change = change`), under
  `detent_get` (`core.cc:654`), in the 48-byte `View` that thread allocated at `:401`. The reader
  introduced it: `git log -S change_bytes` names only `Detent:69ea114` (A14's stats field). It is
  a test-hook read (`DETENT_READER_TEST_HOOKS`), but unpinned it could also read a freed view.
  Jobs 01ef48 (`Detent:b1d281c`) and a39a36 (`Detent:32e65d8`) run the same TSan test to confirm
  it. Fix `Detent:c471f2f`: `stats` pins the view as a lookup does (`Pin`, `acquire`). Mutant
  STATS-1 (the unpinned relaxed read back; `.scratch/mutants-tsan.json`) is 63fdce, run
  `--fails-without`. TSan's "invariant at line 275" (T3's threads, descriptors and store mappings
  back to baseline after close) failed in the same runs. It is unexplained until a run shows the
  cause; the lead will not accept it as tooling noise without one. `Detent:751b648`: T3 prints the
  counts at baseline and at the end, and names each descriptor open at the end but not at baseline
  (path or type). `.scratch/t3/t3.sh` runs `test_reader_switch switch` and passes when the invariant
  holds: STATS-1 symbolized 473f09, STATS-1 `TSAN_OPTIONS=symbolize=0` fe4089, head TSan ab35b3,
  head without TSan f94397. If symbolize=0 holds where symbolized fails, it is the symbolizer's
  pipe. Otherwise the named descriptor is a leak, and I fix it.
- A16: each arm already reads a store its own code wrote. `prepare` generates `main/store-*` with
  main's `first_read_phases` and `branch/store-*` with the branch's, and each publisher is its own
  arm's binary, so there is no format mismatch. Main's arm exits on "invalid local generation
  descriptor": main's `check_generation_file` (`generation_file_access.hh:17-23` at
  `Detent:8cd932c`) requires `st_nlink == 1`, and `read_generation_record` refuses a short read, so
  a generation file the running publisher collects under main's Client is refused. The branch
  reads those again (`Detent:47470ea`, `checked_or_unlinked`). `Detent:ebaa638`: the gate counts
  main's failed reads per run instead of stopping, and prints `phase_medians` (wall, CPU, faults)
  and every read at or over 1 ms (`slow`), to attribute the branch's misses. Reruns c7f32f
  (stopped) and 28641a (running). In b69551 and da956e the branch's medians were 551-761 us and
  main's 531-670 us; open was 306-393 us and the first lookup 240-377 us.
- msg-1, second round (lead, 2026-11-18): (1) profile core hot t1 at `Detent:18adf2b` and
  `Detent:b1d281c`, ns per frame; (2) why the scratch A/B overstated the gain; (3) fix what is
  avoidable on the hit path, rerun aa9860's exclusive command; inherent checks go to Blocked on.
  - Profile 261118-184532-ea0609 (light, `sample` 8 s of hot t1, core and pinned, both arms; the
    bench jobs' own binaries and stores, copied to `.scratch/prof2/`, run by `prof2/run.sh`).
  - A/B 261118-184707-b9981e (strict, 6 interleaved pairs, one store): HDR-2 (lookup reads the
    externals raw, no header decode) against `Detent:18adf2b`, `.scratch/ab2/`. It prices the
    header decode the fix kept.
  - (2), from the code: the scratch patch (`.scratch/exp/src`) replaced the header decode inside
    `decode_node` and `decode_external` with one `read_integer` and decoded no header at all; the
    real fix still runs one full `decode_page_header` per reached page in `lookup` (magic, zero
    bytes, four `read_integer`, a 32-byte digest copy, `validate(PageHeader)`). The A/B also ran
    strict on shared cores (pair 1 core hot: 711 head, 530 patched). b9981e measures the gap.
- `Detent:3e6b09c`: `test_dense_reads.cc:282` constructs `LazyPageAccess` without the factory
  (lead granted it). Build-all 261118-144659-de07eb: 528 of 528 targets built.
- `Detent:18adf2b`: `touched` flips the mapped byte through a writable private copy of its page
  (`mprotect`); a macOS private mapping does not show a file write after first touch (probe
  837cb3 failed on that precondition). Build-all and `touched` 261118-145947-360816 passed.
- `Detent:612f2d0` (lead granted the file): `test_foreign_generation.cc` constructs
  `LazyPageAccess` without the factory; this broke every check build at `Detent:32e65d8`.
- msg-1 fix, `Detent:73c0b6c` (lead granted the four codec files): `decode_node` and
  `decode_external` take the nodes end of a header the caller decoded (`paged::nodes_end(h)`);
  the `Limits` overloads stay as wrappers for the walkers outside the file set. `lookup` decodes
  each reached page's header once; `verify_page_structure` and the first-touch walk pass the end
  they hold. Guard test: `test_lazy_page_bitmap touched` damages a first-touched page through
  the file, checks the mapping shows it, and requires the next `definition()` to be refused, at
  the logical digest (hit compare) and at the magic (lookup's header decode). Mutants HDR-1 (hit
  compare gone) adf29d and HDR-2 (lookup takes the externals raw, no header decode) 7b363f,
  `.scratch/mutants-hdr.json`.
- A14 failure (0206b9 at `Detent:bd6581b`): the cause is the test, not the reader. `capacity`
  grows `change` to 1 GiB, then publishes 40 revisions with the same `GenerationWriter`. The
  agent's census counts `change` at its logical size (`agent_store.cc` `resize_change`), and the
  writer's `DiskLimits.bytes` is 1024 segments = 1 GiB, so the next publication's
  `DiskLedger::reserve` throws `DiskPressure`. The reader reserves nothing in that ledger; the
  three sizes passed. Fix: the ledger stress runs on its own store (`capacity-ledgers`).
- `Detent:bd6581b` ports the first-read gate (lead ruling on A16): `first_read_core --core` times a
  fresh process's `detent_open` and first `detent_get`; main's arm is `first_read_phases --client`
  (runtime, `PreparedReader::open`, refresh thread, first `try_get`), built from main's tree.
  `--generate` builds each tree's own store (10,000 int knobs `n<i % N>:k<i>` = 3i + 1, N = 1 and
  1,000) inside the job under `.scratch/first-read`; `--publish` is the running publisher (a
  one-knob delta to `n0` every 20 ms, collected every 16). `measure` runs six interleaved
  main/branch pairs per cell, 200 reads per run, and fails unless every branch run is 200/200
  below 1 ms; it prints each arm's medians, p99s, the paired difference and the noise floor.
- Jobs at `Detent:8ccef6c`: A10 76dd6c and A11 62b83d passed; the rest were cancelled when the
  head moved and resubmitted at `Detent:bd6581b`. A7 2f0400 failed in 1 s: the slot had no
  `build-reader` (it was reset), and the command did not configure; 1bf0d7 configures first.
- Q6-1 62fc42 survived because of a harness bug, not a code gap: `mutant.py` read each edit from
  the base, so with two edits to `core.cc` only the last one landed. The `remembered` gate was
  removed but `refresh_locked`'s 200 ms check was left in place. `mutant.py` now chains the edits
  to one path, and Q6-1 at `daff3f2` removes both (90e1ea). No other mutant edits one path twice.


- `Detent:470a127` merged main; `Detent:4fafed3` fixes SR1 and FIX-2 in `read_selected`
  (`generation.cc`, granted by the lead): non-blocking opens of `selected`, its image and the
  store's other record opens; an unlinked table or image returns nullopt and is read again
  (`checked_or_unlinked`, one fstat, now in `generation_file_access.hh` and shared with
  `prepared_reader.cc`). `test_reader_files` adds `.image` (35 cases). The record-open hook
  (`RecordTesting`) moved with it and fires for `selected`, so `replaced_selected` is FIX-2's
  deterministic regression again.
- DES-7 done: `paged_navigation.hh` `verified_page_bytes` is the one page check (hit compare;
  decode and compare; commitment; caller's first-touch checks; bit). `checked_page_bytes` and
  `LazyPageAccess::resolve` both call it.
- Probe 261118-094346-d77f12 builds the reader targets and runs core, files, syscalls, lazy-page,
  change-file and switch parks/forks/cold.
- Mutants (`.scratch/mutants.json`, `.scratch/mutant.py` make `mutant/reader-s6/<name>` commits on
  `Detent:4fafed3`), each `--fails-without`, after the probe:
  D5-1 9b4bb9, D5-2 4e137b, D5-3 f5658e, D10-1 855a2e, D10-2 347eee, D10-3 4baa90, D2-1 34bd94,
  D4-1 363cb7, Q6-1 62fc42, FIX-1 7ae4a1, FIX-2 d7fb02 (A8); SR1 selected 862642, image cc96ed,
  change 9d6c47, record ea240d, pack 4d9871 (A10); SR2 ef5614 (A11); freeze F1 998758 (no recheck
  after the count), F2 eb96b2 (no drain), F3 17a24b (child keeps counts), F4 82cbad (prepare
  freezes no table) (A12). All ids are `261118-0945xx-<suffix>`.

- `Detent:69ea114`: A14 stress as `test_reader_switch capacity` (`change` at 1 MiB, 64 MiB,
  1 GiB maps whole, answers in < 2 s, mappings and descriptors back to baseline; heap and mapped
  ledgers after 40 publishes equal their values after 10; values printed). A13: the cold-fork child
  now requires each inherited handle's mapped ledger to equal a fresh handle's for the same read.
  Stats gain `heap_bytes`, `mapped_bytes`, `change_bytes` (hooked build only).
- A7 job 2f0400 (head, after the probe). msg-1: `reader_bench` exclusive, 200K knobs, int hot and
  scattered, core and pinned, 1 and 8 threads, 4 reps x 1M calls: before 0f6566 at
  `Detent:b1d281c`, after f07232 at `Detent:69ea114`. The exclusive cap is 10 min, not 25.

## Next

1. Read the jobs in State; refill Evidence from `campaign brief` at `Detent:a60e089`; CI green.
2. Security re-check of the mapped-read-path changes (Doing, fix round 2); `campaign accept`.

## Blocked on

0. C3 residual (lead, round 3): a refresh now frees a retired view, but a lookup pinned at a
   commit that had already passed its sequence check (mid-answer, e.g. in a cold miss) answers the
   old generation and unpins without refreshing; that view stays charged until the next refresh,
   so until the next publish unless the application calls `detent_refresh`. Under concurrent
   lookups this is likely at a commit. Fixing it touches the lookup path, beyond the free scan
   (D146), so not done. Options: (a) keep: `detent_refresh` or the next publish releases it,
   documented; (b) a lookup answered `DETENT_NO_MEMORY` while a non-current slot holds a view tries
   `refreshing`, runs `free_retired`, and retries if anything was freed (about 15 lines; the warm
   path gains one status compare; test: a reader parked at `LazyPageWindow::Miss` across the
   commit, then every key answers with no refresh). Recommendation: (b), as its own small round.

1. File set (lead): one file outside it, `docs/design/reader.md` (the brief's out-of-set list is the
   file set itself, then `: docs/design/reader.md`). It belongs: `Detent:07a77ec` records D143 in
   the design (Q5 "Ruled (a), D143", D3's mapping-cap paragraph) and corrects the verified bitmap
   ratio to the code (1/640 to 1/160, `VerifiedPages` keeps a nibble per granule; D3 and T4).
   Widen the set by that one file; nothing to revert.
2. t3-asan check (lead, msg-17): prefix its ctest with `DETENT_READER_SWITCH_READERS=4` and give
   the job `--cpus 4`. T3 then runs 4 readers under ASan; A4 says 16, which native and t3-tsan
   keep. Recommendation: apply to t3-asan only; leave t3-tsan at 16 (its races need the readers).

## Fixes since the review

Correctness CHANGES and security APPROVE, both at `Detent:d8c0182`. Fixes in `Detent:c300e5f`,
`Detent:5957f28` and `Detent:07a77ec`; the confirming suite runs are at the merged head `Detent:a60e089`.

| Finding | Fix | Test | Confirming jobs |
|---|---|---|---|
| C1 refresh memo (blocking) | `Detent:c300e5f`: a `detent_refresh` that does not fail clears `failed_store` | `test_reader_core` `refresh_memo` (the probe's sequence) | suite native 261123-020546-339806, plain 261123-020608-eda47a; fails without: MEMO-1 (`Detent:a68f7e5`) 261121-133829-b558f2 |
| C2 transient miss | `Detent:c300e5f`: `openat` resource errnos, `fstat` and `mmap` raise `system_error`, `failed` stays unset, the lookup answers `DETENT_IO` | `transient_miss` (skipped under Linux UBSan, `Detent:a60e089`) | native 15579c, plain efe19d; fails without: MISS-1 (`Detent:de94c2b`) 261121-133832-23f97d |
| N1 cap status | `Detent:5957f28`: `ReadAdmissionRefused` maps to `DETENT_NO_MEMORY` | `capped` | native 15579c, plain efe19d; fails without: NOMEM-1 261121-141047-4552ff |
| S1 bounds (D143) | `Detent:07a77ec`: one mapping cap per handle, `detent_open_options.mapping_bytes` | `capped` | native 15579c, plain efe19d; fails without: CAP-1 261121-141042-a5d80c, CAP-2 261121-141044-9b81e0 |
| S2 atfork abort | `Detent:c300e5f`: a refused `pthread_atfork` fails `detent_open` with `DETENT_NO_MEMORY` | `atfork_refused` | native 15579c, plain efe19d; fails without: ATFORK-1 261121-133840-2a7bcc |
| S4 note citation | `Detent:96a069c`: reader-core.md "Bounds", "Mapped bytes after verification", file-admission counts | doc check | 261122-214531-91bf27 |
| C3 retained view charged (blocking, round 3) | `Detent:dece111`: every refresh first frees the views no lookup pins | `test_reader_core` `retained` | native 261123-020546-339806, plain 261123-020608-eda47a; fails without: RETAIN-1 261123-020610-c38819 |
| C4 atfork lock on every open | `Detent:b490591`: atomic flag before the lock | `test_reader_core` `fork_mid_open` | native 339806, plain eda47a; fails without: ATFORK-2 261123-020612-66011f |
| N6 probe `.at` | `Detent:c1c7498`: `find` | `reader_capacity_probe` | 261123-020615-e1cb4e |
| CI linux-asan-ubsan at `Detent:c6b1315` | `Detent:a60e089`: `transient_miss` skips descriptor exhaustion under Linux UBSan (its vtable check needs a pipe) | CI at the head | CI linux-asan-ubsan at the pushed head |

## Evidence

At the merged head `Detent:a60e089`; mutants (fails without) at their own commits.

| Criterion | Evidence |
|---|---|
| A1-A3, A5, A6, A15 | native 261123-020546-339806, tsan 261122-214348-4432db, asan 261122-214406-a1ad79, plain `test_reader_core` 261123-020608-eda47a |
| A4 | t3-tsan 261123-020547-63d2a8, t3-asan (4 readers) 261122-214431-1a9b50, 261122-214407-57a878; fails without: STATS-1 261119-045706-676367, DROP-1 261120-150455-acc366 |
| A7 | 261122-214407-0657dc |
| A8 | dense ASan 261122-214408-ea231c; mutants as `campaign brief` lists (D5-1..3, D10-1..3, D2-1, D4-1, FIX-1, FIX-2, Q6-1, HDR-1, SEL-1, SEL-2, BOUND-1) |
| A9 | [artifact] `docs/mechanisms/reader-core.md`: "## Fork and cold preparation" (also its sections Fork (D10), Bounds and Mapped bytes after verification; doc check job 261122-225652-b8b8ac) |
| A10 | 261122-214408-c31340; mutants SR1-selected, -image, -change, -record, -pack (brief) |
| A11 | 261122-214408-377d11; mutant SR2 (brief) |
| A12 | cold 261122-214409-d604f3, soak 261122-214409-ebe627; mutants F1-F4 (brief) |
| A13 | 261122-214409-d604f3 |
| A14 | 261122-214410-54f6dc, 261123-020608-eda47a; fails without: CAP-1 261121-141042-a5d80c, CAP-2 261121-141044-9b81e0, NOMEM-1 261121-141047-4552ff |
| A16 | D142 against main `Detent:005f84b`: exclusive 261122-214454-dbd4b2 (running), 261122-214458-034e64 (stopped); counts 261122-214501-3e6714, 261122-214505-7c4359; A/B 261122-214410-77d230 |

## Messages

- msg-17 (lead, t3-asan at ~974% CPU, cab068): `test_reader_switch` reads
  `DETENT_READER_SWITCH_READERS` (default 16); the check change is under Blocked on 2. The
  t3-asan job at the merged head runs with it set to 4 and `--cpus 4`.

- Lead (2026-11-20), D142: applied at `Detent:45db5f7`; A16 resubmitted exclusive; alarm change kept;
  disk store ASan attribution running (head 40/40 passed, main pending).

- Lead (2026-11-20), D140: merged main (`Detent:13f15b7`), gate on D140 (`Detent:f3f76e4`), gates
  requeued; CI dispatched, and on the Linux gcc T3 failure it exposed, fixed at `Detent:d0bdc01`.

- Coordinator (2026-11-19), both blockers: (1) rebench and A/B into `docs/perf/reader-lookup.md`: done at the commit after `Detent:ad86fb0`; (2) c027f04 accepted, freshness argued under Rulings.

- msg-1: attributed (Doing, baceeb and dd2b26); `Detent:73c0b6c` decodes each header once per page;
  lead ruling (option b) applied at `Detent:e77b31d` (raw nodes end again, memory-safety test and
  BOUND-1); the remaining node-codec gap goes to paged-decode-one-shape; closes with rebench 27840a.
- msg-2: applied: `minimum_page_bytes` for the pack extent, header compare before any bitmap
  answer, `layout_version` gone with main's lease, `state_commitment` replaces `root.logical`,
  DES-7 merged (`verified_page_bytes`, `Detent:4fafed3`).
- msg-79 (mine to the lead): closed by the lead; `generation.cc` and `paged_navigation.hh` added.

## Result

msg-1, `reader_bench`, 100K knobs (`--generate 50000`), int, 4 reps x 1M calls, exclusive. Median
of per-thread p50 / p99 in ns; before 318fd2 at `Detent:b1d281c`, after 63e21d at `Detent:32e65d8`:

| Case | 1 thread before | 1 thread after | 8 threads before | 8 threads after |
|---|---|---|---|---|
| core hot | 393 / 446 | 575 / 728 | 468 / 1052 | 702 / 1601 |
| pinned hot | 394 / 430 | 578 / 724 | 495 / 1086 | 724 / 1645 |
| core scattered | 659 / 1092 | 980 / 1475 | 798 / 1695 | 1140 / 2590 |
| pinned scattered | 687 / 1130 | 897 / 1214 | 831 / 1714 | 1124 / 2640 |

Each call includes the clock (about 65 ns). The stores differ in their rollout knobs (bool before, int
rollout values after, per D1), not in the int keys timed.

Attribution (msg-1 regression):
- Profile: `sample` of hot 1-thread, both arms, both commits (`.scratch/prof/*.sample.txt`). After,
  `decode_page_header` is 9.6% of the thread's samples and `decode_node` 8.5%, with
  `decode_external`, `decode_child` and `get_signed` 3.4% more; before had none of them on the hit
  path. Clock share fell from 16.9% to 12.3%: per call is 1.37x longer.
- Experiment: head plus a scratch patch that reads the external count instead of decoding the
  header in `decode_node`/`decode_external`, interleaved with head, 4 pairs, strict hold, 1 thread,
  same store (`.scratch/exp/ab.txt`). Median p50, head to patched: pinned hot 580 to 440
  (-140), pinned scattered 939 to 700 (-239), core hot 591 to 530 (-62, noisy pair), core scattered
  943 to 692 (-251). That recovers most of the hot gap (394 before) and all of the scattered one.
- Cleared: `checked_or_unlinked`'s fstat is not on the lookup path (no syscall but the clock in
  any sample); D1 `detent_get` dispatch adds nothing measurable (core and pinned move together);
  `verified_page_bytes` self time fell (446 samples in `resolve` before, 228 after).

## Rulings I made

- Round 3, C3: one helper, `free_retired`, for both scans (refresh start and commit), so the
  refresh frees on every outcome with one mechanism. The `Freeing` window now fires in either scan.
- Round 3, C4: the window `Registering` (hooked build only) is the test seam; ATFORK-2 keeps it at
  the same decision point, inside the lock, as the code before the fix.
- Round 3, N6: the probe's job runs the "reads" branch on 60,000 records; the "refuses" branch
  needs a store past 512 MiB (about 2M records), so the fix there rests on reading.

- D143's open-time check: `detent_open` attaches nothing (D2), so no store-specific bitmap size is
  known there. The floor is the format's: one full pack and its bitmap, 1,048,576 + 6,560 =
  1,055,136 bytes, public as `DETENT_MINIMUM_MAPPING_BYTES` and static-asserted against
  `paged::segment_bytes` and `VerifiedPages`; below it `DETENT_INVALID_ARGUMENT`. An attach whose
  segment table the cap cannot hold still fails `DETENT_NO_MEMORY` at the first lookup.
- D143's one ledger: the page table takes the cap as both its heap and its mapping account, so the
  cap counts mappings, bitmaps, segment objects and the segment table; `lazy_page_access` and
  main's client unchanged. Field named `mapping_bytes`, as the agent's preset and `ViewLimits`.
- D143's "2M store" case is `reader_capacity_probe`, run as a job, not a suite case: a 2M-record
  publish is too slow for the ASan/TSan gates. The suite's `capped` shows the raised cap on the
  wide store.
- reader.md D3 and T4 said the bitmaps are 1/640 of mapped bytes (one bit per granule);
  `VerifiedPages` (main's) keeps a nibble per granule, 1/160, 6,560 bytes a full pack, about
  9.4 MiB at 5M. Corrected both to the code, as part of the D3 update.

- Review fix round 1: per-handle ledgers, not main's process-wide accounts: `resources::mapping_account`
  builds view.cc's `Defaults`, whose retirement runtime starts a thread (the core starts none), and
  `test_reader_switch` compares per-handle mapped bytes. The bounds are main's numbers.
- C2's transient set: only resource errnos on `openat` (a missing, symlinked or refused pack stays a
  source failure); every `fstat` or `mmap` errno on a checked descriptor. The walk's catch-all in
  `paged_read_state.cc` rethrows `system_error`, so the client's lookup gets `SourceFailure` for that
  call only and delivery's scrub no longer requests a repair for a descriptor shortage.
- C1: `Refused` clears the memo too (the caller asked for a retry; the next lookup backs off as
  usual).
- The cap test uses a test hook (`testing::bounds`) set to one pack's mapped bytes, not a 513 MiB store.

- A table read that fails while a namespace is attached fails the refresh (`Detent:d0bdc01`) rather
  than install a view without it: D4 answers stale and never drops an answer it had. Without an entry
  (first attach, empty store) the view still records no table, as before.
- Freshness of attach from the refresh's table (`Detent:c027f04`, accepted by the lead): the refresh
  loads the store word and sequence, then reads `selected` (D4 rule 1), and attach runs next in the
  same `refreshing` hold with no wait between. A second read there could only see a table renamed
  in that gap of microseconds, and such a publish has not yet moved, or has just moved, the store
  word and sequence the view records as loaded *before* the first read; so the view's `seen` is at
  most the pre-publish value, the next lookup sees the sequence moved, and the winner refreshes
  onto the new table. Nothing is hidden: rule 1 (record what was loaded before the read) is what
  guarantees catch-up, not the read's age. The second read also checked nothing the first did
  not: attach still opens the lease named by the table, takes its shared `flock`, compares the
  held and named inodes, and re-reads `selected` on any lease gone or unlocked (a collected
  generation), up to three times, exactly as before. Main's Client did the same: one read, then the
  lease. The same argument covers `Detent:65afbd0`: the open's table is used only at the store word
  it was read under, with the sequence loaded before it, so any publish since the open moves that
  sequence and the next lookup re-reads.

- A16 fix: attach from the refresh's table changes the ledger's design-vs-code line "attach re-reads
  `selected`" (Decisions 1) toward D4's text; `docs/status/reader-s5-s6.md` now says what the code
  does. The first attach takes the view's table only when not forced, with no entry, the same
  `change` mapping and the same store word, and takes the view's recorded sequence with it, so a
  publish after the open still triggers a refresh (a sequence moved without the word gets a fresh
  read on the next lookup, since the entry then exists). t3 gates rerun from 015cc6/3544a5 to keep
  `--cpus 8`; verify's copies without it were cancelled.

- msg-1 option (b) shape: the memory-safety case holds the segment in an exact `new char[]` whose
  last bytes are the root page, so a read past the page is an ASan heap overflow, and checks every
  returned body lies inside the segment, so native runs check it too. The asan gate's filter
  (`test_reader_|change_file|lazy_page`) does not run `test_local_dense_reads`; 817ba5 runs it under
  ASan as its own job. BOUND-1 drops the index bound, since the in-bounds external count 0 reaches
  it; dropping `end <= size` instead is not reached by any damaged count (the node walk stays on
  the real nodes and `decode_external` keeps its own end check).
- Old gates at `Detent:2fb2a41` were not cancelled (the lead asked to read them); the A16 pair moved
  to `Detent:e77b31d` through `campaign job rerun`, which superseded baa905 and 2d8fe1 itself.
- Mutants run after native 6e68ac, so a broken tree cannot count as a kill.

- Lead rulings 2026-11-19 recorded: mutation 3 stays; node-step inlining goes to
  paged-decode-one-shape and the quest may land with that gap stated; t3-asan keeps 16 readers
  (the lead handles CPU accounting); the t3-tsan child exit is a real failure until named.

- msg-1 raw nodes end (`Detent:09be4f1`) reverted at `Detent:fa32973`: `test_dense_reads`
  mutation 3 is dense's contract, so dropping the header refusal is the lead's call (Blocked on 1).
  HDR-2 stays a mutant.
- A16 D121 by value: "p99" is the cell's pooled branch reads (1,200), and "within the noise floor"
  is a median paired difference at most the floor, so a faster branch passes.
- Deferred until msg-1 settles: the mutants, soak and A7-A14 reruns at the head, since a further
  msg-1 change moves the head again. Submitted now only the gates, the bench, the profile and A16.

- Job moves for the TSan and A16 step: at `Detent:751b648` (ebaa638's jobs cancelled) only build-all, STATS-1, both A16 cells and
  (after build-all) the gate checks. The mutants, soak and A7-A14 move once, after the msg-1 fix, since
  that moves the head again. A16 keeps BASELINE `Detent:8cd932c` so its numbers compare with b69551
  and da956e.

- msg-1 fix shape: the nodes end, not the header, is what `decode_node`/`decode_external` take;
  `decode_external` derives the external count from it and requires an exact fit, so a wrong end
  cannot reach outside the page. The `Limits` overloads stay (seven walkers outside the file set
  call them). Removing them is the lead's next quest, paged-decode-one-shape.
- HDR-1 and HDR-2 count toward A8 (a dying mutant per claim): the claim is that a page damaged
  after first touch is still refused.

- msg-1: f07232 aborted because `--generate 100000` writes 200K knobs and `parse_defaults` caps a
  defaults file at `kDefaultMaxKnobs` = 100,000. That holds at both `Detent:b1d281c` and the head,
  so the before arm (0f6566) would fail too, and a bench fix at the head can't reach it. Both arms
  now run `--generate 50000` (100K knobs, the cap): before 318fd2, after 63e21d, exclusive, 10 min.
  The head is unchanged.

- A14: the ledger stress publishes into a fresh store instead of raising the writer's disk limit
  (`src/detent_testing`, outside the file set). Running soak rounds at `Detent:64d8e34` were
  cancelled too: each runs the full `test_reader_switch`, `capacity` included, so each would fail.

- Main removed `StorageIdentity` and `storage.meta`; the core's "store identity change" refusal goes
  with them. A swapped store still fails the A4 regression check within an incarnation and the
  `change` identity check on refresh.
- C ABI follows main's reader.md D1 (one-knob-type): `detent_get(handle, name, hash, switch,
  fallback, out)`; `detent_eval`, `detent_read`, `detent_value`, `DETENT_WRONG_TYPE` removed;
  statuses renumbered to D1's table. C++ `Reader::get`, `get_as<T>` (out-of-range or a non-0/1
  bool returns the default), `lookup(name, fallback, ...)` per D7.
- Stale mutants: only `test_reader_switch cold` changed since `Detent:4fafed3` (A13's child ledger
  check; `parks`, `forks`, `test_reader_core` and `test_reader_files` are unchanged), so F1-F4
  were cancelled (998758 eb96b2 17a24b 82cbad) and resubmitted at `Detent:8ccef6c`; the other 17
  stay at `Detent:4fafed3`.
- A16 gate shape: the gated number is each process's `total_ns` from before `detent_open` (or
  the Client's runtime) to the checked first value, launch and loading excluded as before. A run
  clones the tree's store (`cp -c`), reads its files into the cache, and starts five warmup
  processes. Each tree builds its own store, as the 5M gate did. The gate targets join each tree
  through `CMAKE_PROJECT_detent_INCLUDE` with a deferred `include`, so no `CMakeLists.txt` is
  edited.
- Retiring the 5M gate (lead's ruling): `docs/perf/first-read-gate.md` now describes only the ported
  gate. `tests/detent_system/first_read_gate.py` is deleted, since `git grep` finds no caller. The
  file set names `docs/perf/first_read_gate.py`, which does not exist, so the lead should correct
  the path to `tests/detent_system/first_read_gate.py`. `first_read_export.cc` stays, because
  `docs/perf/one-knob-type.sh` builds it.
- A12 soak: eight jobs of one round each (four concurrent full `test_reader_switch`, Release),
  since 8 rounds took about 2 h on 2026-11-07 and a light soak job is capped at 30 min.
- A verified hit compares the reference's length, keys, height and digest with the page header's
  raw fields (as `checked_page_bytes`), without a full decode.

Lead note (2026-11-22): reader-s6 is the critical path, so its queued jobs were cancelled and resubmitted at priority 5 (same command, commit, level, criteria tags). Old -> new: 214406-efe19d -> 225545-a3ed62; 214407-0657dc -> 225548-60d6bb; 214407-57a878 -> 225551-f594a6; 214408-377d11 -> 225554-4474fe; 214408-c31340 -> 225556-947e79; 214408-ea231c -> 225559-da38a9; 214409-d604f3 -> 225601-2ec02b; 214409-ebe627 -> 225604-828c08; 214410-54f6dc -> 225606-bfc6ad; 214431-1a9b50 -> 225609-36b02d; 214454-dbd4b2 -> 225638-f831c7; 214458-034e64 -> 225641-3a81d3; 214501-3e6714 -> 225645-d6cd90; 214505-7c4359 -> 225648-985198; 214531-91bf27 -> 225652-b8b8ac (all 261122-). 214652-cc89db cancelled as a duplicate of bff807 (same code). Cite the new ids in Evidence.

Lead note (2026-11-23): 261123-021428-e9d6ef is the correctness reviewer's round-3 probe; it exits non-zero by design on residual R1 (15,564 of 60,000 rev-2 keys with a lookup paused across the commit; one detent_refresh restores 60,000). R1 is non-blocking and goes to the owner.
