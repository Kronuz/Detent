# namespace-s7 review: durability

Commit: 06770a7edea6997e9879ec36a8de4c34c01f1a53
Verdict: APPROVE
Date: 2026-11-11

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Re-review of the D59 fix round (Detent:c171898..Detent:06770a7e)

The first review (below, from "Evidence I relied on") ruled CHANGES at Detent:c171898. This round
checks F1 and F2 only. Code from Detent:abe2b1ab to Detent:06770a7e is identical (journal only), so jobs at
Detent:abe2b1ab are evidence for this commit.

Evidence accepted, not rerun: native suite 261111-021846-f70216 and pins 261111-021846-db2959; T7
261111-021847-4e419d (42024 cases, 903 distinct cuts, 0 violations); mutants 261111-021851-2e1cef
(F1 reverted: T7 fails at `first kill before sizing change: unsafe change file`), 261111-021849-6ce213
(F2 reverted: `test_agent_store.cc:199` fails) and 261111-021853-b98c22 (ndr4); my first-round probe
rerun at the fix, 261111-021855-1b868d (`change-zero: opened` on all three starts; `eio: ... relinked`).

What I checked myself:

- **F1, the extension cannot adopt a foreign file.** `ChangeWriter` runs `owned()` (regular, owner,
  one link, no group or other write) and `mode_only_acl` before `ftruncate` (`change_file.cc:48-55`).
  The symlink case fails earlier, at `O_NOFOLLOW`. Probe 261111-032055-ceeb78
  (`probe_change.cc`, built against `$JOB_SRC`'s `change_file.cc`) shows that an empty `change` that
  is hard-linked, group-writable, symlinked or carries an ACL is refused and stays at 0 bytes. Only a
  file the store's uid owns alone is extended. The agent store also refuses a directory that is not
  the agent's own, or that others can write (`agent_store.cc:288-289`).
- **F1, readers.** `ChangeMapping::open` still goes through `checked_words`, which refuses under one
  word or a size that is not a multiple of 8. The same probe shows a reader refusing an empty file, a
  3-byte file and a 12-byte file, and mapping the empty file once the writer has extended it. A
  ragged 12-byte file is refused by the writer too and left untouched. `ftruncate` grows a file by
  whole zero words and readers map only whole words, so no reader sees a torn word. The one way the
  extension could shrink a live mapping is two writers racing at creation. Both `AgentStore`
  (`agent_store.cc:290-293`) and DiskStore (whose owner session is created before `ChangeWriter`,
  `disk_store.cc:1846-1854`) construct the writer only after taking `owner.lock`. DiskStore's own
  startup scan also refuses a short `change` (`disk_store.cc:283-287`), but that scan runs in the
  startup operation after the writer has already extended the file, so D59's "DiskStore gains it" holds.
- **T7 covers `change`.** `change` is now inside the modelled disk and is never synced
  (`test_agent_store_crash.cc:45,63-65`). So at every event, `Data::Truncate` leaves it empty and
  `Data::Zeros` leaves it at its live size, beside both durable-only and all names. The new
  `kill before sizing change` case (`:348-354`) adds a kill with every name kept and `change` empty.
  Together these cover the states between the primitives on that path (`openat O_CREAT`, `fchmod`,
  the sizing `ftruncate`, `reserve`, and the startup barrier's directory sync): `change` is absent,
  empty, or at a whole number of words. Mutant 2e1cef shows the model sees the difference.
- **F2.** Reuse now requires `s.reached.contains(name)` (`agent_store.cc:436-442`). Every other
  existing name is unlinked and relinked from the freshly written `.tmp-`, whatever its bytes.
  `reached` is rebuilt only from a table that has been renamed and whose directory has been synced
  (`:503-517`), or from the table on disk at startup. A pack whose fsync failed belongs to a
  publication whose table was never written (groups 1 and 2 run before the table), so neither table
  reaches it, and it is relinked. 1b868d shows the inode changing, and `test_agent_store.cc`
  `failed_sync` pins this. A reached name was synced by the publication that made the table reach
  it, so dropping reused names from group 1 loses no sync. Nothing is stranded: the only removals
  are unreached names, and a removal followed by a cut before the relink leaves a name no durable
  table needs. Nothing new leaks: the same names are relinked in place. Long-term collection of
  orphans that are never republished is unchanged by this round, and outside it.
- **CI attempt 1.** Of the code that `test_delivery_coordinator` reaches, this branch changes only
  `change_file.cc`, and that code differs only for a `change` shorter than one word, which the
  test never creates. Its failure (`tests/detent_delivery/test_coordinator.cc:1117`, the fixture's
  `read_some` on an idle connection) passed on attempt 2 at the same commit. I see nothing that
  contradicts treating it as a flake that predates this branch (quest coordinator-idle-flake).

Verdict: APPROVE. F1 and F2 are fixed, each with a test and a mutant that kills it. F7 is a new nit.

## Evidence I relied on (first round, Detent:c171898)

- Mutants 261111-014006-0f1d84 (`n6-rename-before-fsync`) and 261111-014010-7d7284
  (`ndr4-unsynced-reuse`), both done, exit 0 (T7 fails without the fix). So the crash model catches
  a table renamed before its fsync and a verified reused name left out of the sync groups.
- Native suite 261111-013953-ee8bac was still running, and crash model 261111-014002-b517ac was still
  queued, when I wrote this. I did not rerun either, and the verdict does not depend on their
  result. The lead confirms both before landing.
- `git diff 627697e c171898` touches only CMake, the status doc and the journal. The worktree's build
  at 627697e is therefore the same store code, and my probes link against it.

## What I checked myself

Scripts are under `.scratch/review-namespace-s7-durability/`. Rerun with
`campaign job submit --quest namespace-s7 --commit <sha> -- sh <dir>/run_probe.sh <dir>/probe_store.cc`.
`run_probe.sh` links against the namespace-s7 worktree's build. For a fix commit, rebuild that tree
first, or point `wt` in the script at the fix.

- Job 261111-020334-306cec (`probe_store.cc`), exit 0. Its PROBE lines are the evidence for F1 to
  F5. Job 261111-020022-3093b2 ran an earlier version whose EIO hook never fired; it is superseded.
- **Publication order** (`src/detent_local/agent_store.cc:416-512`). The steps run in this order:
  1. Stage every file under `.tmp-`.
  2. `linkat` each to its final name.
  3. Sync group 1 (owner records and reused names, then the directory), then flush the device.
  4. Sync group 2 (payloads, then the directory), then flush.
  5. Write the table under `.tmp-`, then `sync_files` (fsync, plus `F_FULLFSYNC` on macOS through
     `files_sync_devices`).
  6. Rename the table over `selected`, then `sync_directory` (fsync of the directory, plus
     `F_FULLFSYNC` through the owner fd).
  7. Update `change`.

  The only unlinks are `.tmp-` names and orphans the in-memory table does not reach (`:442-445`).
  That table is the durable one: it advances only after the directory sync, and any I/O error
  poisons the store. No name a durable table reaches is ever removed or replaced. So any subset of
  pending directory operations that survives a power cut leaves the old table or the new one, each
  with every file it reaches. The model's two extremes are durable names only, and every name with
  durable bytes, cut short or zero-filled. It records an event at every hook and at every fsync and
  flush (`test_agent_store_crash.cc:66-115`). With those, it covers every primitive of publication
  and of startup except `change` and `owner.lock` (F1).
- **Verified reuse.** `slurp` opens through completion's `open_file`, which uses `O_NOFOLLOW` and
  requires a singly linked file with the right owner on the directory's device (`posix.h:753-773`).
  So a symlink, a second link or a foreign file is never trusted: each is treated as a mismatch. A
  reached name is never unlinked: when it differs, the cycle throws `ReachedNameMismatch`. The third
  path I found is F2.
- **Tombstone for an absent key.** The overlay stops when the label is absent and nothing is
  incoming, without copying the path (`paged_trie_overlay.cc:470-473`). `test_agent_catch_up:200-207`
  shows that only the position moves. It is a true no-op on the trie.
- **Derived ids across a restart.** Ids are pure functions of (profile, incarnation, position, base,
  ordinal), and a test pins one. A cycle cut before the table leaves orphans that the next attempt
  relinks; the model's "first, rebuilt" run covers this. A reached name collides only if a position
  regresses, or if an incarnation is shared or zero (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Fixed at Detent:06770a7e: the writer extends its own short `change`, and T7 models `change` (fails without: 261111-021851-2e1cef). Original finding: a `change` file shorter than one word wedges the store for good. `ChangeWriter` creates it with `O_CREAT` and only then calls `ftruncate(8)` (`change_file.cc:38-39`), and it refuses any `change` under 8 bytes (`:16-21`). Three paths leave such a file: a kill between the two calls, a failed `fchmod`, or a power cut after the name's metadata reached disk but before the size did. The agent store creates `change` before its barrier on every new store (`agent_store.cc:293-294`), so every later start throws "unsafe change file" until an operator deletes the file. The crash model cannot see this: it treats `change` and `owner.lock` as outside the disk (`test_agent_store_crash.cc:44`) and recreates `change` fresh on every recovery (`:134-163`). A3's "every primitive of startup" therefore leaves one primitive untested. The refusal is older code (Detent:b5472560) that DiskStore shares, but S7's new store is what reaches it on every creation. | 261111-020334-306cec: `change-zero: REFUSED`, again on the second start, and `change-zero new store: REFUSED` | Option (a) keeps one shape everywhere: create `change` under a `.tmp-` name, size it to its words, then `linkat` it. Startup already removes leftover `.tmp-` names. Option (b): the writer extends its own singly linked `change` when it is under 8 bytes; readers keep refusing it. Either way, the test must model `change` in T7, or at least show that a zero-length `change`, with or without `selected`, opens. |
| F2 | no | defect | Fixed at Detent:06770a7e under D59(a): only reached names are reused (fails without: 261111-021849-6ce213). Original finding: verified reuse can trust a file whose fsync failed. Group 2's fsync of a pack returns EIO, the store is poisoned, and the process restarts in the same boot. The pack's name stays linked, and the page cache still holds the expected bytes. The next publish verifies those bytes and reuses the same inode. Its new fsync can then succeed: on Linux an error already reported is not reported again to a new fd. The table then reaches a file whose bytes on disk are unknown. D10 catches it later (`.image`/`.read` digests after a reboot; packs at first touch or during scrub) as a failed namespace, so readers get errors, not wrong answers. | 261111-020334-306cec: `eio: first publish failed: ... Input/output error`, then `pack inode before X after X: REUSED` | This is the owner's call, because it narrows D8.4. Option (a): reuse only names the durable table reaches, and unlink and relink every other existing name. That is one rule for every unreached name, and group 1 would no longer need unreached reused names. Option (b): keep the current behaviour and document the failing-disk case as D10's. |
| F3 | no | gap | A4 and risk 5 are shown for the paged builders, not for the agent store. `test_agent_catch_up` installs its base through the DiskStore-era fixture and `recover_generation` (`:163,172`), and never opens an `AgentStore`. Nothing applies an answer to a store recovered from a mid-publish cut. The contributor reads "the same store bytes" as the same state commitment, because catch-up and full-copy ids differ by design. A composition argument holds: T7 checks every file a recovered table reaches byte for byte (`agent_store_fixture.hh:110-136`), and the store treats bytes opaquely. Still, no test runs the chain end to end. | Code reading | Put "apply each answer to a store recovered from a T7 cut, publish it, and compare commitments" in namespace-s7-wire's criteria (D58). The lead decides whether A4's wording needs a decision row. |
| F4 | no | nit | Generation ids leave out the namespace name. Two namespaces with the same incarnation and position therefore get the same names. One cycle refuses the pair; across cycles the second namespace throws `ReachedNameMismatch` and repairs at ordinal 1. Nothing is corrupted, but they collide again at every shared position. Incarnations are drawn per namespace (design D3), so this needs a zero or duplicated incarnation, for example a pre-S5 cursor (W1). | 261111-020334-306cec: `ids: ... the SAME ids`, `refused: a name published twice`, `ReachedNameMismatch ... (space beta)`, `table still verifies` | Refuse a zero incarnation at publish, or add the name to the derivation. Either keeps T6, because both are deterministic. |
| F5 | no | nit | Where `owner.lock` stops excluding. Between two processes it excludes (EWOULDBLOCK), and a lock left by a killed holder is released, so the next start succeeds. It does not exclude in three cases. First, if `owner.lock` is unlinked while a holder lives, a second writer starts: `fresh` becomes true and `create_exclusive` succeeds (`agent_store.cc:290-292`). Second, the store never consults `directory_filesystem()`, so a non-local filesystem whose `flock` does not exclude across clients raises no warning. Third, when two processes create a new store at once, the loser throws EEXIST instead of retrying with `open_existing`. That fails safe. `owner.lock` cannot sit on another device: `open_file` requires the directory's `st_dev`. | 261111-020334-306cec: `second process refused`, `after the holder is killed: opened`, `owner.lock unlinked under a live holder: second writer OPENED` | Wiring (namespace-s7-wire) warns or refuses on a non-local `directory_filesystem()`. The other two are optional. |
| F6 | no | nit | A3's text says "publication and adoption", but adoption is S9 (D9: `storage.meta`, `.lease`, the seeded-file barrier). S7's T7 covers publication, startup's barrier and `.tmp-` removal, and repair. | `docs/design/namespace-delivery.md:433-435` | Read A3 as "startup" for S7, and keep adoption in S9's T7. |
| F7 | no | nit | If `fchmod` fails, or a kill lands between `O_CREAT` and `fchmod`, the extension path accepts `change` with mode `mode & ~umask`, because the writer runs `fchmod` only when it created the file (`change_file.cc:43-44`). Under umask 022 and mode 0644 the two modes are identical. Under a stricter umask, for example 077, readers running as another uid cannot open `change`. Nothing is corrupted. Before the fix this file was refused outright. | Code reading; `grep umask src` finds nothing | Optional: also `fchmod(fd_, mode)` when extending a short `change`, so that "cut at its creation" redoes every step of creation. |

## Not run

- Re-review: no Linux run of the F1 probe (macOS ACL path only). I did not rerun the local ASan coordinator
  repro (261111-030057-345fa3, cancelled).

- I did not wait for the native suite or the crash-model job, and I did not check CI. All three were
  pending at review time.
- No probe ran on Linux. F2's claim about Linux fsync error reporting comes from kernel behaviour,
  not a run. The EIO in the probe is injected through the sync seam; no write-back actually failed.
- I did not attempt a real power cut, or a kill timed to land between `O_CREAT` and `ftruncate`. F1
  demonstrates the resulting state, then reasons about how a crash reaches it.
- Concurrent readers during publication, and `change` word semantics for readers, are reader.md's
  concerns and outside this lens.
