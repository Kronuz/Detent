# consensus-peers-wake review: correctness

Commit: consensus:a1855ace45fb6ce81d94431db4a25f32d00c23b5 (a1855ac)
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

None passed yet. Every job at a1855ac (A1 261121-140831-f20fe2 and 261121-140921-85a62f, the nine
A2 mutant jobs, A3 261121-140924-7f1c2c) and CI run 44955269607 were still queued when I wrote
this. The review is a static reading of peers.h, snapshot_channel.h, session.h, the test and the
nine mutant commits (refs/mutants/consensus-peers-wake/*, each a one-hunk change on a1855ac, as
the script describes). A1, A3 and A5 still need their jobs green; this review does not stand for
them.

## What I checked myself

Probe job 261121-142751-b1362b (queued): `.scratch/detent/review-consensus-peers-wake/bulk-refused-survives.sh`
runs the bulk case at the bulk-refused mutant (consensus:8abfaa5) and passes iff that mutant
survives. Rerun it at the fixed commit's mutant: it must then fail.

Risks the lead named, answered:

1. Missed wake. Every wake follows the state change it reports and precedes the next suspension:
   input emplaced (peers.h:726) then wake (735); session registered (700) then wake (702);
   `receive` (755) then wake (760); `consume_if_current` (817); `close()` then wake in `end()`
   (345). Paths that leave owner work without a wake: none found in the code. Fetch completions
   are posted to the executor (375-380) and run the host's own `done`, so the host raises its wake
   there: sound. Control lanes opening or closing tell the Worker nothing; a closing Control lane
   can only drop its retained input (less work): sound. A held Bulk frame (Busy) is polled by
   Peers every 1 ms (766) and wakes when the channel finally takes or refuses it. One wake path
   is untested: F1.
2. Re-entrancy. Peers takes no lock. No host call reaches `wake`: `stop` (144) and `admit` (253)
   use `close()`, not `end()`; `send`, `pump`, `fetch`, `admit` only cancel timers, whose handlers
   asio posts; `complete` posts (377). So `wake` never recurses. No call site is inside an
   iteration of `connections_`. I traced `stop()` from inside the wake at each site (702, 714,
   735, 760, 818, and `end()` from the reader, writer and timer): every loop rechecks
   `stopping_`, and the second `close()` is guarded by `notified` and `Session::close` is
   idempotent (session.h:185). `admit()` from the wake at 735 is exercised by the control case.
3. Spurious wakes. The idle case (no traffic, lone redialer, three connected voters) asserts exact
   counts; any-lane-opened fails at peers_wake_test.cc:174 and stop-wakes at :183, both by CHECK.
4. `close()` returning bool. It is private. Callers: `stop` :144 and `admit` :253 ignore it (host
   calls, correctly no wake), `end` :345, `run` :711. Detent's src (service.cc:486,
   peers.hh:9) and Xapiand's src only construct `PeerOptions` by field and `Peers<Node>`; no
   positional aggregate init of `PeerOptions` anywhere, so the trailing `wake` member breaks no
   one.
5. Mutants, by the assertion that kills each: control at :226 (`seen == received` in the owner
   turn); opened at :174; lost at :388 and :288; timer at :404; consumed at :373;
   any-lane-opened at :174; stop-wakes at :183. bulk-taken is killed by a hang (F2). bulk-refused
   is not killed (F1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The bulk-refused mutant survives the suite, so A2's claim for that row is false and the `Closed && open` term (peers.h:759) is untested. The corrupt-frame step sends 16 bytes of `X`: the channel copies the 12-byte prefix (consumed = 12) before `frame_length` throws "invalid frame magic" and returns `Closed, 12` (snapshot_channel.h:90-95, 102, frame_length), so `result.consumed` wakes at the mutant exactly as at a1855ac, and `end()`'s `close()` then finds the session already closed. The term is reached only with consumed == 0 on an open session: a held frame retried after Busy whose `process` throws (snapshot_channel.h:86-87, 102-103, then `session_closed` fails the Worker's source and cancels its snapshot, which is owner work from a Peers completion) or a closing or fenced Worker (:83). | snapshot_channel.h:83-103; peers.h:754-760; peers_wake_test.cc:379-382; probe 261121-142751-b1362b; mutant job 261121-140848-6433dd (expected to fail) | Add a bulk step that reaches `Closed` with consumed 0 on an open session (a Begin held Busy then refused, or a Worker closing or fenced before a frame arrives) and checks exactly one wake; the bulk-refused mutant job then passes. Or drop the term and its mutant if that path is ruled not owner work, and say so in peers-wake.md. |
| F2 | no | nit | The bulk-taken mutant is killed by a hang, not a CHECK: without the Begin's wake nobody calls `pump()`, the Accepted is never written, and `lane.session->receive()` (peers_wake_test.cc:371) waits for its first byte untimed (session.h:153-158) until ctest's 60 s TIMEOUT. | peers_wake_test.cc:369-373; session.h:153-158 | `CHECK(co_await reaches(2))` between the Begin's `put` and the Accepted's `receive`. |
| F3 | no | nit | `run()`'s trailing `if (lost) wake()` (peers.h:711-714) looks unreachable: every path there closed the connection first (reader and writer catches call `end()`, `stop()` closes all, `admit()` closes only Control lanes), so `notified` is set and `lost` is false. Harmless; no mutant covers it on its own. | peers.h:707-714, 770-772, 826-828, 143-144 | Keep as a guard, or make it a plain `close(connection)`; owner's call. |
| F4 | no | gap | peers-wake.md says the host may call `stop()` from the wake; no test does. Traced safe at every call site (risk 2 above). | docs/mechanisms/peers-wake.md "What the host may do in it" | A wake that calls `stop()` in one case, or keep the claim as traced. |
| F5 | no | nit | A3's TSan job covers tests that run one io_context on one thread, so it shows only that the harness has no race; the re-entrancy claims rest on the deterministic cases. | peers_wake_test.cc:429-432 | None needed. |
| F6 | no | nit | For the consumer (multi-voter-idle-wake): `admit()` takes at most one input per call and its cursor may pick another lane's, and an input refused with Busy or Pressure stays retained, so `retained_inputs > 0` alone as a no-sleep level would spin until the Worker frees; pair it with the Worker's deadline. | peers.h:235-273; Detent-wt multi-voter-idle-wake journal Next 2 | Consumer's design note, not this quest's. |

## Not run

- Any build or test at a1855ac: the contributor's A1, A2 and A3 jobs and CI 44955269607 were queued; their results are still owed.
- My probe 261121-142751-b1362b was queued; F1 rests on the code reading cited above until it runs.
- Linux and FreeBSD behavior beyond what CI will show.
- Detent's wiring of `wake` to `OwnerWake` (multi-voter-idle-wake), which is not written yet.
