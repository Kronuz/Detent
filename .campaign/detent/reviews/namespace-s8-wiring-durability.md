# namespace-s8-wiring review: durability

Commit: Detent:d77c332d0659065bb06b8ae9c7f3c5321b444d9b (code identical to Detent:591511ce, where the jobs ran)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Lens `durability`, carrying the lead's security, config, mutant and sanitizer questions.

## Evidence I relied on

- 261115-230037-c02ee6 (exit 0): `test_relay_service`, `test_relay_mirror`, `test_relay_chain`
  pass at 591511ce, Release, this host: 56 s, 10 s, 61 s (macOS fsync dominates).
- Mutants killed (exit 0): 261115-230043-562639 `layouts-no-enable`, 261115-230049-08c459
  `upstream-drops-frames`, 261115-230054-1ddbbb `mirror-forgotten`, 261115-230058-44d37f
  `member-route-admitted`.
- CI run 43820855328 at d77c332d: `test_relay_mirror_crash` (the T7 power-cut and kill matrix)
  passes on linux gcc, clang, arm64, ASan/UBSan (3.55 s) and TSan (3.90 s); `test_relay_service`
  passes on clang (18.96 s), arm64, ASan/UBSan (19.89 s) and TSan (21.63 s), and fails on
  linux-x86_64-gcc (D2). Docs run 43820854964 fails `check-docs` (D3).

## What I checked myself

Scripts under `.scratch/review-namespace-s8-wiring-durability/` in the home checkout.

1. `behind_probe.sh` + `behind_probe.cc`, job 261115-234744-05b4d6 (exit 1: the defect): a
   `NamespaceRelay` over a scripted upstream syncs a newer upstream (V1), then an older one (a
   lagging voter V3, which `HttpUpstream` reads after it rotates on any failure). Exit 0 when every
   older upstream is refused. Results:
   - Refused: the same incarnation at an older seal (ruling 4), and any answer that also carries a
     same-incarnation name at an older seal (that name refuses the whole answer; an earlier run,
     261115-230852-6a3ef6, mirrored two names and so passed by accident).
   - Went backwards: x alone at another incarnation (adopts `x@a/seal1` over `x@b/seal2`, fetched 2,
     collected 2); x alone after V1 dropped it (re-adopted, fetched 2); y alone that V3 does not hold
     yet (dropped, its 2 files collected); and x re-created at the same seal beside an unchanged y
     (adopts the old incarnation, fetched 2, collected 1).
2. CI logs (`ci/*.log`): per-test times under ASan and TSan, and the gcc failure's output.
3. Code reading, cited per finding: the HTTP path's disk steps, the routes' authentication, grants,
   name resolution and size bounds, and `application_limits.layout`.

Answers to the lead's questions:

1. Crash at every step through `HttpUpstream`: the HTTP path adds no disk primitive. A fetched body
   is held whole in memory and checked against the layout's size and SHA-256 before staging
   (mirror.cc `Impl::fetch`, called at :227), so a cut or short response is `UpstreamUnavailable` or
   `FetchRefused` and nothing reaches disk; staging, link, D11 sync group, table rename and collector
   are exactly T7's primitives, and the only change to mirror.cc threads the namespace through. A
   restart answers nothing from the ring (memory only) until its first sync: nexts get `503
   source_behind` (mirror_service.cc:174), and the layouts and file routes serve the durable table,
   which only moves forward once adopted. T7 covers it; no new crash step. The stale-answer risk is
   not a crash: D1.
2. Authority routes: authentication precedes `prepare` (`task->admission->credential`); every name
   is checked `valid_namespace_name` and W13-granted before the task is queued (service.cc:911-931),
   so an unauthenticated or ungranted request never reaches the owner and never sets
   `history_wanted`, and the enabling entry is the same one any granted W5 next triggers. A file name
   is never a path: it is compared to the names the namespace's own table entry generates
   (layout_export.cc:65-79), so `..`, another namespace's block or a name outside the layout is 404.
   Relay routes: `registry->authenticate` in `admit`, grants and the layout checked on the mirror
   thread (mirror_service.cc:189-219, namespace_relay.cc:276-281). Sizes: requests bounded by the
   lane body limit, 4,096 names and 255-byte names; authority replies reserved against `reply_bytes`
   in `reply_for` (service.cc:1180-1214), relay replies from the `answer_bytes` ledger; but see D4.
3. `application_limits.layout`: `validate()` at load (service.cc:283-300), then
   `validate_node_options` refuses a policy whose files outgrow a peer fetch (node.cc:92), the same
   two checks SetLayout applies (state.cc:1882-1890); the log limits are always replaced by the
   consensus ones (node.cc:330-335). No config value weakens a bound.
4. Mutants: `upstream-drops-frames` is a relay serving a stale ring, `mirror-forgotten` a relay that
   refetches; none covers ruling 4 (`UpstreamBehind`), and the test never reads a lagging voter (D1).
   W8 at the process level: member routes 404 on both relays, a `voters` configuration refused, and
   `nm` finds no `kronuz::consensus` symbol; mutant `member-route-admitted` is killed.
5. Sanitizers: TSan 21.63 s and ASan/UBSan 19.89 s on CI at this commit, well under 120 s. The 45-56 s
   local figure is macOS fsync, not CPU.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| D1 | yes | defect | A relay goes backwards on a lagging upstream unless the incarnation matches. `attempt` refuses an older layout only for the same incarnation (namespace_relay.cc:194-196); a voter that has not applied a drop, a re-creation or a creation yet makes the relay re-adopt a dropped name, adopt the old incarnation over the new one, or drop and collect a live name. `HttpUpstream` rotates to the next voter on any failure (http_upstream.cc:69-75), and a voter answers layouts from its own state with no read barrier, so this is reachable. Worse at the voter: `LayoutExport::layouts` takes liveness and incarnation from the applied `state` but `seal` from the table (layout_export.cc:44-47), so two voters at the same seal can offer different incarnations, and no seal comparison can order them. | 261115-234744-05b4d6; namespace_relay.cc:194; layout_export.cc:44 | Make the layouts answer ordered and comparable: (a) build it from the sealed state only, so a layout is a function of its seal, carry the seal in the answer (the DTMIRR01 `sequence`, now 0) and refuse any answer older than the mirror's newest adopted seal, drops included; or (b) carry the voter's applied index and refuse an older one. The test: `behind_probe.cc`'s four scenarios in `test_relay_mirror` (all refused), plus a mutant that removes the check. |
| D2 | yes | defect | `test_relay_service` fails on linux-x86_64-gcc: `relay upstream: next answered 409: {"error":"session_busy"}`. The authority keeps a W5 session busy until the previous answer retires, and `HttpUpstream::frames` retries only `410` (http_upstream.cc:269-272), while the delivery client treats `session_busy` as retryable (client.cc:419). The relays hit the same race on every sync (a counted sync failure); the probe fails the test. | CI run 43820855328, job linux-x86_64-gcc, test #252 at 12.75 s | Retry `409 session_busy` in `frames` (bounded, after a short wait), as `delivery::client` does; the failing CI job is the test, and a loop of `test_relay_service` runs on Linux is the confirmation. |
| D3 | yes | gap | Docs CI red: `check-docs` lists 15 unlisted gaps the quest adds: the endpoints `/v2/history/layouts` and `/v2/history/file`, the error `history_unavailable`, and the config keys `mirror`, `upstreams`, `interval_ms`, `answer_bytes` and the `layout` policy fields. | Docs run 43820854964 | Document them in the reference docs the checker reads (or list them where it expects). |
| D4 | no | gap | A history file above the authority's `reply_bytes` (16 MiB default; a base can reach `state_bytes`, 128 MiB) is read whole and hashed on the owner (service.cc:1152, `tier_bytes`), then refused `503 admission_full` in `reply_for`. With D7 (one refused file stops the whole adoption) the relay never adopts any namespace, refetches every file staged before the refused one, and retries every `interval_ms` with no backoff, re-reading that file on the authority's owner each time. | service.cc:1143-1165, 1184-1214; mirror_service.cc:231-259 | Refuse by the cached size before reading (`413`), back off a failing sync, and state the limit with its numbers in "Left open" 1; streaming stays open. |
| D5 | no | nit | The history routes use `fields`, not `request_fields`, so they skip the cluster, group and epoch check every other `/v2` route makes (service.cc:913, 925); a relay pointed at another cluster with a valid token adopts its layouts and files (frames are then refused by cluster). | service.cc:911-931 | Send and check the context, or note why not. |
| D6 | no | nit | A grant failure on the relay's layouts and file routes answers `400 invalid_request` (mirror_service.cc:193, 205), the authority's `prepare` answers its own check status; W5 answers `403 scope_forbidden` for the same refusal. | mirror_service.cc:191-206 | `403 scope_forbidden` throughout. |
| D7 | no | nit | `history_wanted` is also set by a file request (service.cc:2960-2963), while the doc says a layouts request triggers the enabling entry. | service.cc:2960 | Match the doc or the code. |
| D8 | no | nit | `test_relay_service` reserves eight ports by bind-and-close (`free_port`), the port-0 race review.md lists under Traps for parallel ctest. | test_relay_service.py:43-46 | Let each child report the port it bound, or bind with retry. |

## Not run

- The native check (261115-230026-744f5e) was still queued; I relied on CI for the full suite.
- The same-seal case of D1 is modelled in the scripted upstream, not reproduced against detentd.
- No process-level SIGKILL of a relay mid-sync: the HTTP path adds no disk step, and T7's kill
  matrix covers the store.
- No probe of a file above `reply_bytes` (D4 is from the code and the defaults).
- macOS CI jobs were still running.
