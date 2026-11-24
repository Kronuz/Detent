# journal-turn-sector-checks review: durability

Commit: journal:f1ca348a5bd8e7e3dfab8d78af2f951c8801f85a
Verdict: CHANGES
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Reviewed as a format about to freeze: RFTW0002 (D147; docs/design/turn-log-cut-header.md O3b,
accepted at journal:9e0649d), origin/main (journal:e58f977)..f1ca348. The lens is durability and
carries the correctness questions. The bytes hold: under the crash model every sector verdict maps
to the right outcome, the rewrite of the shared sector never changes an earlier byte, and no probe
case loses an acknowledged turn silently. One defect blocks, in the readers rather than the bytes:
a live read of the sector a turn in flight is rewriting throws `Corruption` (F1).

Notes: run as a subagent of the lead, not as a session titled `detent · reviewer
journal-turn-sector-checks durability`. The probes are in `.scratch/detent/review-sector-checks/`
(the lead's path). They ran light, not strict: they are single-process logic probes, not
measurements, and the strict queue held them 85 minutes (261123-141027-53462d and
261123-141028-8d2e54, cancelled before they ran).

## Evidence I relied on

- A1, 261123-132700-1f99e4 at f1ca348: the design's probe (journal:b335b69) at main reproduces 36
  of 201 newest-tail shapes silent; its port (`journal_turn_log_power_test probe`) at the fix
  reports every case, 0 silent: 258 crossing, 436 of 441 changed sectors (the other 5 hit the
  stored low bits, harmless by design), 5,525 blocks (+2 harmless), 103 blocks of the newest turn.
  The port enumerates every sector the newest turn changed and every crossing on RFTW0002's
  layout: a superset of the design's shape classes, not its 201 byte positions (risk 4).
- A2, 261123-132700-6bde99, failing without the fix in 261123-104123-1ecb5a: `sector_verdicts` (17
  checks), `stale_alias` (5), `newest_turn_damage` (damage reported 447, tears read as tears 487),
  and T9 at 8 seeds (31,268 checks; 1,994, 1,338, 1,136 and 1,132 crash points at 512, 1,024,
  2,048 and 4,096-byte blocks), 0 failures (risk 1).
- A3, 261123-132701-1af7dc, failing for a 32-bit-only mutant in 261123-104128-58508b: a stale
  sector from S - 2^32 reads stale. `verdict()` (turn_log.h:141-148) checks the CRC under the full
  64-bit segment, then under the nearest lower segment with the stored low bits, or S - 2^32 when
  those are S's own, guarded below 0 (risk 1).
- A4 bytes, 261123-104143-3708f8 at dea3757, whose `include/` differs from f1ca348 only in a
  framing.h comment: log bytes per turn -5.7 to -6.6% at T9's sizes and 256 B, +0.97% at 4 KiB.
  The fix writes 509-511 B more than it logs in every row (1,348.6 vs 838.2 B at 2 KiB blocks;
  34,184.6 vs 33,673.8 B at 4 KiB records): the partial sector again plus the zero tail. A4 reports
  all three views (log growth, bytes written, sectors and pages touched), which is the honest set:
  devices and the page cache write whole sectors and pages under either format, so sectors touched
  is the device cost and the extra syscall bytes are a memcpy. The journal's sectors and pages
  figures were measured light; 261123-132656-758884 repeats them strict (queued). Latency,
  261123-132700-4d14e4: every arm sits in macOS's 21-24 ms fsync regime (risk 5).
- CI 45269807268 at f1ca348: every job green (Linux TSan, ASan+UBSan, gcc, clang, arm64; FreeBSD;
  macOS x86_64 and arm64).

## What I checked myself

Probe jobs at the fix and at main (`REF=e58f977` builds main's tree with the same probe):
261123-154419-0e1c87 and 261123-154421-5ca305 run all four probes; 261123-155247-def919 and
261123-155249-a33c96 rerun `midwrite` and `tail_rot_probe` after a fix for RFTW0001's short
writes (5ca305's exit 1 is that probe bug) and a "W whole" class. To rerun: `bash run.sh build
A.cc B.cc ...`, then `bash run.sh run A.cc [args] :: B.cc [args] ...` in a checkout.

1. **The shared sector under power cuts (risk 2).** `shared_sector_probe invariant`, 0e1c87: of
   18,000 writes (512 B to 32 KiB blocks, records to 700 B and to 12 KiB), 15,717 rewrote a shared
   sector, and all 4,169,182 earlier bytes they rewrote equal the disk's. Every write is whole
   sectors from the sector holding the position, zero past the turn's end in its last, and every
   acknowledged record reads back: 130,007 checks, 0 failures (main: 0 shared). So under the crash
   model the shared sector is its old version or its new one, each verifying and each holding the
   turn before: the rewrite cannot tear an acknowledged turn. The order is main's, one write, then
   `sync_data`, then `completed()` (`append`, turn_log.h:588-596; the open record, 883-889), and
   turn_driver.h and host.h are unchanged.
2. **Verdicts and outcomes (risk 1).** A1-A3 above, plus `tail_rot_probe` for damage where the log
   holds no data (F2) and a tear inside a sector (F3). No case at the fix is silent.
3. **idle_damage (risk 3).** `idle_damage_probe`, 0e1c87 and 5ca305: F4. No report is lost.
4. **Fixtures and fuzz (risk 4).** fuzz/replay.cc:20-39 writes real segments with
   `TurnLog::append` and feeds them, corrupted, to `fuzz_turn_log`, so the fuzz target reaches
   RFTW0002 unchanged (Ruling 7). The fixtures are A6's, the lead's gate.
5. **Readers during a turn.** `shared_sector_probe midwrite` and `threads`: F1. Read: `Host::park`
   (host.h:575-593), `recover_shared` (journal.h:590-630), `TurnDriver::run` and `TurnBarrier`
   (turn_driver.h:60-100, 350-366), and completion at journal's pin d8b538c
   (linux_completion.h:72, 127; bsd_completion.h:60).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A live read of the sector a turn in flight is rewriting throws `Corruption`: a correct, durable log reports its own acknowledged records as changed. D3 starts each write at the partial sector (turn_log.h:650), rewriting the turn before's bytes there, and `Sectors::read` throws "log record changed since it was written" on any sector that is not current (turn_log.h:263), with no durable copy of that sector to read instead. Two readers run while the write is in flight. `Host::park` peeks every idle group (host.h:589), and idle-groups D10 declares it safe between `begin()` and `completed()` and from the waiters a turn resumes (docs/design/idle-groups.md:286); its throw reaches the consumer's idle policy. A Store's open of a released group reads `live()` (journal.h:602) and fences the Journal on the throw (journal.h:630). The driver awaits the barrier while the loop runs other work (turn_driver.h:360-362), and the write itself runs off the loop's thread: io_uring with `IOSQE_ASYNC` (io-wq) or completion's fallback worker (linux_completion.h:127 and 72 at d8b538c). Forwarding copies idle groups' checkpoints into the newest turns, so the sector a turn rewrites can hold the very records park reads. RFTW0001 writes from the position on and never touches an earlier byte. | A write half copied into its first sector, then a peek of every group (deterministic, MemoryIO): at the fix 4,414 of 4,768 such turns threw `Corruption` (5,579 of 28,080 peeks); at main 0 of 4,779 (0 of 28,140); after each barrier every group read back at both (def919, a33c96). Real threads on macOS APFS: 0 torn reads in 2,340,400 peeks against 199,382 racing pwrites (0e1c87; main 0 in 1,536,562, 5ca305), so APFS serializes them. Linux is argued, not run: ext4 and btrfs copy a buffered read out of the page cache without the inode lock a buffered write holds (XFS takes it shared for reads), so a read can see part of a write's copy. The code assumes POSIX's read/write atomicity (XSH 2.9.7), which those filesystems do not give. | Serve the sector a staged turn rewrites from memory: while `staged_` is set, a read of sector `offset_ / sector_data` of `segment_` takes its bytes before `offset_` from `partial_` (the durable version's data, all an acknowledged record needs) instead of the file; or keep the previous write's last 512 bytes and substitute them in `Sectors::load`. Test: the `midwrite` case in turn_log_test (half-copy the first sector, peek every group, none throws), failing at f1ca348. Making park and `live()` wait for `completed()` also closes it, but narrows D10: the owner's call. |
| F2 | no | gap | Rot in a never-written block header now costs every group. The scan checks the first sector of every block of every segment, spares included (turn_log.h:437-445); a damaged one is damage (rule 1) in a gap no manifest names (rule 3). So one flipped or unreadable first sector in the newest segment's unwritten blocks or in a spare reports every open group lost: 1 sector in 64 of that space at the default 32 KiB blocks. RFTW0001 read the same flips as the end and lost nothing, and threw EIO from recovery on the same unreadable sectors, so for unreadable ones, the way drives report rot, the fix is better. Not silent. The design's Limits say such a sector is read only behind a torn write and costs that turn's groups (turn-log-cut-header.md:229); turn-log-damage.md's Limits omit it. | `tail_rot_probe`, 24 seeds, fix vs main (def919, a33c96). Next block's first sector: flipped, every group lost in 21 of 21 (main: harmless in 23); unreadable, 21 of 21 (main: EIO in 23). A later unwritten block: 18 of 18 (main: harmless in 22). A spare's block 0 or block 3, flipped or unreadable: 24 of 24 each (main: harmless, or EIO). | State it in turn-log-damage.md's Limits and correct the design's line. Optional reader change, no format change: a write covers every sector from the previous end through its own, so past an unwritten or stale header no later block of that segment can hold an acknowledged turn, and past an unwritten block 0 no later segment can. Damage there can be read as the end, leaving two sectors able to cost every group: the first header after the end and the next segment's block 0. The owner's call. |
| F3 | no | gap | Outside the crash model, a tear inside a turn's first sector (pmem without BTT, a device that shears a sector) now costs every group, the turn before's included: the rewritten sector's trailer no longer verifies, and the sector is in the newest block, whose manifest is not written yet. RFTW0001 rewrote the same earlier bytes unchanged, so the same tears mostly cost nothing. Reported, never silent. The Limits say a tear inside a sector "reads as damage" (turn-log-damage.md:114) without saying it reaches acknowledged turns. | `tail_rot_probe` shorn, 24 seeds: at the fix, each tear point (one byte past the earlier data, halfway, the whole data area) costs every group in 23 of 23; at main 61 of 72 cost nothing (1 with W whole) and 11 cost every group (def919, a33c96). | One clause in the Limits: a tear inside a turn's first sector loses every group, the turn before's included. |
| F4 | no | nit | idle_damage's new expectation (group 6 keeps every record) is right, and no damage report is lost (risk 3). Ruling 10 credits RFTW0002, but the change comes from the test moving its damage: main's test damages the two blocks after the first 900-'Z' run, the fix's the two after the first 'Z', blocks 1-2 instead of 3-4. On RFTW0001 the new placement gives the same answer; on RFTW0002 the old one lands in the newest block, whose manifest is not written yet, and both open groups are lost (rule 3). A later group bounded by an orphaned block's manifest is no longer exercised in host_test; lost_bound_test still bounds groups with later records kept (test/lost_bound_test.cc:170, 210, 233). | `idle_damage_probe`: the new placement, at main and at the fix, gives group 6 stored 1-5 and group 1 bounded at 4 (5ca305, 0e1c87). The old placement gives group 6 bounded at 2 with 3 records at main, and groups 1 and 6 lost at the fix. | Reword Ruling 10 (journal only). |
| F5 | no | gap | A1's damage enumeration never has a newest turn that opens a segment ("0 runs where W opens a segment"), nor did the design's probe. T9's crash runs reach such turns; its damage cases reach one only when a seed's newest turn opens a segment. | 1f99e4's output. | Optional: a seed or case whose newest turn opens a segment, damaged in its first sector. |
| F6 | no | nit | The new and moved cases run on MemoryIO (test/turn_log_test.cc:271, 392, 452; Ruling 11, journal:a5670ba), which journal-drop-memory-io deletes. | .campaign/detent/quests/journal-drop-memory-io.md | The lead orders the two quests, or the cases move to faultfs. |

## Not run

- Linux: no VM is up on this host (port 18772 closed, its directory gone) and docker does not
  start, so F1 on ext4 and btrfs is argued from the kernel's read path, not measured. macOS APFS
  was measured and shows no tear.
- No strict or exclusive measurement of my own; A4's strict head jobs are the lead's (758884
  queued, 4d14e4 done).
- No fuzzing, no rerun of A1-A3 (their jobs ran at f1ca348), and A6's fixtures only through CI.
- FreeBSD: CI only.
