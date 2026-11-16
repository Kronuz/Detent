# faultlab-design review: design (for Detent, D104)

Commit: cpp-libraries:727e417a84a39597a7ce185d97eb28381b3a2b6f (docs/design/faultlab.md, branch quest/xapiand/faultlab-design; earlier rounds at cpp-libraries:a2c60c7193cb6a1637868dd674a082481084c216, cpp-libraries:bcf85b4b367352284d7bb0ed4c6e72b7da6be9f7 and cpp-libraries:9d23d747b746432c14cd5550204e817704d7b482)
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

The quest is a design with no code, so there were no jobs to accept. Every finding cites
`file:line` at these commits:

- Detent:abb78f7 (home checkout).
- completion:7731315.
- strata:491a1bd.
- journal:8f3e410 and journal:5a959a1 (quest/detent/journal-driver).
- consensus on quest/detent/consensus-fetch-macos.
- Xapiand:52c3e82 (harness/cluster).

The second round reviews the diff a2c60c7..bcf85b4 of docs/design/faultlab.md. The third round reviews
the diff bcf85b4..9d23d74. The fourth round reviews the diff 9d23d74..727e417, including 3d412e5's
map_shared fold rule.

## What I checked myself

All checks were reads; no probe was needed.

- **First round:** the two crash subjects and their recorders against F8 and F10. journal-driver's
  `PrefixIO` against F4. PeerMesh against proxy.py (F14). The relay histories, A6 and A2 against
  history.py. completion's sync_calls_test against QD1's premise.
- **Second round:**
  1. Every `journal::IO`, `journal::File` and raw call the two stores make, against faultfs's
     overrides (strata faultfs.hh:470-766) and F7. This checks step 7's prerequisite list.
  2. Every `replace` in the two stores against the directory syncs before it, for the dropped
     both-names state.
  3. journal's MemoryIO power-loss model (journal:5a959a1 memory_io.h:49-81), for the same state.
- **Third round:** a search of both stores and every helper they include for raw `open`/`openat`,
  `mmap`, `fstat`/`fstatat`, `linkat`, `rename`, `fsync`, `ftruncate`, `fchmod` and `flock`. That
  is mirror.cc (which includes only mirror.hh) and agent_store.cc with change_file.cc,
  live_record.hh, names.hh, segment_names.hh and layout.hh. I then compared each remaining raw call
  with what the seam provides at 9d23d74.
- **Fourth round:**
  - R10's fix against change_file.cc:31-47: `openat`, then `O_EXCL` create, then `fchmod`, then
    `fstat`, then `mode_only_acl`.
  - The fold rule against ChangeWriter's write path, from agent_store.cc:474-479 (`sync_directory`,
    `reserve`, `publish`) and :244-246 (startup `reserve`, `bump_all`). ChangeWriter makes no
    `sync` of `change` anywhere.
  - Whether ChangeWriter stores from a thread other than the one making IO calls: agent_store.cc
    starts no thread.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | no (resolved at bcf85b4) | gap | **First round:** the recorder could not serve Detent's stores. They took a concrete `PosixIO` and published with a raw `linkat` and a raw `fstat`. **Resolved:** <br>1. F8 adds `IO::link`, which never replaces and throws EEXIST. <br>2. F7 adds faultfs `link`, drawing a coin only when one is logged, so F3's seeds hold. <br>3. F10 adds `verify(real_directory)` against the real directory's names and bytes. <br>4. F10's prerequisite and step 7 retype both stores to `journal::IO` and move the ownership check to `directory_trust()`. <br>5. Tests for link and for `verify` failing on a raw write are listed. <br>For MirrorStore this list is complete: every call it makes is a faultfs override or added by F7. | faultlab.md (bcf85b4) F7, F8, F10, step 7, checks; Detent mirror.cc:45-145, :196-330 | None. |
| R8 | no (resolved at 9d23d74, except R10) | gap | Step 7's prerequisite is not enough for AgentStore. **(a)** It calls `File::try_lease(Exclusive)` to hold and collect generations. faultfs does not override it, so the io.h default throws `logic_error`; F7 does not add it. **(b)** `change` is opened by `ChangeWriter` on the directory fd: `openat` O_EXCL with a mode, `fchmod`, `fstat` owner and link count, `ftruncate` growth, then a writable `MAP_SHARED` mmap whose words are plain memory stores. "Open every file through the IO" covers creating, sizing and inspecting the file only if faultfs overrides `File::inspect`, which it does not today. The seam has no writable mapping (`File::map` is read-only), and no decorator can mirror memory stores into a shadow. So `verify()` would fail on `change`'s words on every run. Today's test models `change` by length only and never by its words (test_agent_store_crash.cc:63-64), and has a kill case between its creation and its sizing (:351-356, D59 F1). | Detent agent_store.cc:73, :208, :359, :530; change_file.hh:28-52; change_file.cc:31-87; completion io.h:85-94; strata faultfs.hh:470-766 (no `try_lease`, `inspect`) | **Resolved:** F7 adds `inspect` and `try_lease` to faultfs (leases released by `kill()` and `crash()`, plus `op::lease`). F8 adds `File::map_shared`, which the recorder forwards to the real file only. F10 creates, sizes and inspects `change` through the IO, and `verify(dir, {"change"})` compares it by length. The former fix text follows for the record: F7 adds `File::try_lease` and `File::inspect` to faultfs. `try_lease` uses a per-inode lease table that `kill()` and `crash()` release, with `op::lease` for fault rules. For `change`'s words, pick one and state it in F10: <br>1. **Recommended:** `ChangeWriter` creates, sizes and inspects `change` through the IO, keeping its name and length (the parts recovery reads) in the shadow. It maps the words outside the seam from a new `File::map_shared()` that PosixFile implements with mmap and faultfs backs with the inode's current bytes; the recorder forwards it to the real file only. `verify(dir, length_only = {"change"})` then declares the one exception, as today's test does. <br>2. Keep `ChangeWriter` on the directory fd and make `verify` skip `change` entirely. This is cheaper but loses the D59 F1 creation-to-sizing cut from the shadow. <br>Test: the agent-store crash run ends with `verify()` passing, and a lease held at a cut is released by `kill()`. |
| R10 | no (resolved at 727e417) | gap | One raw call in the stores has no seam member. `ChangeWriter` refuses a `change` file unless `internal::mode_only_acl(fd_)` holds, an fd-based ACL check that guards what readers trust. `File::inspect` returns `EntryInspection` (owner, links, permissions, kind, size), which carries no ACL. So once `change` goes through the IO (F10), the store either drops the check, which narrows a security refusal, or needs a native fd again, which bypasses the seam. Every other raw call is covered: <br>1. `openat` O_RDWR, then O_EXCL create: `open_existing`, then `create_exclusive`. <br>2. `fstat`: `File::inspect`. <br>3. `ftruncate`: `truncate`. <br>4. `mmap` and `munmap`: `map_shared` and its release. <br>5. agent_store.cc:201 `fstat`: `directory_trust()`. <br>6. `linkat` at :382 and mirror.cc:249: `IO::link`. <br>mirror.cc has no other raw call. | Detent change_file.cc:43-47 (`fchmod`, `fstat`, `mode_only_acl`); include/detent/config_trust_internal.hh:59; completion io.h:44-49 | **Resolved:** F8 adds `EntryInspection::extended_acl` (PosixIO reports an ACL beyond the mode bits; faultfs reports false), which covers `mode_only_acl` (change_file.cc:47). `create_exclusive` now applies the IO's mode exactly, which covers the `fchmod` after `O_EXCL` (:43). F10 maps change_file.cc:31-47 onto `create_exclusive`, `inspect` and `truncate`. The former fix text follows for the record. **Recommended:** `EntryInspection` gains one field, `bool extended_acl` (PosixFile fills it from the fd's ACL, as `acceptable_acl` does; faultfs reports false), and F10 says `ChangeWriter` refuses on it. That adds no new virtual. Also state that `create_exclusive` applies the IO's mode exactly, which today's `fchmod` after `O_EXCL` enforces against the umask (change_file.cc:43). Test: an agent-store test that sets an extended ACL on `change` is refused, through PosixIO. |
| R11 | no (citation resolved at 727e417) | nit | F10 cites "`File::inspect` (agent_store.cc:359, 530)", but those lines are `try_lease`. The `inspect` user is `ChangeWriter` (change_file.cc:46). Outside step 7's two stores, DiskStore also `fstat`s the native handle (disk_store.cc:433), and the reader-side files (generation.cc, prepared_reader.cc, segment_mapping.cc, lazy_page_access.cc, trusted_restart.cc, pack_scrub.cc, generation_file_access.hh, directory.cc, and `ChangeMapping` at change_file.cc:111-126) open, `flock`, `fstat` and mmap raw. They are read-only, so the shadow and `verify()` are unaffected. They run only on materialized real directories, never on faultfs. | as cited | Fix the citation. Optionally note that readers stay outside the seam and run on `materialize`. |
| R12 | no | nit | Under the seam, `change` gets the IO's file mode, while ChangeWriter still takes `options.mode`, which agent_store.hh:76 documents as "the PosixIO's artifact mode". That makes two sources for one value. | include/detent_local/agent_store.hh:76; agent_store.cc:208, :252 | At step 7, drop the argument or check `inspect().permissions` against it. |
| R2 | no (resolved) | gap | QD3 said placed stops could be driven from outside. F12 now gives `at=<label>`, F15 states faultlab carries no signal code and that code-point stops stay in the test, and QD3 records it. | faultlab.md F12 item 3, F15, QD3 | None. |
| R3 | no (resolved) | gap | `isolate` meant two different faults. F14 now names both (`isolate` holds silently; Detent's behavior is `isolate` plus `reset`), step 7 picks one per run, and `Mesh.close` raises forward-task failures. | faultlab.md F14 | None. |
| R4 | no (resolved) | gap | The checker rejected Detent's `kind` lines. F12 item 4 and F13 now set them aside. | faultlab.md F12, F13 | None. |
| R5 | no (resolved) | nit | `kill()` now keeps staged ordering epochs. | faultlab.md F6.1 | None. |
| R6 | no (resolved) | nit | The recorder now takes `(filesystem&, directory)`. | faultlab.md F10 | None. |
| R7 | no (resolved) | nit | Step 5 now names journal-driver's recount and keeps `RecordingIO` as a test decorator. | faultlab.md step 5 | None. |
| R9 | no (resolved at 9d23d74) | nit | F5 calls the both-names state "illegal", and the criterion should be named. Both `additions` and faultfs's default are judged by the IO contract: independent coins per namespace operation, with `replace` atomic. They are not judged by a real filesystem. By real-filesystem standards `additions` is not reachable either, because journaled ext4, xfs and APFS persist directory operations in prefix order, and a later create surviving an earlier lost remove is not a prefix. | faultlab.md F5 | One clause: "legal" means allowed by the IO contract that faultfs models; the both-names state is outside that contract and, see below, unreachable on APFS, ext4 with its journal, and xfs. |

## Answers QD1-QD3, F3

The design folded the first round's answers in at bcf85b4 (its "Questions for Detent's lead
(answered)" section). In short:

- **QD1:** yes, an IO-level recorder is enough once the stores go through the seam (R1, R8). Recovery
  runs on faultfs, with `materialize` only for `mirror_matches`.
- **QD2:** the four fixed picks at every cut, plus N seeds at the outer cuts only. N is measured
  inside the 900 s timeout. One job shows the old model's states equal the new ones before
  `after_power_cut` goes.
- **QD3:** the partition and outage runs fit as they are. A relay never serving older history
  needs the named `monotonic` check, and A2 the named `seal` check. A6's verdicts are timing
  assertions inside the test, and stops at a code point stay in the test.
- **F3 (the design's F4):** no conflict with D105. `PrefixIO` is the caller's decorator, valid
  over faultfs. F4 now says so.

### Both names of one rename (the second round's question)

**Nothing in Detent or journal relies on it.** The state needs a rename whose source name was
already durable when the crash hit. Neither store, nor journal's suites, ever renames a durable
source:

- **MirrorStore.** Every `replace` source is a temporary created after the last directory sync:
  - The table: stage, `sync_files`, `replace`, `sync_directory` (mirror.cc:97-116).
  - A replacement in the link loop runs before the group's `sync_files_and_directory`
    (mirror.cc:249-272).
  - A repair runs `sync_files`, then `replace`, then `sync_directory` (mirror.cc:324-328).
  Startup removes temporaries and never renames them (mirror.cc:141-145).
- **AgentStore.** The table temporary is created after the two group syncs and renamed before
  `s.sync_directory()` (agent_store.cc:413-475). Startup removes temporaries (:225).
  - The both-names states that do arise come from link followed by unlinking the temporary
    (agent_store.cc:382-404; mirror.cc:249-257). These are two operations, so the intermediate
    state is legal and `additions` keeps it.
- **The restart matrices** start from `durable_disk(files)` of a cut, whose temporaries startup
  removes. No test asserts state counts; they are printed only (test_relay_mirror_crash.cc:366,
  :471; test_agent_store_crash.cc:451, :509).
- **journal's suites (MemoryIO) never produce the state.** Power loss restores `durable_names`
  whole (memory_io.h:66-81). faultfs replays `rename` atomically (faultfs.hh:10-11).
- **QD2's comparison job** should therefore list zero dropped states for both stores. A nonzero
  list is a finding.

**It is unreachable on Detent's filesystems.** On ext4 (with its journal, any `data=` mode), xfs
and APFS, a rename is one metadata transaction, so recovery shows the old names or the new ones,
never both. The Linux man page's "both oldpath and newpath" window is a runtime window, not a
crash state. It is reachable only on unjournaled filesystems: ext2, ext4 with `^has_journal`, and
FAT, where fsck may repair it into two links. Detent's recovery would tolerate even that, because
startup removes a temporary that still names the new file. So dropping the state costs Detent
nothing, and keeping it would test nothing Detent's filesystems can produce.

### map_shared's crash model and ChangeWriter (fourth round)

The model fits, and nothing in it decides a Detent recovery.

- **When stores count as durable: never through a sync.** ChangeWriter's stores (`publish` at
  agent_store.cc:479, `bump_all` at :246) are never followed by a `sync` of `change`. On faultfs, a
  fold turns them into unsynced writes of the changed sectors. At a crash each sector keeps its old
  bytes or its new ones, beyond the never-synced durable size it reads as zeros, and the size is
  the durable one or the current one. That matches today's model, where `change` is never synced
  and its words are kept as zeros of its live length (test_agent_store_crash.cc:4, :63-64).
- **Sizes stay valid.** Every size a crash can pick is a multiple of 8: `change_bytes` sizes, or
  512-byte boundaries under the opt-in `size_pick::sector`. So recovery's word-shape check never
  sees a torn length. A size under one word is extended (change_file.cc:50-54), which is the D59 F1
  cut F10 keeps.
- **What the recorder and `verify` see.** `map_shared` goes to the real file only, so the shadow
  never holds a mapping and never folds. It sees `change`'s create and its `truncate`s (startup
  :244, `reserve` :476), so its length tracks the real file. `verify(dir, {"change"})` compares
  that length and nothing else, and the fold rule does not touch the recorded matrix.
- **Where the fold rule applies.** It matters only when AgentStore runs on faultfs directly
  (QD1 (a) recovery). There `reserve` and `bump_all` at startup store through the mapping and fold
  at the next call, with `mutations()` and the cut points unchanged.
- **Threads.** AgentStore stores and calls the IO on one thread (agent_store.cc starts none), so
  F8's single-thread caveat holds. Readers map `change` in other processes through raw
  `ChangeMapping` (change_file.cc:111-126), outside faultfs.

## Not run

- No jobs.
- F5's mapping and the seeded matrices' run time are not measured; they are step 7's job (QD2).
- F9 is the owner's ruling and not reviewed.
- Xapiand-, strata- and storage-side steps are outside this lens.
- I did not check whether Detent refuses unjournaled filesystems through `directory_filesystem()`.
