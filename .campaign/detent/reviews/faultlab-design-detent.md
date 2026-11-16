# faultlab-design review: design (for Detent, D104)

Commit: cpp-libraries:bcf85b4b367352284d7bb0ed4c6e72b7da6be9f7 (docs/design/faultlab.md, branch quest/xapiand/faultlab-design; first round at cpp-libraries:a2c60c7193cb6a1637868dd674a082481084c216)
Verdict: CHANGES
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

The second round reviews the diff a2c60c7..bcf85b4 of docs/design/faultlab.md.

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

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | no (resolved at bcf85b4) | gap | **First round:** the recorder could not serve Detent's stores. They took a concrete `PosixIO` and published with a raw `linkat` and a raw `fstat`. **Resolved:** <br>1. F8 adds `IO::link`, which never replaces and throws EEXIST. <br>2. F7 adds faultfs `link`, drawing a coin only when one is logged, so F3's seeds hold. <br>3. F10 adds `verify(real_directory)` against the real directory's names and bytes. <br>4. F10's prerequisite and step 7 retype both stores to `journal::IO` and move the ownership check to `directory_trust()`. <br>5. Tests for link and for `verify` failing on a raw write are listed. <br>For MirrorStore this list is complete: every call it makes is a faultfs override or added by F7. | faultlab.md (bcf85b4) F7, F8, F10, step 7, checks; Detent mirror.cc:45-145, :196-330 | None. |
| R8 | yes | gap | Step 7's prerequisite is not enough for AgentStore. **(a)** It calls `File::try_lease(Exclusive)` to hold and collect generations. faultfs does not override it, so the io.h default throws `logic_error`; F7 does not add it. **(b)** `change` is opened by `ChangeWriter` on the directory fd: `openat` O_EXCL with a mode, `fchmod`, `fstat` owner and link count, `ftruncate` growth, then a writable `MAP_SHARED` mmap whose words are plain memory stores. "Open every file through the IO" covers creating, sizing and inspecting the file only if faultfs overrides `File::inspect`, which it does not today. The seam has no writable mapping (`File::map` is read-only), and no decorator can mirror memory stores into a shadow. So `verify()` would fail on `change`'s words on every run. Today's test models `change` by length only and never by its words (test_agent_store_crash.cc:63-64), and has a kill case between its creation and its sizing (:351-356, D59 F1). | Detent agent_store.cc:73, :208, :359, :530; change_file.hh:28-52; change_file.cc:31-87; completion io.h:85-94; strata faultfs.hh:470-766 (no `try_lease`, `inspect`) | F7 adds `File::try_lease` and `File::inspect` to faultfs. `try_lease` uses a per-inode lease table that `kill()` and `crash()` release, with `op::lease` for fault rules. For `change`'s words, pick one and state it in F10: <br>1. **Recommended:** `ChangeWriter` creates, sizes and inspects `change` through the IO, keeping its name and length (the parts recovery reads) in the shadow. It maps the words outside the seam from a new `File::map_shared()` that PosixFile implements with mmap and faultfs backs with the inode's current bytes; the recorder forwards it to the real file only. `verify(dir, length_only = {"change"})` then declares the one exception, as today's test does. <br>2. Keep `ChangeWriter` on the directory fd and make `verify` skip `change` entirely. This is cheaper but loses the D59 F1 creation-to-sizing cut from the shadow. <br>Test: the agent-store crash run ends with `verify()` passing, and a lease held at a cut is released by `kill()`. |
| R2 | no (resolved) | gap | QD3 said placed stops could be driven from outside. F12 now gives `at=<label>`, F15 states faultlab carries no signal code and that code-point stops stay in the test, and QD3 records it. | faultlab.md F12 item 3, F15, QD3 | None. |
| R3 | no (resolved) | gap | `isolate` meant two different faults. F14 now names both (`isolate` holds silently; Detent's behavior is `isolate` plus `reset`), step 7 picks one per run, and `Mesh.close` raises forward-task failures. | faultlab.md F14 | None. |
| R4 | no (resolved) | gap | The checker rejected Detent's `kind` lines. F12 item 4 and F13 now set them aside. | faultlab.md F12, F13 | None. |
| R5 | no (resolved) | nit | `kill()` now keeps staged ordering epochs. | faultlab.md F6.1 | None. |
| R6 | no (resolved) | nit | The recorder now takes `(filesystem&, directory)`. | faultlab.md F10 | None. |
| R7 | no (resolved) | nit | Step 5 now names journal-driver's recount and keeps `RecordingIO` as a test decorator. | faultlab.md step 5 | None. |
| R9 | no | nit | F5 calls the both-names state "illegal", and the criterion should be named. Both `additions` and faultfs's default are judged by the IO contract: independent coins per namespace operation, with `replace` atomic. They are not judged by a real filesystem. By real-filesystem standards `additions` is not reachable either, because journaled ext4, xfs and APFS persist directory operations in prefix order, and a later create surviving an earlier lost remove is not a prefix. | faultlab.md F5 | One clause: "legal" means allowed by the IO contract that faultfs models; the both-names state is outside that contract and, see below, unreachable on APFS, ext4 with its journal, and xfs. |

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

## Not run

- No jobs.
- F5's mapping and the seeded matrices' run time are not measured; they are step 7's job (QD2).
- F9 is the owner's ruling and not reviewed.
- Xapiand-, strata- and storage-side steps are outside this lens.
- I did not check whether Detent refuses unjournaled filesystems through `directory_filesystem()`.
