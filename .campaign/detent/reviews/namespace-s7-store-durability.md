# namespace-s7-store review: durability

Commit: Detent:518f6147d24ed8d0c1ec7f1a41f58c16be718756 (the branch head, Detent:a74b4c46, differs only in the journal; the concurrent tests/detent_delivery, benchmark, tool and doc ports are not reviewed)
Verdict: CHANGES
Date: 2026-11-13

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- T7, 261113-044510-08f77b at this commit: 55380 cases, 1037 distinct cuts, 127 republished, 70 repairs, 0 violations. The hook fires after every primitive of startup (barrier, each `.tmp-` removal, `change`), publish (each create, write, link, orphan unlink, temporary unlink, both sync groups, table create/write/sync/rename, directory sync, `change`) and collect (each removal). The recorder also notes each file fsync, directory fsync and device flush on their own (`test_agent_store_crash.cc:84-118`), so cuts land between the fsyncs inside a group too. Each cut is checked against durable-only names and against every name, with truncated or zero-filled data. The check (`:249`) requires a table that was actually published with sequence >= the one served, and every file it reaches verifying byte for byte (`table_verifies`). So recovery lands on the acknowledged table or the in-flight one, never a mix: the table is one renamed file.
- Mutant n6 (table renamed before its fsync), 261113-042432-0f33ed, killed by T7. A4 test 261113-044511-bfb064 at this commit. Mutants a4-collect-ignores-lease and a4-publish-ignores-lease, 261113-042414-b3c4fb and 261113-042418-606cb1, killed. Those three ran at Detent:03ef11d2; `agent_store.cc` and `tests/detent_agent/` are identical at this commit.
- A3 grep 261113-044510-384de8. Read counters (CI's configuration) 261113-044512-76bfe6, which expects `{1, 5, table, 0, 0, hashed}`. At 26d197cd the same test failed on the double count (261113-042437-89be6e: 1746, want 1129), so the test sees the bytes it claims to.
- Suite 261113-044509-c22720: it failed (see F5), but `test_agent_catch_up` and every `test_system_managed_*` passed in it.

## What I checked myself

Probe scripts are under `.scratch/review-namespace-s7-store-durability/` in the home checkout. Each patches only its job's checkout.

- `run_lease_probe.sh` + `probe_lease.cc`, job 261113-050912-e58cff. It interposes `openat` so that opening a `.lease` fails, then shows F1. It also checks the reader's side of the lease protocol. A reader that opened the lease before collect() took it can still lock it after the unlink, but it holds nlink 0 and the name is gone, so `generation.cc:323-334` sends it round again. A revisit that relinks the lease leaves the reader's inode different from the name's, so it refuses.
- `run_catch_up_probe.sh`, job 261113-050642-f58ce8, answers Q3 (F2).
- `run_fix_mutants.sh`, job 261113-050718-b61f7b. Before applying either mutant, the job runs a baseline of the 15 flat and paged DiskStore tests, and all pass. Mutant m1 restores the old order, the plan attached before the reservation: `test_local_paged_disk_store` aborts with `moved paged publication plan`. Mutant m2 charges the flat `.live` to a private ledger: every test passes (F3).
- Code reading:
  - **Leases.** completion's `try_lease` is `flock(LOCK_NB)` (`posix.h:545-559` at its pin 560c288). flock locks belong to the open file description, so the agent's own views and collect() conflict even inside one process.
  - **Removal order.** collect() holds the exclusive lock across the `.lease` unlink and removes the other files only afterwards (`agent_store.cc:509-535`).
  - **Serial disk work.** DiskStore runs one piece of disk work at a time and collects only after the paged recovery is confirmed (`disk_store.cc:172-194`).
  - **Writers.** A wider grep (`O_CREAT|O_WRONLY|O_RDWR|ofstream|fopen|rename|unlink|ftruncate|linkat|mkdir`) over `src/detent_{local,agent,delivery,testing}` and `include/detent_local` finds no write outside agent_store.cc and change_file.cc. The one exception, `directory.cc`'s `mkdirat`, provisions the directory itself. `src/`, `include/`, `bindings/` and `tools/` (minus `tools/w5/first_read.py`, which is being ported) read no `DTSELECT` or `storage.meta`.

Answers:

1. **Q1 (A2).** Yes, at the AgentStore level, and DiskStore does no directory I/O of its own. The gap is F4.
2. **Q2 (A4).** Lease races are safe: a reader that opens between the check and the unlink either sees EWOULDBLOCK, ENOENT, or nlink 0 on its locked inode, and retries. A crash mid-collect is also safe: the `.lease` goes first, so what a cut leaves has no lease to hold, and T7 cuts every removal. One path is not safe: an open failure on the `.lease` reads as "no reader" (F1).
3. **Q3 (A5).** In the journal's run, the 4 cuts not at R were the kills after `rename table`, `sync directory` and `change`, plus the uncut run. Each recovered at h and was compared with the full copy's commitment, which is correct: a process kill after the rename keeps the new table. The collection's cuts are reached only by timing (F2).
4. **Q4 (A3).** The product code is clean. The leftovers are in tests, benchmarks and docs (F7).
5. **Q5.** Reserving before the move is correct and tested (m1 killed). Charging `.live` to the store's account is correct but untested (F3). The reader's first-open count is correct, and the counters job pins it. The count is per actual open, so the managed path, which reads the table more than once, counts each read.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | collect() treats any failure to open a generation's `.lease` as "no reader holds it". When `open_existing_read_only` throws (EMFILE, ENFILE, EACCES, bad_alloc), `file` stays null and the generation's `.lease`, `.image`, `.read`, `.live` and every segment only it names are unlinked under a reader that holds the lease. That breaks A4's "never one a reader holds", and a reader then fails at its next by-name open (packs at first touch). publish() has the same fail-open: on an open failure it `continue`s, so a revisited cycle unlinks and relinks a held lease instead of throwing LeaseHeld. | `agent_store.cc:511-516` and `:342-346`. Probe 261113-050912-e58cff: the control keeps the generation; with EMFILE and with EACCES, `removed 1 generations; held lease links 0; image GONE; pack GONE`; `publish revisit emfile: published; held lease links 0` | In collect(), only a missing name (ENOENT) may proceed. Any other open failure keeps the generation and the segments its `.live` names, as a held one does, and leaves the collection deferred. In publish(), propagate the failure (or throw LeaseHeld). Test: `probe_lease.cc`'s `collect_open_failure` and `publish_open_failure` in `test_agent_store` (an interposed `openat`, or a seam), asserting kept/LeaseHeld. |
| F2 | no | gap | A5's cut points inside the collection depend on timing. The catch-up child collects only if the retirement thread has already closed the base view's `.lease`. The journal's run had 33 cut points and none in the collection. My run had 37, with 4 in the collection, and its final uncut run deferred (2 `.lease` files left). The test comment claims the collection is cut. A5 itself holds: 29 cuts at R caught up in both runs, and T7 cuts every removal deterministically. | `test_agent_catch_up.cc:229-241,261-262`. Probe 261113-050642-f58ce8: `CUT 0-28 ... revision 10`, `CUT 29-35 ... revision 14`, `CHILD past the cycle: 32 primitives, 2 .lease files left` | Wait for the base's lease before the child's collection (as 4a9d5620 does in the intents test), or drop "(and the collection that follows)" from the comment. |
| F3 | no | gap | The flat `.live` charged to the store's account has no test that fails without it: the mutant survives. | Job 261113-050718-b61f7b, m2 SURVIVED across 15 tests; `disk_store.cc:366-369` | Add a flat `begin` against an account with no room for the `.live`: it must refuse with StoragePressure, queue nothing, and leave `reserved` at zero. |
| F4 | no | gap | T7 never publishes with `dropped` or under an adopted profile, which is the shape of the DiskStore cycle after a scope or profile change (`disk_store.cc:95-106`). The primitives are the same and only the table content differs, so the composition argument holds, but no cut tests the drop. | `test_agent_store_crash.cc:192-201,426-443` | Optional: a T7 cycle that drops beta while adding gamma, under a second profile. |
| F5 | no | gap | Outside this lens. Suite 261113-044509-c22720 failed on `test_relay_chain` (Timeout, 120 s; 51 s at Detent:fe14036e in 261113-003843-d3ad47). In the same run `test_relay_mirror_crash` took 354 s against 62 s there, which points to load. `test_relay_chain` links relay_mirror and authority, nothing this quest changes. A1 and A5 still have no passing suite job at this commit. | Job log, `tests/detent_relay/CMakeLists.txt:15-19` | Rerun the suite. |
| F6 | no | nit | The coordinator no longer wakes at `collection_retry_at()` (`coordinator.cc:194`). DiskStore's 1-30 s backoff therefore acts only when something else wakes the loop. A base held by the agent's own retiring view, the common case after every publication, stays until the next publication, a pressure refusal or a poll. That is bounded and documented in Wiring, but it leaves two retry mechanisms, one of them inert. | `disk_store.cc:90-92,122-128,169-179` | Wire `collection_retry_at()` back into `wait_disk`, or delete the backoff. |
| F7 | no | nit | Old-layout reads remain outside the product code: `tests/detent_delivery/verify_local_format.py:15` (`DTSELECT`), `test_managed_process.sh` (`storage.meta`), and `benchmarks/paged_{collection,recovery}_bench.cc` (which write `storage.meta`). | `git grep` at 518f6147 | Make sure the port includes `verify_local_format.py`. It is not named in msg-47/48's list. |
| F8 | no | nit | Outside this lens: while disk work runs, the owner polls its completion every 1 ms, so a long fsync-bound publication wakes it about 1000 times a second. | `coordinator.cc` disk_loop_body (the `disk_wake->expires_after(1ms)` loop) | Post the completion back to the owner's executor instead. |

## Not run

- No Linux run. Every probe and T7 job ran on macOS, so the device-flush model is the Apple one.
- No real power cut, and no TSan run of DiskStore's worker handoff.
- I did not rerun `test_relay_chain`, the full suite or CI.
- I did not review the in-flight tests/detent_delivery, benchmark, tool or doc ports.
- F1's real-world triggers (descriptor exhaustion in the agent) are argued, not measured. The probe injects the failure.
