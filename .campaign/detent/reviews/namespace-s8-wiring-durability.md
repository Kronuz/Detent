# namespace-s8-wiring review: durability

Commit: Detent:7a98e14b05e2f24a846a18e8c0122a3e5b9cffef (code identical to Detent:dd16de65a6d2, where the jobs ran)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-review of the fixes since Detent:4243c07a (D9, D10), lens `durability`. The earlier rounds'
findings and evidence stand where not restated here.

## Evidence I relied on

- Suite 261116-041454-245173 (exit 0, dd16de65): `test_relay_service` logs heads
  {a: 19, b: 17, c: 21, d: 8}, 111 answers, the no-cursor answer read directly at the authority, A
  and B; relay C served 2 stale layouts answers and converged. `test_relay_mirror`,
  `test_relay_chain` (5.6 s), `test_relay_mirror_crash` pass.
- Mutant `upstream-drops-frames` 261116-041500-89b9e0: killed by the new heads check
  ("the authority's answers reach heads {}"), not by an unrelated failure.
- Mutant `behind-not-skipped` 261116-041457-47be19: killed by `converged` ("differing: {'c'}").
- Native 261116-041526-b91daa and pins 261116-041526-5b27e2 pass; the other mutants as the journal
  lists them.

## What I checked myself

Scripts under `.scratch/review-namespace-s8-wiring-durability/` in the home checkout.

1. `skip_probe.sh` + `skip_probe.cc`, job 261116-043603-b768ab: a `NamespaceRelay` over a scripted upstream
   of two sources (`sources()` 2, `skip()` swaps), one lagging and reachable, one current that
   seals after each of five syncs; frames empty, as a voter answers a cursor ahead of its head.
   Result (exit 1, 56 s): `older-seal` FOLLOWS (relay at seal 7 = current, 1 skip);
   `all-behind` BOUNDED (UpstreamBehind after 2 layouts asks, 1 skip); `at-start` STALLED (relay at
   seal 1, current at 7, 0 skips); `same-seal` STALLED (relay at seal 1, current at 6, 0 skips). Every
   stalled sync succeeded: nothing threw.
2. Code reading for the lead's questions, below.

Answers to the lead's questions:

1. D9. Bounded: each `sync` iteration returns, throws, or raises `moved` (< 3) or `behind`
   (< `sources()`), so at most 3 + N - 1 attempts (namespace_relay.cc:251-267); within one,
   `frames` is bounded by one 410 reopen and 10 busy waits (http_upstream.cc:257-293). No backwards
   move: every `UpstreamBehind` is thrown in `attempt` before `store.adopt` (namespace_relay.cc:191,
   196, 204 vs 207), so a skip leaves the mirror untouched and the next source passes the same D1
   checks. A current source is never skipped: `skip` follows only a refused answer, and layouts and
   frames of one attempt come from one endpoint (any rotation inside `HttpUpstream` throws,
   http_upstream.cc:61-76, 177-186). 409: `skip` is `rotate`, which drops the session
   (http_upstream.cc:69), right since a session belongs to one voter; the busy retry stays on its
   endpoint. 413: `expect` rotates and throws `UpstreamUnavailable`, not `Behind`, so the sync fails
   and backs off; no loop, and the stall on an oversized file is D4's known limit.
   But the fix covers only a lagging voter whose seal is below the mirror's: D11.
2. D10: yes. `direct()` (test_relay_service.py:430-455) opens and nexts with the test's own HTTP and
   parses DTDIST01 in Python; it requires a non-empty payload and its digest equals the probe's
   `raw *` at the authority, A and B, so the relays' answers equal the authority's independently of
   `HttpUpstream`. The heads check (every name, position > 0) and "answers at 0 and at the head
   differ" make the per-cursor comparison non-empty; the mutant now dies on the heads check.
3. Test changes. `converged()` catching a probe failure: a failure that persists still fails at the
   deadline with the last message; it hides only a transient one, and the 404 it targets is the
   legitimate race of a file collected between a layouts answer and its read (the route answers only
   files of the current layout), with the final answers still compared and `on_disk` checked. No
   durability defect hidden. The proxy's 502 changes only a test failure mode. `test_relay_chain`
   on `memory_storage`: no relay criterion needs the voters on disk; the voters never restart there
   (only relays do, lines 432, 456), the relays' mirrors stay on disk, and `LayoutExport` reading a
   real detentd's on-disk history is covered by `test_relay_service`. Precedent:
   test_history_cluster.cc:195, test_lagging_incarnation.cc:60.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| D1 | - | defect | Resolved (round 2). | 261116-005140-01fe78 | - |
| D2 | - | defect | Resolved (round 2). | CI 43834870722 | - |
| D3 | - | gap | Resolved (round 2). | Docs run 43834870274 | - |
| D4 | - | gap | Resolved (round 2); streaming stays open in the status doc. | layout_export.cc:75-79 | - |
| D5 | no | nit | Open, in namespace-followups. | - | As before. |
| D6 | - | nit | Resolved (round 2). | - | - |
| D7 | - | nit | Resolved (round 2). | - | - |
| D8 | no | nit | Open, in namespace-followups. | - | As before. |
| D9 | - | defect | Resolved for the case it names in its fix: a voter answering a seal below the mirror's is skipped, one try per source per sync, nothing adopted. | 261116-041457-47be19; 261116-043603-b768ab `older-seal`, `all-behind`; namespace_relay.cc:251-267 | - |
| D10 | - | gap | Resolved: the no-cursor answer is read without `HttpUpstream` and must carry frames; heads must reach every name. | 261116-041500-89b9e0; test_relay_service.py:430-455, 586-601 | - |
| D11 | yes | defect | The rest of D9: a relay still follows a reachable voter cut off from the quorum whenever that voter's seal is not below the mirror's, which is the common case. (a) At start, or after a restart with no newer seal held, endpoint 0 lagging: its answer is adopted. (b) The voter was cut off after the seal the relay holds: its layouts answer the same seal (not behind), its W5 next answers a cursor ahead of its head with no frames (`AnswerKind::Ahead`, session_frames.cc:37-38, 183, frames_export.cc:18, then 204 at service.cc:2695), so every sync succeeds with nothing new. In both the relay never learns a newer seal from the stale voter, so it never refuses, never skips, reports `ready` and successful syncs, and serves a frozen mirror until the voter rejoins. Nothing at a voter gates the history routes or W5 next on a live leader (service.cc:1146-1150; `leader_alive` gates only `/v1/health` `ready`, :1740, 1857). The status doc's "the relay moves on to the next voter" (namespace-s8.md, "The service") holds only for an older seal. | 261116-043603-b768ab `at-start`, `same-seal`; code cited | Options: 1. (recommended) A voter answers its history routes and W5 next `503` while it knows no live leader (the predicate `/v1/health` `ready` already uses); `HttpUpstream` rotates on it, bounding a cut-off voter's staleness to `election_max`, covering (a), (b) and frames, and every W8 reader at once. Test: in `test_relay_service`, stop (SIGSTOP) two voters; the third answers `503` on `/v2/history/layouts` and `session/next` within the election bound, and a relay reading it first converges once they resume; a mutant that drops the gate is killed. 2. The relay asks every source's layouts seal each sync and reads the newest: N requests per sync, and still blind to (b) between seals. |

## Not run

- No process-level partition of a detentd voter; D11 (b)'s voter side is from the code cited
  (no leadership check on the W5 next or history paths), the relay side from the probe.
- No mutant of `skip()` resetting the session; the code reading stands for it.
- Non-blocking: the D9 log line "refused N older layouts answers" counts stale answers the proxy
  served (`lagging.stale`), not refusals; and the test depends on a seal during `churn(27, 1)`
  (otherwise the stale answer equals C's seal and C stalls as in D11 (b)), which holds at this
  policy (suite log) but is not checked.
- The lagging voter's old session is never closed on `skip`; the voter's session collection reaps
  it, as after any rotation.
