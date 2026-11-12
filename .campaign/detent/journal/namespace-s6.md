# namespace-s6: journal

State: blocked on lead: the quest's file set does not reach the files W1-W8 and W13 change
Quest: .campaign/detent/quests/namespace-s6.md (the lead's; never edit it)

## Doing

Nothing in code yet. Survey done at `Detent:b4444e82` (main `51c43ac3`); stopped on the file set.

## Next

1. On the lead's ruling below: start with the option it picks (B: the library half in this set).

## Blocked on

**Lead: the file set.** Set: `src|include/detent_{delivery,distribution,distribution_http}/**`,
`tests/detent_{delivery,distribution}/**`, `docs/status/namespace-s6.md`. What each criterion
must change outside it (survey at main `51c43ac3`):

| Change | Files outside the set | Held by |
|---|---|---|
| W1 Cursor {cluster, incarnation, position} | `include/detent/cursor.hh` (global Cursor with scope, group, restore_epoch, revision); its users in `include/detent/read_view.hh`, `src/detent/read_view.cc`, `include|src/detent_local/*` (~27 files), `src/detent_agent/{main,managed_host}.cc`, `src/detent_testing/generation_writer.cc`, `benchmarks/{delta_decode_bench,local_corpus,paged_recovery_bench}*`. Two duplicate W1 types already exist: `src/detent_authority/state.hh:83` and `include/detent_local/agent_store.hh:21` (`NamespaceCursor`) | okt-followups (`src/detent_local/**`, active); S5.2 (`src/detent_authority/**`, planned) |
| W2 scope identity goes | `include/detent/scope.hh`, `src/detent_authority/export_scope.*`, `src/detent_local/layout.cc` | S5.2, okt-followups |
| W5/W6 A1 endpoint, frames | authority side: `src/detent_authority/{service,subscription,delivery_export,delta_export,export_capture,export_encoder}.*`; frames by the cut need S5.2's ring/blocks/base, which is planned, not started | S5.2; okt-followups (`export_encoder.*`) |
| A2 W7 incarnation as full copy | authority answer and the agent/reader switch (`src/detent_local/reader.cc:58-61`) | S5.2, okt-followups |
| A3 W8 relays as readers | `src/detent_relay/{sessions,registry,bundle,host,configuration,service}.cc`, `include/detent_relay/**` | namespace-s8 (active) |
| A5 W13 grants | `src/detent_authority/service.cc:592-609`, `src/detent_relay/configuration.cc` (`src/detent_delivery/configuration.cc:160` is in the set) | S5.2, namespace-s8 |
| A6 Q4 removal | `src/detent_authority/service.cc:1130` (`POST /v2/distribution`); tests in `tests/detent_authority/**`, `tests/detent_agent/test_agent_catch_up.cc`, `tests/detent_system/first_read_export.cc`; docs `ACTORS.md`, `AUTHORITY_EXPORT.md`, `RELAY.md`, `USAGE.md`, `DISTRIBUTION_ARCHITECTURE.md`, `SUBSCRIPTIONS_ARCHITECTURE.md`, `DELIVERY_INTEGRATION_ARCHITECTURE.md` | S5.2 |

Ordering knot: S6 lands after S5.2, but the endpoint, grants and removal live in
`src/detent_authority/service.cc`, which S5.2's set (`src/detent_authority/**`) holds.

Options:

- **A. Widen S6 to the whole cutover** (the table's files). Needs okt-followups landed, S8 paused
  (it lands after S6 anyway), and S5.2 narrowed to exclude the serving files (`service.*`,
  `subscription.*`, `delivery_export.*`, `delta_export.*`, `export_*`). One quest of an estimated
  3-5K lines across five libraries: hard to review as one high-risk quest, and it holds four
  quests' files.
- **B. Split (recommended).** S6 keeps its set plus `include/detent/cursor.hh` and the two
  duplicate-cursor headers (`src/detent_authority/state.hh`, `include/detent_local/agent_store.hh`,
  a few lines each), and delivers: the W1 per-namespace cursor as the one type both use; the W5
  request codec (`[{name, cursor}]` or `all` with a directory cursor, `wait_ms`) in
  `detent_distribution_http`; the W6 frame codec (ring entries, trimmed block, younger blocks
  whole, base; named blocks; tombstones for absent keys) and frame selection over a per-namespace
  history-source interface that S5.2 implements, in `detent_distribution`; the W13 grant matcher
  in `detent_distribution`, shared by authority and relay; the agent client side in
  `detent_delivery` (`namespaces` optional, `delivery_mode` gone). Proven in the set: A1 (bytes per
  answer against a test history source), A5 (matcher tests), A4 (suites). A cutover quest after
  S5.2 and okt-followups (authority endpoint and grants, `scope.hh` and the old global Cursor
  deleted, `POST /v2/distribution` removed, docs) takes A2 and A6; A3 (relay as a W5/W6 reader)
  moves to namespace-s8, which already rewrites the relay. Each piece lands green; the old global
  Cursor lives until the cutover deletes it (D43).
- **C. Keep S6 as written; it waits** until S5.2 and okt-followups land and S8 yields the relay.
  Nothing starts now.

Recommendation: B. It starts now with no overlap with an active quest (`include/detent/**` and
`include/detent_local/agent_store.hh` are free; `state.hh` is in S5.2's planned set, so S5.2
starts after this lands or rebases over a few lines), and it keeps each high-risk review to one
mechanism. A and C both wait on okt-followups and S5.2 before any S6 code.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |
| A6 | <no evidence yet> |

## Result

Nothing yet.

## Rulings I made

None yet.
