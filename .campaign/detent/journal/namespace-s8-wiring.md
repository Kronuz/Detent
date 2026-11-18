# namespace-s8-wiring: journal

State: ready for review (A5 per D101 at Detent:11057903; waiting on jobs 261116-082652-fee406 relay suite, 261116-082655-e15f57 and 261116-082658-3a05d9 soak halves, 261116-082634-2fbb70 native, 261116-082634-e969e2 pins, mutants 261116-082702-991d63 absence-trusted, 261116-082706-2a4c28 history-not-gated, 261116-082709-6a5056 refusal-not-skipped)
Quest: .campaign/detent/quests/namespace-s8-wiring.md (the lead's; never edit it)

## Doing

Nothing: the jobs remain.

## Next

1. When the jobs end, record them in the Evidence table (the D101 round).
2. A soak or suite failure in A5's test: read its log before changing anything; the D101 window
   is no longer asserted, so a failure there is new.

## Blocked on

Nothing.

## Evidence

Fifth round (D101, owner 2026-11-16): the readiness gate stays and narrows a relay's exposure
to a voter cut off from its leader to at most the election timeout; never adopting from such a
voter moves to voter-read-freshness (its A2). The suite at Detent:bf52fca4 (261116-073946-b0ee94)
failed exactly there: relay D `ready` with 15 syncs, 2 entries, from voter x alone. A5 is now
[decision] D101; the test asserts what it says, deterministically (Detent:11057903, test and
`docs/status/namespace-s8.md` only, no src/ change):
1. `refuses(i)`: once voter i reports not ready, for 1 s both routes answer 503 `not_ready` and
   it never reports ready again (no leader can reach it: the others are stopped).
2. `refuses(x, returned)`: x stopped at least 2.5 s (past `election_max`, so its owner loop notes
   the absence and its last contact is stale), resumed alone; every request for 1 s from the
   resume answers 503 `not_ready`. The routes answer on the owner after its first pass, which
   sets the hold first, so this holds within the 2 s hold; health (published every 250 ms) may
   lag, so it is checked only once it first reports not ready. `absence-trusted` serves 200 once
   the queued frames arrive (~100 ms).
3. Relay D [x, stopped y] converges with the live quorum after y resumes, and again after the
   at-seal case; relay C (proxy 503, deterministic) is the skip check that kills
   `refusal-not-skipped` (C never ready).
Moved, verbatim: the old `a5` as `a5_voter_read_freshness` (uncalled), with "relay D adopted from
a voter that knows no leader" and "relay D synced from a voter that knows no leader". The soak is
two 10-run halves (a light job is capped at 30 min; a --long one is deferred).

| Criterion | Evidence (Detent:11057903) |
|---|---|
| A1-A3 | relay suite 261116-082652-fee406 (pending) |
| A5 | [decision] D101 (PLAN.md); test: suite 261116-082652-fee406, soak 261116-082655-e15f57 + 261116-082658-3a05d9 (20 runs beside 6 `yes` hogs), mutants absence-trusted 261116-082702-991d63, history-not-gated 261116-082706-2a4c28, refusal-not-skipped 261116-082709-6a5056 (all pending) |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service" (the window and D101; step 4 as tested now) |
| checks | native 261116-082634-2fbb70, pins 261116-082634-e969e2 (pending) |

Fourth-round evidence (superseded for A5 and the checks by the above):

Fourth round (native 261116-055648-ea95b0 failed A5's start case: relay D `ready` with 6 files
from voter x). Mechanism, traced at Detent:3023f2d0 with a temporary patch logging each `serving`
transition, each AppendResponse x sent and each history request's gate
(`.scratch/instrument.diff`, driver `.scratch/a5_repeat.py`: A5's start case alone, 20 repeats
beside 6 `yes` hogs, light hold): x is stopped ~2.5 s while the leader seals; the leader's
heartbeat and appends to x wait in x's sockets; on SIGCONT x's first pass reports not ready (the
last contact 2.5 s old), then 46-94 ms later it reads the queued frames, its AppendResponse sets
`leader_contact` to now and `serving` turns true for `election_max` (2 s) while the leader is
stopped. First frame: a heartbeat sent when x stopped (matched 14 of the quorum's 22), then the
leader's last appends (16-22). 20 of 20 resumes flipped; 9 of 20 inside `refuses()` itself (the
route answered past the gate, 503 history_unavailable), the others answered D's layouts 200; the
native run adopted. So the voter answered with no live leader on proof older than `election_max`:
a gate defect (proof counted when read, not when it could have been sent).

Fix (Detent:bf52fca4, service.cc): the owner loop notes a return from an absence of
`election_min` or more; no AppendResponse or RoleChanged counts as contact until `election_max`
after it, and `leader_alive` (so `serving`, `/v1/health` `ready` and both routes) holds only past
it. Test: `refuses()` checks both routes every 20 ms for 1 s after the voter reports not ready
(the queued frames arrive within ~100 ms). Local: the fix 20 of 20 pass beside the hog (x refused
through D's 2 failed syncs, D `syncs` 0, `fetched_files` 0); mutant `absence-trusted` (`trusting`
always true) 5 of 5 killed by `refuses()`.

| Criterion | Evidence (Detent:bf52fca4) |
|---|---|
| A1-A3, A5 | relay suite 261116-073946-b0ee94 (pending) |
| A5 | mutants absence-trusted 261116-073925-5d2f6a (new: the absence guard), history-not-gated 261116-073927-c36fd9, refusal-not-skipped 261116-073930-d0f58f (pending) |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service": the absence rule; step 4 (the resumed voter keeps refusing) |
| checks | native 261116-073923-0d3f9a (pending), pins 261116-073923-5297d0 (passed) |

Third-round evidence (superseded for A5 and the checks by the above):

Third round (D11, A5 per D99) at Detent:45acbe9f, test fix Detent:3023f2d0. Local run (Release,
loaded host), `test_relay_service` 80 s: A1-A3 as before; A5 "voter 1 alone (applied 54, the
quorum's 62) refused 503; relay D adopted nothing in 2 failed syncs, then converged" and "voter 1
at D's seal and alone refused 503; relay D synced nothing, then converged"; relay C (reading a 503
source first) had 0 failed syncs. Earlier evidence at Detent:dd16de65 is superseded for the
service mutants and the suite.

| Criterion | Evidence (Detent:3023f2d0) |
|---|---|
| A1 | suite 261116-055643-30fa1f; mutants behind-not-skipped 261116-055702-e9a921, upstream-drops-frames 261116-055709-dd35c4, busy-not-retried 261116-055715-70a4f9, layouts-no-enable 261116-055722-63b7b5 (regenerated); `test_relay_mirror`/`test_relay_chain` mutants as listed for dd16de65 below (those tests and their code unchanged) |
| A2 | suite 261116-055643-30fa1f; mutant mirror-forgotten 261116-055736-17f69b |
| A3 | suite 261116-055643-30fa1f; mutant member-route-admitted 261116-055743-68bbbe |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service" (A5 added: `503 not_ready`, the skip on 503, the test's step 4) |
| A5 | suite 261116-055643-30fa1f; mutants history-not-gated 261116-055750-88fa5c (voter gate), refusal-not-skipped 261116-055729-53fd26 (relay skip on 503) |
| checks | native 261116-055648-ea95b0, pins 261116-055655-92aa4f |

Second-round evidence (superseded where restated above):

Durability review (CHANGES) folded in at Detent:e09d3690 (code at Detent:bfb6a386, docs Detent:8da51b76,
mutants Detent:9945b0f0; jobs submitted at Detent:e09d3690).

Local runs at the fix (Release, loaded host): `test_relay_mirror` 10 s (incl. `behind`: 6 older upstreams
refused across a restart), `test_relay_chain` 44 s (each voter's export equals its oracle at every step;
an unsealed drop leaves it unchanged), `test_relay_service` 49 s (incl. the busy proxy).

| Criterion | Evidence |
|---|---|
| A1 | suite 261116-041454-245173 (`test_relay_service` incl. relay C and the direct read, `test_relay_mirror`, `test_relay_chain`, `test_relay_mirror_crash`); mutants at Detent:dd16de65: 261116-041457-47be19 (`behind-not-skipped`), 261116-041500-89b9e0 (`upstream-drops-frames`), 261116-041504-0ecd50 (`busy-not-retried`), 261116-041508-1e6ec2 (`layouts-no-enable`), 261116-041519-3cd80f (`hide-failed`), 261116-041523-2b6e19 (`keep-dropped`); at Detent:1eea5261 (same code, `test_relay_mirror` unchanged since), killed: 261116-030714-3d4167 (`older-seal-adopted`), 261116-030717-1f35ed (`same-seal-adopted`), 261116-030721-281010 (`fetch-reached`), 261116-030726-64b0c4 (`no-collector`), 261116-030729-92134f (`trust-first-read`) |
| A2 | suite 261116-041454-245173; mutant 261116-041511-a30e3f (`mirror-forgotten`) |
| A3 | suite 261116-041454-245173; mutant 261116-041515-baa8ec (`member-route-admitted`) |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service" (updated: seal order, busy retry, 413, backoff, skip past a lagging voter) |
| checks | native 261116-041526-b91daa, pins 261116-041526-5b27e2 |

Second round (D9, D10) at Detent:3fa0dfeb, main merged at Detent:eba70ec5. Local run (Release,
loaded host): `test_relay_service` 56 s: heads {a: 19, b: 17, c: 21, d: 8}, 111 answers, the
answer at no cursor read directly equal at the authority, A and B; relay C refused 1 older layouts
answer and followed the leader. `test_relay_mirror` 10 s. Earlier evidence at Detent:e09d3690
(suite 261116-005109-0f20c5, native 261116-005055-6d0652) is superseded.

At Detent:1eea5261 (lead, msg): all 13 mutants and pins passed; suite 261116-030647-709c5f and
native 261116-030743-567511 failed `lagging.stale > 0`: relay C had rotated to the leader (an early
503 `history_unavailable` rotates) before the proxy turned lagging, so it never asked it. Native
also timed out `test_relay_chain` (120 s, load 15-45). Fixed at Detent:dd16de65:
1. C reads `[lagging proxy, gated proxy]`, the gated one closed (503) until the lagging one served
   C an older seal, so C must refuse a stale answer and skip to converge. Local: 2 refused, 55 s.
2. `test_relay_chain` was not lengthened by D10 (it runs no service code; 48 s in suite
   261116-005109-0f20c5 vs 76 s in 261116-030647-709c5f, `test_relay_mirror_crash` 20 s vs 52 s:
   load). A `sample` of it shows ~90% in the harness voters' journal F_FULLFSYNC; its voters now
   use the harness's `memory_storage`, relays stay on disk: 49 s to 19 s locally.
3. `converged()` retries a probe that fails while a relay still syncs (a file collected between
   the probe's layouts and its read: the 404 at 591511ce, seen again locally); the proxy answers
   502 on a failed forward.

Review findings:

| ID | Fix | Test | Fails without |
|---|---|---|---|
| D1 | A layouts answer carries its seal (DTMIRR01 header word 4), built from the sealed state alone (layout_export.cc); the mirror table records its newest seal; `attempt` refuses an older seal, a lower or disagreeing same-seal layout, and a drop of a name held at that seal (namespace_relay.cc) | `test_relay_mirror` `behind` (the probe's scenarios plus same-incarnation, across a restart), `test_relay_chain` export checks; probe rerun 261116-005140-01fe78 (adapted to the new `Upstream::layouts` type: one line) | probe 261115-234744-05b4d6 exits 1 at d77c332d; mutants `older-seal-adopted`, `same-seal-adopted` |
| D2 | `HttpUpstream::frames` keeps the session on 409 `session_busy` and asks again, 10 times, 1 ms doubling | `test_relay_service` reads the leader through a TLS proxy that answers every other next 409 `session_busy` | mutant `busy-not-retried` |
| D3 | reference/configuration.md (mirror keys, `application_limits.layout`), reference/http-api/authority.md (routes, `history_unavailable`, 413, `session_busy`); stale `application_limits` exception removed | `check_docs.py`: 43 gaps, 43 listed, 0 unlisted, 0 stale | Docs run 43820854964 at d77c332d |
| D4 | `LayoutExport::file` refuses a file above `reply_bytes - 256` with 413 `reply_bound`, before reading when a layouts answer sized it; the mirror thread doubles its wait after each failed sync, up to 30 s or one interval | `test_relay_chain` (length_error at size - 1, bytes at size) | none: the post-read bound also throws, so a mutant of the pre-read check survives; the saving is the read, not the answer |

Re-review (CHANGES, D9 and D10 blocking), the quest's last fix round:

| ID | Fix | Test | Fails without |
|---|---|---|---|
| D9 | `Upstream` gains `sources()` and `skip()`; `NamespaceRelay::sync` on `UpstreamBehind` skips to the next source and starts over, each source once per sync, then throws (the service backs off as before); `HttpUpstream` answers its endpoint count and rotates | `test_relay_service` relay C reads `[lagging proxy, leader]`: the proxy forwards until C holds the authority's seal, then answers every layouts request with the first one it forwarded; C converges with the authority after 12 more writes, and the proxy served a stale answer | mutant `behind-not-skipped` (rethrow at once) |
| D11 | A voter's history routes answer `503 not_ready` unless `serving` holds (service.cc: `node.worker().ready() && leader_alive()`, the one lambda `/v1/health` `ready` also reports); `HttpUpstream::layouts` turns a 503 into `UpstreamBehind`, so `sync` skips to the next source within the sync | `test_relay_service` A5: two voters SIGSTOPped, the third reports not ready and answers layouts and file 503 `not_ready`; relay D [that voter, a stopped one] adopts nothing (at start, the voter 8 entries behind) and syncs nothing (at D's seal), then converges with the leader once a voter resumes; relay C reads `[gated (503), lagging]` and has 0 failed syncs | mutants `history-not-gated`, `refusal-not-skipped` |
| D10 | `test_relay_service` checks the authority's heads hold every name at a position > 0, that each name's answers at 0 and at its head differ, and that the answer at no cursor read at the authority, A and B with the test's own HTTP and DTDIST01 parsing (no HttpUpstream) equals the probe's | the same | mutant `upstream-drops-frames` |

The earlier failed job 261116-005133-947e51 (listed under A1 without a note) was that mutant
surviving, not a flake: every 200 next read as an empty body, so the probe reached no head, asked
only the two `*` answers, and they were empty and equal everywhere (the review's rerun
261116-023219-318dfb logs `heads {}`, 2 answers). Its kill at 591511ce (261115-230049-08c459) was
an unrelated 404. D10's checks make the comparison hold frames read without the code under test.

D5-D8 (nits): D6 fixed (relay history routes answer 403 `scope_forbidden`), D7 fixed (only a layouts
request sets `history_wanted`). D5 (history routes skip the cluster/group/epoch context check) and D8
(`free_port` bind-and-close race in `test_relay_service`; the new proxy binds port 0 itself) are left for
namespace-followups.

## Result

detent-relay serves its namespace mirror over HTTPS (W8). `MirrorService` (src/detent_relay/mirror_service.cc)
owns `NamespaceRelay` on one thread, syncs from `HttpUpstream` (src/detent_relay/http_upstream.cc), and
answers W5 version 2 and the history routes; version 1 stays with the bundles. The authority adds
`POST /v2/history/layouts` and `/v2/history/file` (src/detent_authority/layout_export.cc) and takes
`application_limits.layout`. Layouts are ordered by the upstream seal they carry, so a relay never follows a
lagging voter backwards. How it works and the routes in Contracts terms: docs/status/namespace-s8.md,
"The service"; reference: website/src/content/docs/reference/{configuration,http-api/authority}.md. Left
open there: streamed fetch of large files (a file above the reply bound is refused 413), a bound on the ring.

## Rulings I made

1. One layout encoding (DTMIRR01) serves the mirror's table and the HTTP layouts answer; the codec moved
   to detent_relay_layout so the authority links it without mirror I/O. An answer is a table with
   sequence 0 and `seal` the upstream seal it was built at; the mirror's table keeps `sequence` as its own
   write count and `seal` as the newest seal it adopted.
2. `Upstream::fetch` and `MirrorFetch` carry the namespace, so every upstream authorizes a file read by
   W13 grant and serves only files its current layout names for that namespace.
3. New authority routes `POST /v2/history/layouts` and `/v2/history/file`, answered on the owner from
   the voter's own sealed history (no quorum read: files are immutable, the layout is a function of the
   seal). A layouts request wants positions like a W5 next, so a relay alone triggers the enabling entry.
4. (Replaces the same-incarnation rule.) A relay refuses any answer older than its mirror: a seal below
   the table's, a layout below the one held, another layout at the same seal, or a name absent at a seal
   where the mirror holds it (`UpstreamBehind`). `MirrorStore::adopt` takes the seal and refuses a lower
   one (std::invalid_argument) as a guard.
5. The relay refuses `all` (400) and names it does not mirror or grant (403); nexts get 503
   source_behind before the first sync; history routes share the opens lane.
6. A relay may run with no v1 bundles when a mirror is configured; credentials may carry W13
   `namespaces` beside or instead of `bundles`.
7. The authority's configuration takes `application_limits.layout` (validated as SetLayout), so a
   process-level test reaches bases, blocks and sections; additive and optional.
8. The relay's file route answers only files its current layout names (404 otherwise); a layouts
   request from the relay lists only the names it mirrors and the credential grants.
9. `test_relay_chain` links `detent_authority_service` to check `LayoutExport` against its oracle.
10. Out-of-set files for D3 (msg-58, msg-59 to the lead): the two website reference pages and
    tests/docs/docs_exceptions.txt. Six stale mutant patches (four already stale at d77c332d) are
    regenerated with the same mutation.
11. `test_relay_service.py` is ruff-formatted and lint-clean (three `check=False`).
12. D9's bound lives in `NamespaceRelay::sync`: `Upstream` reports its `sources()` and `skip()`s
    one (defaults 1 and nothing, so a relay upstream and the test fakes are unchanged); a sync
    tries each source once, then throws `UpstreamBehind` and the service backs off as before.
13. D10's independent read is the W5 answer at no cursor, parsed in Python (DTDIST01: 96-byte
    header, payload size at byte 12); the per-cursor answers stay with the probe, now checked to
    reach every name and to differ between R = 0 and the head.
15. D11's gate covers the two history routes only, not the W5 next: every relay attempt starts with
    layouts and reads frames from the same endpoint, so a refused layouts request is enough for a
    relay. Gating W5 next would change every v2 reader's behavior (a 503 during each election and
    at bootstrap) and is left to the lead (namespace-followups: direct W5 readers of a cut-off
    voter). The error is `not_ready`, after the health field it mirrors; `layouts-no-enable` is
    regenerated for the moved lines.
16. Any 503 to a layouts request is a skip, not only `not_ready`: `history_unavailable`, `stopping`
    and `admission_full` are also "this source cannot answer now". A single-endpoint relay fails
    the sync and backs off as before. Relay C now reads `[gated, lagging]` and starts once A is
    ready, so its first sync must skip a 503 source; `behind-not-skipped` now dies with C never
    ready (the mutant throws without `skip`, so C stays on the closed proxy).
17. A lone voter and bootstrap: detentd refuses fewer than three voters ("fixed voter count: an
    odd number of voters, at least 3", probed locally), so a single-voter authority exists only
    in-process; there it is `Role::Leader` with `leader_known` once it elects itself, so `serving`
    holds exactly when its `/v1/health` reports `ready`. No single-voter test reads a history route,
    and the tests that would host one (tests/detent_authority) are outside this quest's files. At
    bootstrap the routes answer 503 until the first leader is known, as they already did until
    positions were enabled; `test_relay_service` starts relays right after the election and they
    become ready (suite).
18. The absence rule lives in the service's `leader_alive`, the one predicate behind `/v1/health`
    `ready` and the history routes, not in consensus: the core also counts a queued frame as
    contact (its `leader_contact_` is set when read), and per-frame receive times are in the
    consensus transport, not this quest's files. Threshold `election_min` (an absence the cluster
    could elect through), hold `election_max` (the lease window; a stale leader's CheckQuorum
    steps it down within it). The queued frames drain in under 100 ms in the trace. A voter
    whose owner loop stalls 1 s or more under load reports not ready for 2 s after: honest, it
    could have missed an election. W5 next stays ungated (ruling 15).
19. A5's test asserts only what D101 keeps (the gate while not ready, the absence rule from a
    return, convergence); relay D's two no-adoption assertions are parked uncalled in
    `a5_voter_read_freshness`, not deleted, so voter-read-freshness enables them as its A2.
14. `test_relay_chain`'s voters run on the harness's memory storage: the test proves the relays'
    files and answers against the voters, and the voters' own durability is the authority suites';
    the relays' mirrors stay on disk.
