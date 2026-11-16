# drivers-completion-seam review: correctness

Commit: completion:269a4c4cbfed20a9dbd2616c0e7793adf7a32db4 (branch quest/detent/drivers-completion-seam, against main completion:3f82cda)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- CI run 43829040088 at completion:269a4c4: all nine jobs green, freebsd-x86_64 included, so f2dc801's
  test fix holds on FreeBSD (`completion_sync_data` Passed, log lines 1129-1130).
- Mutant jobs at completion:269a4c4, each exit 0 (killed): 261116-000751-c47e20, 261116-000752-68fa7c,
  261116-000752-06ce40, 261116-000753-959b2b, 261116-000807-2563be, 261116-000808-2719f9,
  261116-000808-b88aed, 261116-000809-5174e5; grep job 261116-000911-53e42f (exit 0).
- A4 rows 261115-230734-0b0a3e (exclusive) and controls 261115-230745-462b77 at completion:f579c4e:
  `git diff f579c4e 269a4c4 -- include bench` is empty, so they measured this code.
- Not relied on: suite 261116-000744-e8ac8c was still queued when I wrote this; CI covers it.

## What I checked myself

Scripts under `.scratch/review-drivers-completion-seam-correctness/` in the Detent home checkout.

- `lane_review.sh` + `lane_review_probe.cc` (job 261116-001827-3663c6, light, macOS, 16 Bulk tasks, 60
  turns): open-loop turns at a fixed period, a cancelled Bulk "victim" every 40 ms with its token
  requested as soon as it waits, and two Turn sources. Light level, so rows are noisy (lane rows
  spread 1.8-23.8 MiB/s); only the large effects below are read from it.
- `stress.sh` + `stress_probe.cc` (job 261116-010643-1e71d2): 400 seeded rounds of 2-10 concurrent operations on
  one `AsioCompletionDriver`, random lanes, lengths, `run`/`run_one`, tokens requested before, from a
  completion hook, or from another thread; every fifth round stops the loop mid-flight and tears down
  (close, destroy_roots), Debug + ASan/UBSan. Checks against a sequential model: never two primitives
  in flight, nothing issued or completed after its run returned, issued == completed, every run ends,
  outcome matches the token, queue idle and slot free after the round. Result: 400 rounds (80 teardown),
  0 violations, ASan/UBSan clean. The first runs (261116-001737-80225e, 261116-002001-ff4d65,
  261116-003320-48157b, 261116-010521-f2acb6) were probe bugs: a build-dir path, a name clash, and a
  teardown that destroyed the queue undrained, which `~BsdCompletionQueue` answers with
  `std::terminate` by contract (`bsd_completion.h:70-72`; see C8).
- `extra_mutants.sh` (job 261116-002636-bc1cb7): seven seam mutants the quest's eight leave out; six killed, one
  equivalent survivor (Q5).
- Read: the full include diff, `seam.h`, the Asio slot (`asio_completion.h:120-205`), the BSD and Linux
  queue paths, `sync_data_test.cc`, `seam_test.cc`, `asio_seam_test.cc`, `check_seam.py`, the CI log.

### Q1: the token at primitive boundaries

Two cases, with different answers.

1. **In flight: acceptable as is.** A cancelled caller waits for the one primitive it issued (one
   64 KiB write, or one `F_FULLFSYNC`, about 20 ms on this Mac, longer under load). Releasing it earlier
   needs a reaper that outlives the caller's frame; the operation's buffers are already kept by the
   queue (`pending_`), so it is a latency cost, not a safety one. It fits journal-driver A2 (staged
   records written before return) and consensus-driver A3: at teardown the slot holder's `Hold` posts to
   a closed scheduler (dropped, task teardown phase 2) and `~Admit` unlinks a waiter. D8's row "only the
   caller is released" should be amended to say the caller is released when the primitive completes.
2. **Waiting for the slot: must change.** The token is read only when the slot reaches the waiter, and
   no cancel callback is registered, so a cancelled caller waits out the whole queue ahead of it. The
   ruling's "costs at most one primitive's latency" is false. Measured (probe 261116-001827-3663c6):
   one Turn source, 16 Bulk: release p50 187-218 ms, max 247 ms (FIFO 24 ms); two Turn sources:
   1,430 and 1,807 ms, i.e. until the turns stopped. D8's first row (a call waiting for admission:
   unlinked, never submitted, completes with `task::cancelled`) is what this wait is, and deadlines
   (D8) cannot bound a fan-out while it holds. Fix: register a `cancel_callback` in `Admit` that claims
   the waiter and hops to the owner (`post_call`), which unlinks it if still linked and resumes it to
   throw; a waiter already handed the slot loses the claim and proceeds as now (D8's waiter protocol).

### Q2: concurrent runs on the Asio driver

No race, double completion or late completion found. All slot state is touched on the owner only;
`slot_taken_` stays true while anyone waits (`pass_slot` hands it over, `asio_completion.h:180-188`), a
popped waiter is unlinked before its resume is posted, the timer and notification descriptor are used
by the slot holder only, and `parallel_group` reaps both waits before resuming. The stress probe above
checks these against a model, teardown included: 400 rounds, 0 violations (261116-010643-1e71d2). Two drives of the same operation are
no longer refused (the old `active_` refusal is gone and `in_flight()` is false while it waits): a
misuse that now interleaves the two drives' primitives instead of failing (C5).

### Q3: SyncData per backend

- Linux ring: `IORING_OP_FSYNC` + `IORING_FSYNC_DATASYNC` on a zeroed SQE (`linux_completion.h:109`,
  `:278-285`), errors as `system_error(-res, generic_category)` like Sync; `fdatasync` semantics. CI
  cannot show it ran: `native_ring` returns early, printing to stdout, when the ring is unavailable
  (`sync_data_test.cc:197-200`), ctest prints no stdout on a pass, and no other completion test uses
  `RequireNative`. The Linux runners are Ubuntu 24.04 VMs, so io_uring is likely there, but nothing
  proves it (C2).
- FreeBSD: `aio_fsync(O_DSYNC)` is `fdatasync` semantics. A closed file's EBADF at submission becomes
  the operation's error, `system_error(EBADF, generic_category)`, the same as the worker path, and
  counts `native_rejected` as Read/Write/Sync refused at submission already do; the product behaviour is
  right and f2dc801 fixes only the test. Any refusal other than ENOSYS/ENOTSUP/EAGAIN is completed as an
  error, not sent to the worker; on a FreeBSD without `O_DSYNC` support (before 13.0) SyncData would
  fail with EINVAL (C7).
- Worker: `file_sync_data` (`posix.h:184-192`): `fdatasync` on Linux, `F_FULLFSYNC` on macOS (correct:
  macOS `fdatasync` does not flush the drive cache), `fsync` elsewhere, so FreeBSD's worker path is
  stronger than its native path. Both meet `io.h:82-84`'s contract.

### Q4: strict priority and Bulk throughput

Strict priority at admission is X13's literal ask ("turn primitives go ahead of artifact and snapshot
IO"). The -85% is mostly the probe's closed loop, not starvation: the lane arm runs 2.4x as many turns
per second (37-40/s against 15/s in my closed rows), and each turn holds the one slot for its ~20 ms
SyncData. Under FIFO, every turn primitive waits for all 16 Bulk waiters, so Bulk gets a fixed ~32
chunks (2 MiB) per turn (1,939-2,016 Bulk writes per 60 turns in every flat row), and FIFO cannot even
serve the offered turn rate: open loop at 45 ms it served 14-17 turns/s of 22, at 30 ms 9-16 of 33,
turn p99 up to 1.3 s. The lane served the offered rate and gave Bulk the rest of the slot (up to 23.8
MiB/s at 45 ms). With one Turn source per driver (D2, D6), Bulk is never starved: `pass_slot` at the
end of each turn operation goes to the Bulk head before the next turn queues, so Bulk gets at least one
primitive per turn. With two Turn sources it is starved outright: 17 Bulk writes in a whole run
(0.6-0.7 MiB/s, against 976 flat). A bounded share (one Bulk primitive per Turn primitive once Bulk
has waited) would add at most one 64 KiB chunk (~1.5 ms here) to each turn primitive's wait, keeping
most of the 21 ms to 0.5 ms win; I did not implement it. Recommendation: keep strict priority, and
either state and check one Turn source per driver or add the aging bound (C3, the lead's call).

### Q5: the mutants

The eight cover SyncData dispatch and error, Held ticket order, Held drop, the token in Inline and
Held, the lane, and a second loop beside `drive_synchronously`. They leave out the Asio token checks,
the slot hand-over and Held's posting and teardown; `extra_mutants.sh` (261116-002636-bc1cb7) adds
seven. Killed: `completion_ref` dropping the token, no token re-check after the slot wait, Held
resuming inside `deliver`, a Bulk holder not yielding to Bulk waiters, `~Hold` not passing the slot,
`~Wait` not leaving the queue. Survived: a Turn holder yielding to Bulk waiters too; it is equivalent,
since `pass_slot` takes the Turn lane first and hands the slot straight back (one extra post), so no
test can tell it apart by order. With these, the eight plus the existing tests cover Held ordering,
token propagation through the reference and every driver, and Inline as the one loop (the grep and
its mutant). The Asio cancel-while-waiting gap (C1) is uncovered because the behaviour is missing.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | yes | defect | A caller cancelled while waiting for the Asio slot is not released until the slot reaches it: 187-247 ms with one Turn source, 1.4-1.8 s (until turns stop) with two, against D8's admission row and the ruling's "at most one primitive's latency" | probe 261116-001827-3663c6; `asio_completion.h:148-170` (`Admit` registers no callback) | `cancel_callback` in `Admit`, hop by `post_call`, unlink if linked, resume to throw; test: a waiter behind a gated holder, token requested from another thread, completes `cancelled` while the holder's primitive is still blocked (gate opened after the release is observed); amend the journal ruling and D8's in-kernel row to "released when the primitive completes" |
| C2 | yes | gap | The Linux ring SyncData path and its EIO fault are not shown to run in CI: the section skips silently when io_uring is unavailable | `sync_data_test.cc:197-200`; CI log 43829040088 shows Passed only | make the ring section fail on Linux unless an opt-out env names the host as ring-less (or construct with `RequireNative` there); CI green then proves A1 for the ring |
| C3 | no | gap | Strict priority starves Bulk (catch-up, snapshot install) when two Turn-lane sources share a driver; the one-Turn-source assumption is stated nowhere and nothing checks it | probe 261116-001827-3663c6 two-source rows: 17 Bulk writes per run | lead's call: document and Debug-assert one Turn operation at a time per driver, or admit one Bulk primitive after N Turn primitives while Bulk waits |
| C4 | no | gap | MEASUREMENTS "Turn lane" reads the -85% Bulk drop as the designed trade; it is mostly the closed loop's turn rate (2.4x more turns served); an open-loop row would show the real trade | probe 261116-001827-3663c6 open rows | add an open-loop row (fixed turn period) to `docs/MEASUREMENTS.md`, or say in the text that FIFO's Bulk rate is ~32 chunks per turn |
| C5 | no | nit | Two concurrent `run`s of one operation are no longer refused; their primitives interleave | `asio_completion.h:202-204` (only `in_flight()` checked) | a per-operation "driving" guard, or document it as misuse |
| C6 | no | nit | The closed-file case checks only that an error came back, not EBADF, although the comment names it | `sync_data_test.cc:166-169` | `check_errno(bad.error, EBADF, ...)` |
| C7 | no | nit | FreeBSD native SyncData completes EINVAL as an error instead of falling back where `aio_fsync(O_DSYNC)` is unsupported (FreeBSD < 13) | `bsd_completion.h:139-147` | none needed while CI's FreeBSD is 13+; or add EINVAL to the fallback set for SyncData |
| C8 | no | gap | Phase-3 teardown with a primitive in flight leaves the queue undrained, and `~BsdCompletionQueue` then calls `std::terminate`; pre-existing contract (the old single-run driver had it too), but D8 and consensus-driver A3's teardown need the host to drain the queue after `destroy_roots`, which nothing states | stress 261116-010521-f2acb6 backtrace; `bsd_completion.h:70-72` | state the drain step in `completion-seam.md` (or let the driver drain on destruction); consensus-driver's teardown test should cover it |
| C9 | no | nit | `check_seam.py`'s docstring says `--base` checks every public header declaration remains; the code checks only test/ files | `check_seam.py:11`, `:74-86` | drop the claim or implement it |
| C10 | no | nit | No ring test distinguishes SyncData from Sync (dropping `IORING_FSYNC_DATASYNC` survives); the mutant is stronger, not weaker, so durability is unaffected | `linux_completion.h:284-285` | none required |

## Not run

- Linux and FreeBSD locally (no host); the ring path is C2's gap.
- TSan on the stress probe (sanitizers are CI's; the driver is single-threaded on its owner, the only
  cross-thread touch is `token.requested()`, an acquire load).
- An implementation of the bounded share in Q4; its cost is estimated, not measured.
- A4's exclusive rows were not rerun; my lane probe ran at light level and is not a replacement for them.
