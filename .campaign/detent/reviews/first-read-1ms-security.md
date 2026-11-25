# first-read-1ms review: security

Commit: Detent:e52669a7
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

None at this commit. Mutant job 261124-155204-3e4aa2 (the page digest is not checked) was still
queued when I finished, and so were CI and A1/A2. I read the code and the tests and ran my own
probes. The suite's `test_trusted_paths` "raced" case (now the first `fstatat`, which is the
post-open name check) and `test_reader_core` `damaged_page` are in the diff. I did not see them
pass at this commit.

## What I checked myself

Scripts are in `.scratch/detent/review-first-read-1ms-security/` (`run.sh`, `probe_table.cc`,
`probe_reader.cc`). `run.sh` copies the probes into the job checkout and builds them against
`detent::local_view` and the reader core. With `SAN=thread` it builds the concurrency probe
under TSan.

- Job 261124-164244-e6d442 (light, RelWithDebInfo, macOS) failed with 2 FAILs per binary:
  - **tamper:** 62,950 single-byte mutations (xor 0x01/0x80/0xff, set 0x00/0xff at every byte),
    every truncation, and extensions of an 11,995-byte, 70-entry, 3-page table. 9,968 were
    refused at the read. Every other case answered the original entry, answered not listed for
    an unlisted name, or refused that lookup, on the first reach and again after marking, with
    and without marks. **PASS**.
  - **layout-2 (unit):** a 237-byte layout-2 table is `LayoutRefused`. A 96-byte empty layout-2
    table is `TableRefused (header)`. **FAIL**, see S1.
  - **layout2-store (end to end):** the store reopens over a 1-entry layout-2 `selected` and
    removes it. Over an empty one, the open throws "agent store table: header". **FAIL**, see S1.
  - **selected-acl:** `read_store_table` accepts a `selected` carrying `everyone allow write`
    (`mode_only_acl` false). **FAIL**, see S2.
  - **reader-selected-acl:** the reader core answers `n001`/`n099` from that table. **FAIL**,
    see S2.
  - **reader-image-acl:** a `.image` with that grant is refused (`source_failure`). This
    confirms that reusing the selection's image bytes keeps the ACL check on the bytes used.
    **PASS**.
  - **reader-selected-hard-link:** a second link refuses the open. **PASS**.
  - **walk-race (20 s):** a component flipped between the genuine directory, absent, and
    non-root symlinks to another directory and back. Result: 8,180 genuine opens, 40,891
    refusals, 0 opens of another directory. **PASS**.
  - **mapped-change-before-mark:** a page changed in place before its first touch refuses.
    **PASS**.
  - **mapped-change-after-mark** and **mapped-truncation** / **reader-selected-truncated**: on
    macOS both answered the bytes as first mapped. The truncated reader was not killed. See N1.
- Job 261124-160318-6c1199 (TSan) passed: 300 rounds of 8 threads first-touching one fresh
  table and one table with a damaged last page. No TSan report, no wrong answer, and every
  thread refused the damaged page. Concurrent first touches hashed the same page more than once
  (23,745 extra hashes in the Release run), as the design says they may.
- Read: `PagedTable` (store_table.cc), `read_table` / `select_entry` (generation.cc),
  `open_publisher_directory` (generation_file_access.hh), `PreparedReader::attach`,
  `VerifiedPages`, and the reader core's table use.
  - In `walk`, every page check (digest, order after the previous page's last name, slot below
    `slots`, last name equal to the index's, zero padding) runs before `marks->set`. A marked
    page still runs every check except the hash.
  - The mark is set with a release CAS and read with acquire.
  - `selected` is opened with `O_NOFOLLOW` and checked for owner, regular file, `nlink == 1`,
    no group/other write and size, then mapped `PROT_READ, MAP_PRIVATE` from that same fd.
  - The agent replaces `selected` only by temp+rename (agent_store.cc:466-477), so a reader's
    old mapping is of an inode that nothing writes again.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | yes | defect | An empty layout-2 table is refused as damage, not as a layout (D43). `PagedTable` checks `in.size() >= 72 + 32` before it reads the layout. Main's encoder writes an empty table as 64 + 32 = 96 bytes, which a publish that drops every namespace produces (agent_store.cc:441). So it gets `TableRefused("header")`, and `DiskStore` open (disk_store.cc:481-507) then throws out of `decode_table(bytes).profile`. The store never opens, where a non-empty layout-2 table is removed once and bootstrapped. | job 261124-164244-e6d442 `layout-2` and `layout2-store` FAIL; store_table.cc:193 | Read the magic and the layout first (size >= 12). For an older layout, check its whole digest (size >= 96) and throw `LayoutRefused`, then apply the layout-3 size check. Test: `decode_table` of the 96-byte empty layout-2 table throws `LayoutRefused`, and a managed store over it opens with the refusal recorded. |
| S2 | yes | defect | `selected`, the store's trust root and now a mapped file, is the only store file read or mapped without `mode_only_acl`. Records (prepared_reader.cc:40), leases, `change` and segment payloads (lazy_page_access.cc:163) all check it. A macOS ACL grant gives a non-owner write access to the table the reader trusts, and an unkeyed SHA-256 cannot catch a forged table. The gap predates this quest (main's `read_table` has no ACL check). This quest maps the file and reuses the image ACL check, which makes `read_table` the one place the check is missing. Splitting it into its own quest is the lead's call. | job 261124-164244-e6d442 `selected-acl` and `reader-selected-acl` FAIL; generation.cc:115 | After `checked_or_unlinked` in `read_table`, add `require(internal::mode_only_acl(table_file.fd))`, before the map. Test (macOS): a `selected` with `everyone allow write` refuses the reader's open and lookups. |
| N1 | no | gap | A page's mark assumes the mapped inode never changes. On macOS (XNU copy-delay) the probe saw `MAP_PRIVATE` behave as a snapshot: an in-place write after the mark and a truncation both answered the bytes first mapped. On Linux, an unmodified private page shows later writes to the file, and a truncation SIGBUSes the reader. With S2 fixed, only the owner or root can do either, the same trust segment marks already rely on. | job 261124-164244-e6d442 `mapped-*`, `reader-selected-truncated` (macOS only) | Add one line in the design: marks and the mapping rely on `selected` being immutable after its rename (agent temp+rename). |
| N2 | no | gap | The design claims that concurrent first touch is safe ("both hash it; both marks agree"), but no suite test covers it. My TSan probe passes. | job 261124-160318-6c1199 | Optionally add the 8-thread fresh/damaged-page stress to `test_table_pages`. |
| N3 | no | gap | The suite damages one index byte and one entry byte only. Damage to the header, table digest, page order, last name, slot bound and padding, and truncation, are covered only by my probe. | job 261124-164244-e6d442 `tamper` | Optionally fold the single-byte mutation oracle into `test_table_pages`. |
| N4 | no | nit | The `selected` mapping (up to `table_bytes(2^20)`, about 1.1 GB) is not admitted to the mapping ledger, while segment mappings are. Only its marks are accounted. Before this change the whole table was an unaccounted heap vector, so this is not a regression. | store_table.cc:176; lazy_page_access.cc admit | None required. |

## Not run

- Linux (in-place write or truncation of a mapped `selected`, O_RDONLY trust walk); FreeBSD ACLs.
- I/O errors on the mapped table, which a disk fault turns into SIGBUS rather than an error status.
- The `paged_read_state.cc` leaf-key change and the inline `decode_node`/`read_integer` moves,
  beyond reading them (correctness lens).
- `same_table` comparing digests only: a republished, repaired table with the same digest keeps
  the damaged mapping until the next real publish. This is availability, not authenticity.
- The suite, CI, the mutant job, and A1/A2: all were queued or running at this commit.

## Round 2

Commit: Detent:9cc06bc9 (code as at Detent:47216200; 9cc06bc9 changes only the journal)
Verdict: APPROVE
Date: 2026-11-24

### Evidence I relied on

- 261124-193948-acb3a6: the new tests pass at Detent:47216200. In `test_local_table_pages` these
  are the S1 cases (layouts 1 and 2: empty, short, whole, damaged), the per-check cases and the
  concurrent first touch. In `test_reader_core` they are `acl_grants` and `damaged_attached`.
- The fails-without mutants:
  - S1 (size rule before the layout): 261124-194035-e14cfa, at Detent:99b8fb19, exit 1.
  - S2 (no ACL in the helper): 261124-194041-ed977a, at Detent:d6da5a90, exit 134.

### What I checked myself

The round-1 scripts were adapted to `TableEntryView`. `probe_reader.cc` also gained
`agent_selected_acl`.

- Job 261124-195512-09902d passed with 0 FAILs:
  - **tamper:** the same 62,950 mutations, truncations and extensions. The second pass now
    answers marked pages from the offset index. **PASS**.
  - **layout-2:** the 96-byte empty table and the 237-byte one-entry table are both
    `LayoutRefused`. **PASS**.
  - **layout2-store:** a managed store over either opens and removes `selected`. **PASS**.
  - **selected-acl / reader-selected-acl:** a `selected` with a grant refuses with "ACL" and the
    reader's open fails. **PASS**.
  - **reader-image-acl** and **hard-link:** **PASS**.
  - **walk-race:** 19,509 genuine opens, 93,566 refusals, 0 opens of another directory. **PASS**.
  - **agent-selected-acl:** the agent's store open refuses with "ACL", through
    `read_generation_selection`.
  - **mapped-change / truncation** on macOS: unchanged from round 1 (N1).
- Job 261124-195515-e40ef1 (TSan): 300 rounds of 8 threads on fresh and damaged-page tables. With
  the offset index, no TSan report and 0 wrong answers.
- Read: every `openat`/`open` in `src/detent_local` and `src/detent_reader`.
  - `selected`, `.image`, `.read`, `.lease` (attach and recovery, the latter `trusted_file` after
    its `flock`), `.live`, `.owner` and segment payloads all reach `generation_file_refusal`, ACL
    included, on the descriptor they read or map.
  - Segment payloads reach it through `inspect_mapping_file` for `SegmentMapping`, and through
    `check_generation_file` in `lazy_page_access`.
  - `change` keeps its own owner/link/mode/ACL rule. `PackScrub` trusts nothing it reads.
- Read: the offset index (store_table.cc `check`/`find`).
  - Offsets are written only after the digest, entry bounds, order, slot, last-name and padding
    checks have all passed, and before the release CAS of the mark.
  - A failed check throws before any store.
  - A reader reads the index only after `test()` (acquire) sees the mark.
  - Racing first touches store the same values.
  - Entries per page are bounded at 29 by the 138-byte minimum entry (static_assert).

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | defect | Fixed: the magic and layout are read first, and layouts 1 and 2 have their own size rule. | 261124-195512-09902d `layout-2`, `layout2-store`; mutant 261124-194035-e14cfa | Done |
| S2 | no | defect | Fixed: `generation_file_refusal` checks the ACL, and every reader and recovery file read goes through it. | 261124-195512-09902d `selected-acl`, `reader-selected-acl`; mutant 261124-194041-ed977a | Done |
| R1 | no | nit | "Store file checks" says the agent's open of `selected` uses the helper. It does, through `read_generation_selection`, and the agent open refuses a granted `selected` (`agent-selected-acl`). But the agent's own `verify` reads `.image`, `.read` and `.live` through completion's `PosixIO` (agent_store.cc:158-160). That check covers owner, link and mode, not ACL, and the table does not list it. Reaching it needs a grant that only the owner, root or an inherited directory ACE can add. An inherited ACE also lands on the next `selected`, which the agent then refuses. | agent_store.cc:126-160; completion posix.h:116 | Add a row for the agent's `verify` reads, or route them through the helper. |
| R2 | no | gap | A marked page is now answered from the offset index with no per-reach bounds check. `TableEntryView` reads the length byte and 136 field bytes in place. If the mapped inode changes after the mark (owner or root only, Linux visibility, N1), a changed length byte can read past the page, and on the last page past the mapping. The design states this ("What a marked page does not re-check"). It is the same immutability assumption as N1. | store_table.cc `find`; docs/perf/first-read-1ms.md | None needed inside the model. |

### Not run

- Linux and FreeBSD.
- CI at 9cc06bc9 (`macos-arm64` running).
- The A2 gates (queued).

## Round 3

Commit: Detent:28a8d276 (the head Detent:c125fd9e changes only docs/perf)
Verdict: APPROVE
Date: 2026-11-25

### Evidence I relied on

- 261124-221122-a481db: `test_agent_store` with `acl_grants` passes at Detent:28a8d276.
- 261124-221125-540805: the mutant at Detent:f495c19b swaps `slurp`'s helper call for the old
  owner/link/mode check. The test fails for an ACL reason, not a build one. Its log reads
  `test_agent_store: ACL grants accepted: .image .read .live selected reused`, with exit 1 and
  the build `|| exit 127` guard not taken.

### What I checked myself

Read every agent-side file access at Detent:28a8d276 (agent_store.cc, disk_store.cc,
change_file.cc).

- `slurp` now opens with `O_NOFOLLOW` and applies `generation_file_refusal`, owner `geteuid()`,
  on the descriptor it `pread`s. It serves:
  - the `.image`, `.read` and `.live` reads in `verify` (agent_store.cc:159-161);
  - the open's `selected` (agent_store.cc:230);
  - the reached-name reuse (agent_store.cc:399);
  - the held generation's `.live` (agent_store.cc:541).
- `change` keeps its own owner/link/mode/ACL rule.
- The agent opens leases through completion's `PosixIO` (agent_store.cc:360, :535) only to take
  an exclusive lease. It reads no bytes from them.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | no | gap | Fixed: the agent's `verify` and its other reads go through the helper. | 261124-221122-a481db; mutant 261124-221125-540805 | Done |
| R3 | no | nit | One agent read still escapes the helper. `DiskStore` open reads `selected` through completion's `open_existing_read_only`, which checks owner, link and mode but not ACL, to take the store's profile (disk_store.cc:495-506). Nothing it reads survives a refusal. `AgentStore`'s open then reads `selected` again through the helper, and that read refuses a grant (`acl_grants`, "selected") and requires the same profile (agent_store.cc:230-234). | disk_store.cc:495; agent_store.cc:230 | Optional: take the profile from `AgentStore`'s own helper read, so the one check is the only one. |

### Not run

My round-2 probes again, since the round-3 change touches only agent-side `slurp`. CI at
Detent:c125fd9e. Linux and FreeBSD.
