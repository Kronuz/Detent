# Detent review: namespace S3b, repair of a damaged pack (option b)

- Date: 2026-11-04.
- Reviewed: `namespace/s3b` at `Detent:d4692ba` (last code `Detent:ea74c9f`), `git diff Detent:5475baf..Detent:ea74c9f` (31 files), the ledger `docs/status/namespace-s3b.md`, `docs/mechanisms/trusted-restart.md` ("Repair of a damaged pack").
- Checked against: `docs/design/namespace-delivery.md` D10 (D10.1 to D10.3), D8.2, D11; the S3 architect review's A7 (`Detent:2b0b9cb`); the S3 security review's SR3 and SR4 (`Detent:5f2385b`); the ledger's done criteria and its two owner questions.
- Checked by: a macOS x86-64 RelWithDebInfo build with relay (`build-arch-rel`): the 14 touched tests pass in 124 s (`.scratch/s3b-arch/tests-rel.log`). An ASan/UBSan build (`build-arch-asan`) built, but its test run was still queued on the host's light-hold semaphore when this review was written. Probes, all in this worktree's `.scratch/s3b-arch/`: `probe.cc` (built by `build-probe.sh` into `probe-rel`), `run-probes.sh` and the seed store `work/store` (99,990 + 10 knobs, revisions 1 to 3, 69 MB). Results are in `work/{lease,q1,q2,q2damaged,crash-repair,crash-collect,crash-publish}.log`. `mapprivate.c` shows that macOS does not show a `pwrite` through a touched `MAP_PRIVATE` page, so Q1 simulates the Linux case by writing into the private mapping (`mprotect`, copy on write; the file is untouched).
  - Not run: benchmarks, fuzzing, TSan, Linux, real power cuts, the sanitizer run of the touched tests (queued), the warm-recovery timing (below), and the leased-collection control (`leaseclean`). `probe.cc` holds the two extra modes; `build-probe2.sh` builds them into `probe2-rel`, which was still queued and was not used here.

## Verdict

**APPROVE WITH CONDITIONS: the repair heals T5's damage and every crash point probed heals, but a reader that keeps the damaged generation leased silently stops all collection (L1).**

What holds, and reproduces:

- The heal path. `test_delivery_repair heal`, `delta`, `unchanged` and `relay`, and `test_testing_trusted_restart heal` pass locally. The repaired generation shares no pack with the damaged one: the snapshot encoder builds from the incoming state only (`paged_snapshot_plan.cc:20`), as A7.2 said.
- `Repair` cannot publish over a newer generation. `begin_paged` requires the selection to equal the plan's base (`disk_store.cc:1563`), and `Repair` requires the base's identity and profile and a revision at or after the base's (`paged_plan.cc:84`, `disk_store.cc:1578`).
- The collector's `tolerate_mismatch` applies only to sweep candidates the marking left unmarked (`paged_segment_collection.cc:482`). The walk's own pack reads still refuse a mismatch (`:412`). An I/O error or short read throws, not "damaged" (`paged_segment_read.cc:325`). An unmarked pack is removed whether or not its checksum matches, so tolerance does not widen what is reachable-and-deleted.
- Crash windows at the writer level (probe `crash`, SIGKILL at 12 delays spread across each operation, then a restart, a re-detection and a repair if needed, a collection, and a full walk). None of the 36 kills served a wrong value or lost the last good generation, and every one healed with 0 stray packs (`work/crash-*.log`):
  - `repair` (5.0 s): 10 kills before durability restarted on the damaged revision 3 (963 `SourceFailure`, 0 wrong); the scrub reported 23 packs, the store re-repaired and healed. 2 kills after durability restarted clean.
  - `publish` (7.6 s): 7 restarted at revision 3, 5 at revision 4, all clean.
  - `collect` (4.4 s): 12 of 12 clean. Most of these kills land in the writer's open, not inside the sweep, so this is weak evidence for a kill between owner removal and pack removal.

What blocks: L1 (AR3).

## Findings

**AR1. Q1: mapping `invalid_argument` to `SourceFailure` covers 4 of 150 probed outcomes; most of the rest are SIGSEGV. CONFIRMED (`q1`).** Probe `./probe-rel q1 work/store ./probe-rel 30`: each trial recovers trusted, reads all 100,000 knobs so that every page on the path is verified, rewrites one verified page in the private mapping, and reads all 100,000 knobs again.

```text
children (branch child count, offset 24):  30 SIGSEGV
body     (leaf body length, offset 28):    29 SIGSEGV, 1 runtime_error "invalid validated definition region"
prefix   (prefix length, offset 20):       30 wrong MissingKnob for a present knob
childref (a byte of a child reference):    25 no effect, 2 SourceFailure, 3 invalid_argument "invalid paged local image"
random   (4 random bytes):                 12 no effect, 8 wrong value, 5 SourceFailure, 3 runtime_error, 1 invalid_argument, 1 SIGSEGV
```

The causes are in `paged_read_state.cc`, which re-reads header fields of an already verified page on every lookup:

- `:96` and `:101` index `bytes[begin + middle * child_bytes]` with the page's own `children`, unchecked. That is the SIGSEGV.
- `:86` calls `bytes.last(read_integer(bytes, 28, 4))` with no bound.
- `:103` calls `subspan` out of range, and `:46` `decode_reference` then throws `invalid_argument`. That is CI's throw.
- `BorrowedDefinition` throws `runtime_error` (`borrowed_definition.cc:9`).
- `ReadView::try_eval` calls `evaluate` outside its try (`view.cc:390`).

The read path is pre-existing, not S3b's code, and it applies equally to reader processes. Recommendation below.

**AR2. Q2: a forward snapshot over an unreported damaged prior fails its warm recovery after the new generation is durable. CONFIRMED (`q2damaged`); the warm cost is PLAUSIBLE.**

- `./probe-rel q2damaged work/store`: after T5's flips on revision 3, `GenerationWriter::publish_json(4)` threw "local page failed first-touch verification" while revision 4 was already selected. In the coordinator that failure goes to `settle_failure` and fences, as the note's Limits say.
- The cold full walk of a freshly published 100K generation took 739, 1,075 and 906 ms over three rounds. Each round hashed 24,059,400 segment bytes, made 237,958 page digests and validated 99,969 definitions (`work/q2.log`, loaded host, indicative only).
- My recovery-level "warm" run did not take the warm path (`metrics.warm == 0`: passing `prior` without an admission is not warm), so I have no warm number. A snapshot shares no pack, so the warm path must hash the same new generation plus whatever it reads of the prior; cold is at most warm by construction. To settle the number: `./build-probe2.sh rel && ./probe2-rel q2pub work/store`, which interleaves `publish_json` (warm) with `repair_json` (cold) six times and prints ms and page digests.

**AR3. While a reader leases the damaged generation, a collection removes nothing at all and reports no error. CONFIRMED symptom (`lease`); the cause is PLAUSIBLE.** `./probe-rel lease work/store`:

```text
69 packs before (46 unreachable from revisions 1-2, 23 of the damaged revision 3); collect with the damaged generation leased: returned; unreachable undamaged packs left 46 of 46, damaged generation's packs left 23
after the lease ends and a new collect: unreachable left 0, damaged generation's packs left 0
```

- The likely cause: the leased root is walked to mark it (it has no certificate in a fresh process). The walk reads the damaged pack without tolerance, `SegmentProofError` fails the whole collection (`paged_segment_collection.cc:412`, `:490`), and `fail` swallows it.
- Before S3b, a damaged generation stayed selected, so this state was rare. After a repair it is the normal state while any reader still holds the old generation. Disk-pressure collections (`coordinator.cc:723-731`) then reclaim nothing until the reader lets go, so publication can stay refused under pressure.
- `test_repair.cc:675` calls `request_collection()` in a loop until the packs go, and the writer-level `heal` checks only the damaged packs. Neither would see this. Errors pass silently here.
- Control that attributes the cause: `./probe2-rel leaseclean work/store` (the same flow with no damage).

**AR4. The scrub treats transient errors as damage and does synchronous I/O on the coordinator loop. PLAUSIBLE.**

- A failed `open` of the directory (`pack_scrub.cc:32`) or of a pack (`:94`), including EMFILE, reports damage. So does a short or failed `pread` (`:108`), EINTR and EIO included. Each such pass requests a full Replace and a repair. A persistent EMFILE repairs once per pass, indefinitely.
- `scrubber->step` (`coordinator.cc:683`) now preads from disk on the coordinator's own thread, up to 2 MiB per 20 ms tick, where it used to hash memory. A slow disk stalls the loop.
- Settle with a probe that runs `PackScrub` with `RLIMIT_NOFILE` lowered after `begin`.

**AR5. The `Repair` intent drops the same-cursor fork check, and `pending` clears only on a `Repair` publication. PLAUSIBLE.**

- At an unchanged cursor, a Replace whose state differs from the selected one now publishes. Under `Maintenance` it threw "same-cursor snapshot changes logical state". That is acceptable, because the base cannot be trusted for the comparison, but the note should state it.
- `diagnostic.repair.pending` clears only at `coordinator.cc:496`. A Replace of another identity (the `TrustedReset` path) or one behind the base (refused at `paged_plan.cc:84`) leaves the repair pending. Snapshots stay forced and compaction stays skipped (`:574`) until a `Repair` lands. The refused case backs off; neither case loops hot.
- Settle with a `test_repair` variant whose server answers the forced open at revision 0 or under another identity, checking `repair.pending` and `failures` after 10 rounds.

**AR6. Crash windows hold at the writer level. CONFIRMED (`crash`); coordinator-level re-detection is PLAUSIBLE.** The numbers are under What holds. The repair request is coordinator memory (`coordinator.cc:124-136`). A restart re-detects it at the first scrub pass, which is the `heal` test's own starting state (it restarts over damage), or at the next delta's first touch (`:1085`). At the default 512 KiB/s, one pass over this 23 MB generation takes about 46 s. Collect-window coverage is weak (above). Settle with `N=48 ./probe-rel crash work/store ./probe-rel collect 48` run with a kill hook inside `PagedSegmentCollection` between `RemoveOwner` and `RemovePack`, or by timing the open separately.

**AR7. Proof gaps.**

- No test collects while the damaged generation is leased and checks that the other unreachable packs go (AR3).
- No test feeds the read path a verified page that changed (AR1).
- `t5`'s refused-update step (`test_trusted_restart.cc:281`) encodes the AR2 behavior as expected. It changes if Q2 is taken.

## Owner questions

**Q1 (verified page rewritten under the private mapping).** Recommend (b'), not (b).

1. Bounds-check, at read time, the page fields the lookup re-reads: `prefix + children * child_bytes + body == length == bytes.size()`, before `:86` to `:103`. That is a few compares per page visit. It removes the SIGSEGVs, which are crashes of the agent or a reader process from a write to the store.
2. Map every `std::exception` other than `bad_alloc` and `StoragePressure` that escapes `definition()` or `evaluate()` to `SourceFailure`, in both `try_get` and `try_eval` (`view.cc:356`, `:383`, with `:390` moved inside the try).

Wrong values, such as the 30 wrong `MissingKnob` results from the prefix rewrite, stay the documented limit until the scrub withdraws the bits.

Options: (a) leave as documented, which keeps the crash; (b) as proposed, which closes about 3% of the outcomes probed here; (b') above. The read path is user-visible behavior, so this is the owner's call. It does not block S3b, but I would close it before the gate (C1).

**Q2 (warm recovery of a snapshot against a damaged prior).** Recommend recovering every snapshot plan cold: key `populate` (`disk_store.cc:1599`) and `capture` (`:1809`) on "the plan is a snapshot" (`Fresh`, `Forward` from a snapshot, `TrustedReset`, `Repair`), not on `Repair` only.

- The fence it removes is confirmed (AR2).
- The cost is the cold walk measured above, about 0.7 to 1.1 s and 24 MB hashed at 100K on a loaded host. The warm path hashes the same new generation, so the change should cost nothing extra; `q2pub` puts a number on it.
- In T5, "update over a damaged base is refused" becomes "the update publishes and its recovery is clean", and `after == revision + 1` becomes the only outcome.
- Deltas and compactions keep warm recovery, since they share packs.

## Blocking items

- L1 (AR3): a collection must not stop at a leased damaged generation.
  1. Attribute the cause with `leaseclean`.
  2. If it is the walk, mark a root whose walk fails proof conservatively: every segment its root or `.live` names stays marked. Then continue with the other roots and sweep.
  3. Make the failure visible (`terminal_collection_error` or a status counter), not swallowed.
  4. Add a test: with the damaged generation leased, a collection removes the other unreachable packs and keeps the leased generation's; once the lease ends, the next collection removes them. `test_repair.cc:675` stops looping on `request_collection()` and asks once after the lease ends.

## Conditions

- C1 (AR1), before the gate: the owner rules on Q1. If (b'), add a test that rewrites a verified branch page's child count and a leaf's body length in the mapping and expects `SourceFailure`, never a signal or a throw.
- C2 (AR2), before the gate: the owner rules on Q2. If taken, update `t5` and the note's Limits, and record `q2pub`'s warm and cold numbers in the ledger.
- C3 (AR4): retry a failed open or read once before reporting a pack damaged, and do not report the whole set when the directory open fails. Move scrub reads off the coordinator thread, or bound them per tick to a slice measured on a slow disk.
- C4 (AR5): the note states that a repair accepts a same-cursor state that differs from the selected one. `pending` clears, or the coordinator records the stall, when a non-`Repair` publication replaces the selected generation.
- C5: the sanitizer run of the touched tests (queued in `build-arch-asan`) passes, or CI's ASan/UBSan job on the fix commit stands in for it.

## Fix list for the developer

1. L1: conservative marking of a leased root whose walk fails proof, the error surfaced, the test, and `test_repair.cc:675` changed to ask once.
2. If the owner takes Q1 (b'): read-time header bounds in `paged_read_state.cc:46-103`, broad exception mapping in `view.cc:350-390`, and the test.
3. If the owner takes Q2: `populate` and `capture` keyed on a snapshot plan, `t5` updated, and `q2pub` numbers recorded.
4. C3 and C4.
