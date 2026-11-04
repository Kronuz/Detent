# Namespace S3 security review: the live record and trusted restart

**Date:** 2026-11-04
**Scope:** `namespace/s3` at `Detent:5af632a` against `github/main` at `Detent:3e8bdbc`: `live_record.cc`, `trusted_restart.cc`, `verified_pages.hh`, `paged_navigation.hh` and its read sites, `pack_scrub.cc`, the collector's `.live` handling and the coordinator's scrub task.
**Verdict:** **APPROVE WITH CONDITIONS.** No probe made a read return a wrong value. The decoder held under 924K fuzz inputs with an independent oracle. Every stale, swapped, truncated or unsafe record I built was refused. The conditions cover a digest-valid `.live` that opens trusted while disagreeing with the root (SR1, SR2): the agent then wedges or throws a logic error where the design promises a refusal or `SourceFailure`.

## Threat model and boundary

The design states none for S3 beyond the mechanism note's limits (`docs/mechanisms/trusted-restart.md`, "Limits"): trust ends at the live counts and at `.owner` records, "which a writer of the store can forge as it can forge any file there". Three attackers apply:

1. **A malicious or damaged input file.** This covers bit rot, a torn or truncated write, or a record copied from another generation or another store. The digest chain must catch it. A read must return the right value or `SourceFailure`, and a bad record must refuse the store. This is the attacker the branch must defeat.
2. **Someone with local file access as the store's account.** They can rewrite `selected`, the root and every page consistently, so S3 gives them nothing new. Findings against them are about fail-closed behavior and robustness, not privilege.
3. **A builder defect.** A digest-valid record that is wrong reaches a restart without a walk. The restart should refuse what it can check for free.

The digest chain runs from `selected` to the root (320 B hashed at start), then through each parent reference's physical and logical digest to every page (`paged_layout.cc:170-189`). `.live` decides only which file each segment id maps to, plus the derived counts. A substituted pack therefore fails first touch. A `.live` cannot change a value. It can change which segments are mapped (SR2) and what later builds derive (SR1).

## Findings

### SR1 (medium): a `.live` whose derived counts disagree with the root opens trusted and then wedges every update

**Attacker:** a builder defect, or a writer of the store (who gains nothing new). **Gain:** the store stays selected and restarts trusted, but every update fails. The refusal path ("a refused store is repaired like a corrupt one") never triggers.

[`trusted_restart.cc:61-71`](../../src/detent_local/trusted_restart.cc) checks the rows' lengths against `.read` and binds the header. It adopts `row.live` and the histogram unchecked (`:90`). Every total it would need is already authenticated and in memory: `root.pages`, `root.bytes`, `root.records`, `metadata.counts.overrides` and `metadata.counts.maximum_overrides`. The first update then makes the same checks and refuses (`paged_overlay_assembler.cc:118`, `paged_warm_validator.cc:545-553`).

Probe `P2.1`, `P2.2` (`.scratch/sr/probe_trusted.cc`, log `.scratch/sr/probe.log`) takes a 5-pack, 20K-knob store at revision 3. It re-encodes `.live` with a valid digest and either 1,000 extra live pages on row 0, or a histogram with 7 extra records in bucket 0 and one in bucket 200:

```text
P2.1 inflated live pages (sum != root.pages): opened trusted
P2.1 reads: ok 20000 source 0 wrong 0
P2.1 update over it: publish threw (invalid warm paged graph)
P2.2 forged histogram: opened trusted
P2.2 update over it: publish threw (invalid warm paged graph)
P2.1b shifted live pages (sums kept): opened trusted
P2.1b update over it: publish threw (invalid warm paged graph)
P2.1b restart after the update: opened trusted
```

Reads stay correct. The store is stuck: it restarts trusted and refuses every publication. P2.1b moves live pages between two rows and keeps the sums, so no total can catch it. The successor's warm graph check catches it instead, with the same wedge.

The mechanism note is also inaccurate as written. It says "a wrong pair changes compaction choices, not values". The histogram also sets the successor's `maximum_overrides` (`paged_warm_validator.cc:546-547`), which reader processes enforce. Understated live pages decide whether `WarmRegistryMerge` drops a segment (`warm_registry_merge.cc:39`).

**Condition C1.** After decoding, `TrustedRestart::open` requires all five totals:

- The rows' live pages sum to `root.pages`.
- The rows' live bytes sum to `root.bytes`.
- The histogram sums to `root.records`.
- Σ i·h[i] equals `metadata.counts.overrides`.
- The highest nonzero bucket equals `metadata.counts.maximum_overrides`.

These are five comparisons over data already read, with no I/O. Add P2.1 and P2.2 to `test_testing_trusted_restart refusals`. Correct the note's sentence: it should list what the counts drive, and say that a shifted-but-summed pair (P2.1b) is caught only at the next update.

### SR2 (medium): a `.live` with current header and another generation's rows opens trusted, and every read throws `std::logic_error`

**Attacker:** as SR1. **Gain:** the agent's reads escape as `logic_error("validated page dependency missing")` instead of `SourceFailure`. The probe's first run died on it: `libc++abi: terminating due to uncaught exception of type std::logic_error`. The collector then sees reachability built from rows that are not the root's segments.

Probe `P2.5` takes revision 2's `.live` rows and re-heads them with revision 3's storage, generation, root checksum and `.read` digest. It re-encodes with a valid digest. The old packs are still on disk with their `.owner` records, and their lengths sum to the same physical bytes, so every check passes:

```text
P2.5 current header over stale rows: opened trusted
  read of k0 threw validated page dependency missing
P2.5 reads: ok 0 source 0 wrong 0 thrown 20000
```

The per-segment loop (`generation.cc:465-480`) proves each row names a real, owned pack. It does not prove the rows are the set the root reaches, so the note's "checked a second time against ground truth" overstates it. In the agent path, a reference outside the registry throws `logic_error` ([`paged_read_state.cc:54-55`](../../src/detent_local/paged_read_state.cc)). A reference outside a mapping throws `invalid_argument` ([`paged_navigation.hh:23-25`](../../src/detent_local/paged_navigation.hh)). `ReadView::try_get` maps only `ReadSourceFailure` (`view.cc:356`). Mutant M1 in the ledger hit the same abort. No value was wrong: every reachable page is still digest-checked.

**Condition C2.** In the agent's read path, a reference whose segment is absent from the registry, or whose extent exceeds its mapping, throws `ReadSourceFailure`. The lookup then reports `SourceFailure`, as the reader path does (`lazy_page_access.cc:196-198`). Also require at restart that `root.tree.segment` is one of the rows. This costs one binary search and refuses the P2.5 shape when the tree page's segment changed. Add P2.5 as a test that expects `SourceFailure` or a refusal, never an exception. Binding the row set itself needs a manifest digest the root or table authenticates (D10.2's W12 shape). That is a format decision for the lead and is not required here.

### SR3 (low): the in-process scrub cannot see damage that lands after the packs are mapped, on macOS

**Attacker:** a damaged input file, meaning bit rot or a stray write after start. **Gain:** none against reads, but the detection D10 promises is missing in a long-running agent.

[`pack_scrub.cc:42`](../../src/detent_local/pack_scrub.cc) hashes the agent's own `MAP_PRIVATE` mapping (`segment_mapping.cc:54`), not the file. Probe `P3.2b` (`SR_P32B=1 build-asan/sr_probe_trusted`) recovers trusted and reads all 20K knobs. It then rewrites 40 random bytes of the packs with `pwrite`, reads again and scrubs:

```text
P3.2b before damage ok 20000; 40 bytes rewritten; after: ok 20000 source 0 wrong 0 thrown 0; scrub passes 1 failures 0
P3.2b after a fresh restart: ok 18379 source 1621 wrong 0
```

Reads stay consistent with the snapshot the process mapped, which is the safe outcome. The scrub's single pass reports nothing, while a fresh restart surfaces 1,621 failures. On Linux, clean private pages share the page cache, so a file write shows through. Neither platform re-reads media for resident pages, though. T5 flips bytes before the restart, so it does not cover this case.

**Option (owner's call):** scrub with `pread` and `F_NOCACHE` or `posix_fadvise(DONTNEED)` instead of the mapping, so a pass compares the file. Otherwise, state in the note and in D10 that the scrub covers damage present when the pack was mapped.

### SR4 (low): a damaged-pack report does not withdraw the pages already verified

[`pack_scrub.cc:53`](../../src/detent_local/pack_scrub.cc) and `coordinator.cc` `scrub_loop` record the failure. The mapping's `VerifiedPages` bits stay set, so pages verified before the damage keep serving. This is the documented Limit ("A page verified before it was damaged stays verified"), and repair is blocked on a lead ruling. SR3 shows the scrub may not see such damage at all, so the exposure is narrow. When the repair lands, a scrub mismatch should clear that mapping's bitmap. Reads of the pack then re-verify and fail with `SourceFailure`, per D10.3. Not blocking.

### SR5 (low, pre-existing pattern): a FIFO in place of `.live` blocks the cold start in `open`

`open_record` ([`trusted_restart.cc:23`](../../src/detent_local/trusted_restart.cc)) and the pack open (`:78`) use `O_RDONLY` without `O_NONBLOCK`. Probe `P4.2` replaced `.live` with a FIFO. The restart sat in `open()` until the probe's 5 s watchdog opened the write end; `check_generation_file` then refused it. Only the store's account can create entries in the store directory (`generation.cc:245` refuses a group- or world-writable directory), and every other record open in `generation.cc:123-311` has the same shape. Not new to S3. Adding `O_NONBLOCK` to the record opens makes the refusal immediate.

### SR6 (low): the trusted restart allocates outside the storage ledger

`std::vector<char> live_bytes` ([`trusted_restart.cc:59`](../../src/detent_local/trusted_restart.cc)), the decoded `rows` (`live_record.cc:73`) and `reachable` (`trusted_restart.cc:74`) use the default allocator, about 150 B per segment. The size is bounded by `root.segments ≤ limits.segments`, checked at `:58`, and the root is authenticated, so an input file cannot inflate it. The default bound of 4,096 segments gives about 600 KiB uncharged. Charge `heap`, as the full recovery does. Not blocking.

## What held

1. **Decoder** (`live_record.cc:55-87`). The size and length field are checked before any row read. `rows ≤ maximum_rows` and `size == 1184 + 72·rows` hold before the digest and before `rows.resize`, so allocation is bounded by the input's own size. There is no 64-bit overflow, and 72·(2³²−1) fits easily. On a 32-bit `size_t` the size formula could wrap, but no supported target has one. The branch's driver plus my `.scratch/sr/live_record_fuzz2.cc` ran 924,433 inputs in 241 s under ASan and UBSan with no finding (`.scratch/sr/fuzz2.log`). `fuzz2` varies `maximum_rows` (0 to `UINT64_MAX`) and builds raw records with arbitrary row fields, counts, length fields and padding behind a repaired digest. It checks every accept or reject against an independent statement of the layout rules, not only the round trip.
2. **Binding.** These were all refused: a stale `.live` (P2.4), a stale `.live` and `.read` pair (P2.4b), a row checksum change (P2.3), and row lengths shifted with the sum kept (P2.3b, refused by the inode-length check). The branch's own `refusals` mode also passed under my ASan/UBSan build (`.scratch/sr/branch-tests.log`; its `t5` at the default 99,990 knobs also passed: 99,031 ok, 969 `SourceFailure`, 0 wrong, `.scratch/sr/branch-t5.log`).
3. **File safety.** These `.live` files were refused: a symlink, a hard link, a group-writable file, a directory, an oversized file (+72 B), an empty file, and an empty `.read` (P4). A record is copied with `pread` and decoded from the copy, so there is no double fetch.
4. **First touch.** All nine `checked_page_bytes` sites take their reference from the hashed root or from a page that just passed `verify_page`, which checks the physical SHA-256 over the exact extent and then the logical commitment. I found no raw-span read of a base page outside them. The other `.bytes` uses hash whole packs (`paged_graph_validator.cc:223`, `paged_warm_validator.cc:314`, the scrub) or read the agent's own fresh build (`OwnedSegmentInputs`, `paged_overlay_assembler.cc:293`, `paged_snapshot_builder.cc:342`). The registry merge carries `first_touch()` with the mapping (`segment_registry.cc:30`). The bitmap is keyed by granule only. That is sound because every tested reference is authentic: two different authentic references cannot share a granule without overlapping pages, which needs a SHA-256 collision.
5. **Threads.** `VerifiedPages` uses `fetch_or(release)` and `load(acquire)` on immutable bytes, so a race verifies twice and does no harm. P3.1 ran 8 threads reading every key over a store with 40 flipped bytes. Each thread saw 19,690 ok and 310 `SourceFailure`, with no wrong value and no thrown exception. I did not run TSan locally (CI's `linux-tsan` job covers the suite).
6. **Refusal paths.** Every throw in `TrustedRestart::open` happens before the registry is returned. A partial registry is dropped with its mappings, and nothing durable is written. The trusted path runs only for a cold start with no admission and no prior (`generation.cc:358`), inside the shared lease lock, so the collector cannot remove `.live` mid-read.

## Verdict

**APPROVE WITH CONDITIONS.**

- **Blocking items:** none for landing. SR1 and SR2 have no wrong-value or privilege path.
- **Conditions before the step is called done:**
  - C1 (SR1): the five totals checks at restart, with tests.
  - C2 (SR2): references outside the registry or a mapping become `SourceFailure` in the agent path; `root.tree.segment` must be among the rows; the P2.5 test.
  - The note's binding and counts sentences are corrected.
- **Non-blocking:** SR3 (scrub through the mapping), SR4 (withdraw bits on a scrub mismatch, with the repair), SR5 (`O_NONBLOCK` on record opens), SR6 (charge the ledger).

**Probes** (all kept under the review worktree's `.scratch/sr/`, `Detent/.scratch/sr-s3/.scratch/sr/`; sources and logs also copied to `Detent/.scratch/review-namespace-s3-security/` in the main checkout): `live_record_fuzz2.cc`, `build_fuzz2.sh`, `fuzz2.log`, `corpus2/`; `probe_trusted.cc`, `probe_targets.cmake` and `probe_add.cmake` (built with `-DCMAKE_PROJECT_detent_INCLUDE`, no source edit), `probe.log`; `branch-tests.log`, `branch-t5.log`; the ASan/UBSan build `build-asan/` (worktree only).

**Not run:** TSan, LSan (macOS AppleClang has none), Linux (for SR3's page-cache case), the collector over a P2.5 store (whether it deletes the live generation's packs), the 100K and 5M stores (probes used 20K knobs and 5 packs), a coordinator-level run of the scrub task, ACL-based write on store files, and the branch's oracle mode under sanitizers.
