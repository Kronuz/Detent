# namespace-s8-wiring review: durability

Commit: Detent:0ef1117f3d54c794c9615a6c7c46b24f824d25c8 (code identical to Detent:110579034daa, where the jobs ran)
Verdict: APPROVE
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-review of the changes since Detent:7a98e14b (the D11 round), lens `durability`, against A5 as
D101 restates it: the readiness gate and the absence rule narrow the window, the full guarantee
is voter-read-freshness's. The earlier rounds' findings and evidence stand where not restated.

## Evidence I relied on

- Native suite 261116-082634-2fbb70 (exit 0, 275 of 275 at 11057903): the gate changes `/v1/health`
  `ready` for every consumer (system, fleet, DNS-voter tests read it); none regressed.
- Relay suite 261116-082652-fee406 (exit 0): "D9: relay C refused 1 older layouts answers and
  followed the current voter"; "A5: voter 2 back alone (applied 54, the quorum's 62) refused 503
  from its return"; "A5: voter 2 at D's seal and alone refused 503"; relay D converged both times.
- Soak 261116-082655-e15f57 and 261116-082658-3a05d9: 10 of 10 each beside 6 `yes` hogs.
- Mutants, each killed by the assertion it targets (logs read):
  `absence-trusted` 261116-082702-991d63 ("voter 0, knowing no leader, answered
  /v2/history/layouts 200", in `refuses`); `history-not-gated` 261116-082706-2a4c28 (same
  assertion, voter 2); `refusal-not-skipped` 261116-082709-6a5056 ("relay C failed a sync on a
  source that refused", `sync_failures` 1).
- Pins 261116-082634-e969e2.

## What I checked myself

Scripts under `.scratch/review-namespace-s8-wiring-durability/` in the home checkout.

1. `stall_probe.sh` + `stall_probe.py`, job 261116-100048-41d4e2: builds detentd with a 2.5 s
   sleep injected in an owner pass just before an `AppendResponse` stamps `leader_contact` (a
   per-pid flag file), stops the leader and the other voter during the sleep, and asks the stalled
   voter for layouts every 50 ms meanwhile. First run 261116-100048-41d4e2: driver could not find
   the binaries (paths fixed); rerun 261116-102225-cb11b4 was still queued when this review was
   written: its HOLE / NO HOLE line settles D12's reachability.
2. Code reading for the lead's questions, below.

Answers to the lead's questions:

1. The gate. One predicate: `serving` (service.cc:1753) feeds `/v1/health` `ready` (:1866) and
   both history routes (:2984), and the history routes answer only on the owner, so a route and
   the health field cannot disagree beyond the 250 ms health publication. The relay maps any 503
   to layouts to `UpstreamBehind` without rotating (http_upstream.cc:242-246); `sync` skips it
   (namespace_relay.cc:263-267, `skip` rotates), so a refusing voter costs one request per sync
   and nothing is adopted. Bounded: `behind` < `sources()` per sync, then the sync fails and backs
   off (mirror_service.cc:343-344, doubling from `interval_ms`, capped at max(30 s, interval)).
   A 503 to `/v2/history/file` stays `UpstreamUnavailable` (rotate, fail the sync, back off): a
   voter losing its leader between layouts and fetch, bounded. False not-ready under load: the
   loop wakes every 1 ms when idle (service.cc:3140), so the absence rule fires only on a real
   start-to-start gap of `election_min` (1 s by default, service.cc:484), at which the cluster
   could itself elect; the cost is that voter (leader included, since `trusting()` gates the
   `Role::Leader` branch too) refusing for `election_max` (2 s). Relays stop syncing only if every
   voter refuses at once, and then recover within about twice the episode (the backoff doubling).
   Acceptable, and documented: `docs/status/namespace-s8.md` "(stopped, or its loop starved)" and
   journal ruling 18. The website reference omits the absence rule (nit D14).
2. The absence rule against the traced mechanism (SIGSTOP while idle, queued frames read after
   SIGCONT): sound. A stop almost always lands in the 1 ms timer wait; peer frames are delivered on
   the same executor and processed only inside `node.service` (service.cc:2022), after the pass top
   has set `returned` (:1999-2001), so no queued frame stamps contact during the hold, and a
   contact read during it is never stamped later. Two paths remain outside D101's window: D12 (a
   stall that begins mid-pass) and, much weaker, frame backlogs that take longer than
   `election_max` to drain after a return (the trace drained in under 100 ms; non-blocking note).
   A partitioned but running leader keeps `Role::Leader` until CheckQuorum (D41) steps it down,
   within `election_max`: that is D101's documented window.
3. D1-D10: not weakened. The 503 skip throws before `store.adopt`, as D9's refusals do; the
   other non-200 answers still go through `require` (rotate, `UpstreamUnavailable`), so 413 and
   the fetch path are unchanged; `frames` and the busy retry are untouched. D9's older-seal skip
   is still exercised (C's `later` convergence after `lagging`; the log line above), now with
   `gated` first, so C's first sync also exercises the 503 skip.
4. Tests: `refuses(x, returned)` asserts every answer 503 `not_ready` for 1 s from SIGCONT after a
   stop of at least 2.5 s, inside the 2 s hold that the first pass sets before any request is
   answered: deterministic except for D12's mid-pass case (rare under SIGSTOP of an idle loop).
   `refuses(i)` (others stopped, so no leader can reach i) is deterministic. Relay D's convergence
   and relay C's zero failed syncs are the relay half. The moved cases are preserved verbatim and
   uncalled in `a5_voter_read_freshness` (test_relay_service.py:700-757), named by
   voter-read-freshness's A2; that quest will need to start relay D only once if both run (nit D15).

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
| D9 | - | defect | Resolved (round 3); unchanged by this round, still exercised. | 261116-082652-fee406 (D9 line) | - |
| D10 | - | gap | Resolved (round 3). | test_relay_service.py:451-476 | - |
| D11 | - | defect | Moved by D101 (owner): the readiness gate and the absence rule land here as A5 [decision] D101; never adopting from a voter cut off from its leader is voter-read-freshness's A2. What this round lands holds as D101 states it, with D12 below. | 261116-082652-fee406, mutants above; PLAN.md D101 | - |
| D12 | no | defect | The absence rule checks the gap only at the top of a pass (service.cc:1998-2001). A stall that begins mid-pass (a SIGSTOP or descheduling after the top, before the network actions) is not caught in that pass: frames delivered before the stall are processed after it, their `AppendResponse` stamps `leader_contact = Clock::now()` while `trusting()` still holds (:2033-2038), and the history tasks the HTTP lanes queued meanwhile are answered later in the same pass (:2971-2986) with `serving()` true, on a leader last heard from before the stall (possibly far past `election_max`). The next pass sets the hold, and the stamped contact is already older than the hold when it ends, so exposure is one pass; staleness only (D1/D9 hold). Rare under SIGSTOP of an idle loop; also a theoretical flake source for `refuses(x, returned)`. From code; the probe job (first run 261116-100048-41d4e2 had wrong binary paths, rerun 261116-102225-cb11b4 pending at hand-off) prints HOLE or NO HOLE. | 261116-100048-41d4e2; 261116-102225-cb11b4 (pending); service.cc:1998-2001, 2033-2045, 2971-2986 | Options: 1. (recommended, one line) stamp `leader_contact = owner_started` instead of `Clock::now()` (both sites, service.cc:2038 and :2045): every frame a pass processes was delivered before the pass began, so a mid-pass stall ages the contact instead of refreshing it; or make `trusting()` also require `Clock::now() - last_pass < election_min`. Test: `stall_probe` (a flag-file sleep before the contact stamp) answers no 200 with the leader stopped; it needs a test hook, so a mutant-style patch run, not the suite. 2. Leave it to voter-read-freshness, whose A2 already lists the resumed-after-stop voter. |
| D13 | no | nit | The absence rule assumes the frames queued during an absence drain within `election_max`; a backlog read later still stamps contact when read. Only a voter whose leader queued more than 2 s of reading for it, then died. | journal ruling 18 (drain under 100 ms traced) | voter-read-freshness closes it; no action here. |
| D14 | no | nit | The authority HTTP reference (website authority.md:208) says `503 not_ready` "while the voter knows no live leader" but not that a voter back from an absence of `election_min` refuses for `election_max`, which `docs/status/namespace-s8.md` states. | website/src/content/docs/reference/http-api/authority.md:208 | One clause, or leave to voter-read-freshness's A4. |
| D15 | no | nit | `a5_voter_read_freshness` starts relay D itself, as `a5` does; enabling both as is starts D twice. | test_relay_service.py:700-716 | voter-read-freshness adapts it (or gives it relay `e`). |

## Not run

- No process-level network partition (iptables/pf) of a voter; the partitioned-leader window is
  from the code (role short-circuit, CheckQuorum D41) and is D101's.
- No measurement of how often a production owner pass exceeds `election_min`; the suite exposes
  `owner_turn` but no job here read its maximum under load.
- `stall_probe` injects the stall with a sleep at one point in the pass; a real SIGSTOP landing
  mid-pass was not reproduced (its probability is the pass prefix over the 100 ms heartbeat).
- The previous round's non-blocking notes (the D9 log line counts proxy-stale answers; the
  dependence on a seal during `churn(27, 1)`) stand.
