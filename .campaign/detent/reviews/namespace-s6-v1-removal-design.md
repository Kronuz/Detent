# namespace-s6-v1-removal review: design

Commit: Detent:a8e5d5fbf32e48d6ee2112403382e6757ddb6c6d (main; `git diff a900571a HEAD -- src include tests benchmarks tools` is empty, so the survey's snapshot is the code reviewed)
Verdict: CHANGES
Date: 2026-11-15

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

For a planned quest, CHANGES means: re-plan the quest file (dependency, split, criteria) as below
before anyone builds. The D8 direction stands; nothing here is a REJECT.

## Evidence I relied on

No job evidence exists for a planned quest. I read the quest file, PLAN D8 (row), D43, D84, D88,
D93, `docs/design/namespace-delivery.md` (D8, W1, W5-W13), `docs/design/reader.md` (A1-A4, D2, D4,
T2), `docs/status/namespace-s6-cutover.md`, `docs/status/namespace-s7.md`, the cutover and s6
correctness reviews (F5, F3), and the Codex survey at `codex/s6-v1-removal-survey:docs/design/s6-v1-removal-survey.md`.
Survey claims I checked against code at main, and found true:

- The table already holds N W1 entries; size `96 + Σ(137 + len)`, 626,784 B at 4,096 16-byte names
  (`src/detent_local/store_table.cc:12,59-79`).
- `derive_generation` already takes (incarnation, position) (`src/detent_local/derived_ids.cc:41`);
  the scope-named entry is `namespace_name`/`namespace_cursor`, a hash of the old cursor
  (`derived_ids.cc:58-86`, `src/detent_local/disk_store.cc:273,372`).
- The paged root writes the old cursor at offsets 56/64/80/96/112 (`src/detent_local/paged_layout.cc:344-348,367-371`);
  `state_commitment` does not cover the cursor (`paged_layout.cc:178`), so pages are unaffected.
- `AgentStore::publish` takes dropped names (`include/detent_local/agent_store.hh:95-98`).
- The one-shot is `POST /v2/distribution` (`src/detent_authority/service.cc:1199`); v1 and v2
  sessions share `/v2/distribution/session/*` (`src/detent_distribution_http/session_request.cc:87-91`).

## What I checked myself

No probe jobs: the decisions below do not hinge on a number I could not derive from code; the
numbers that bound D8's limits are named under "Probe first" for the build to measure. Code reads
behind each finding are cited in its row.

## Decision 1: one entry and generation per followed namespace (D8), with conditions

D8, not D88's scope-named entry. Reasons:

1. D88's entry cannot hold v2 state without new on-disk machinery. One generation over N
   namespaces needs N W1 cursors in it (the 320-byte root holds one), an aggregate generation-id
   rule over a cursor vector, and a build that replaces one namespace's subtree for a W7 full copy
   or a below-cut answer. No builder does that today (no subtree replace in `src/detent_local`).
   That is new recovery semantics: a D10 repair or a new incarnation of one namespace rebuilds and
   republishes the whole scope.
2. Everything downstream already assumes D8: W10 (a root holds one namespace), reader.md A1 (each
   namespace has its own entry and generation), the `change` file's per-namespace slot (reader D4:
   a publish in another namespace never moves a reader's word; with one entry every publish moves
   every reader), D7's relay prepared generation, and D9 seeding.
3. D8 needs no new on-disk recovery semantics: `AgentStore` publishes, drops, verifies and fails
   (`failed()`) per entry, and T7 already cuts a multi-namespace cycle (`docs/status/namespace-s7.md`,
   "The crash model").

D8's costs are runtime ones, which the survey names but does not bound. The plan must fix these
before building (findings F3-F5, F10, F11):

- C1. One coordinator, one W5 session, one `AgentStore` cycle per response. Per-namespace state
  (cursor, selected generation, recovered base, repair) is a record, never a `Coordinator` or a
  session per namespace. 4,096 coordinators would open 4,096 sessions and defeat W5.
- C2. Bounded file descriptors and mappings. Today each recovered paged generation opens its own
  directory descriptor and holds its lease descriptor for its lifetime
  (`src/detent_local/generation.cc:282,300`; `src/detent_local/leased_paged_root.hh:10-13`). An
  agent holding every namespace's base holds about 8,192 descriptors at 4,096 namespaces, against
  default soft limits of 1,024 (Linux) and 256 (macOS). Share the one store directory descriptor,
  decide whether the writer needs a shared lease on generations its own durable table reaches (the
  collector already keeps those), and recover a namespace's base on demand under a bound.
- C3. Reader attach is per namespace. The survey's "table selection and reader lifetime must hold
  one coherent table generation" is rejected. Namespace D1 and reader.md:117 promise nothing across
  namespaces, and requiring a coherent attach of N entries retries against every publication:
  `selection_of` gives up after three attempts (`generation.cc:153-158`), which with 4,096
  namespaces publishing becomes a livelock. A reader re-attaches only the namespace whose entry
  moved.
- C4. Readiness and failure scope per namespace (F5).

Format and on-disk: no table format change is needed to carry N entries, but the cursor change is
a format change (F2). Bump `agent_store_layout` to 2, so an old store is refused once, at table
decode, with `TableRefused` (D43: one case, one status), and bump the root to `DTROOT04` /
`format_version` 4. The test synthesizes the previous version's bytes (D43 forbids old fixtures).

## Decision 2: split into ordered landings, each leaving main working

"Everything at once" is right only for the consumer switch: the relay's upstream is the same
`Coordinator` the agent runs (`src/detent_relay/host.cc:224`), so agent and relay change delivery
together. The additive work before that switch and the deletions after it can each land alone.

| # | Landing | Main works because | Criteria |
|---|---|---|---|
| P1 | v2 hardening (additive) | v2 paths only get stricter; v1 still serves every consumer | A4; A5 as sharpened in F9; A6 plus F6; the `history.hh` tier comment |
| P2 | namespace-s8-wiring (already planned) | The relay serves W5/W6 from its mirror beside its v1 bundles | Its own A1-A4; A1 is the relay half of this quest's A3 |
| P3 | Local layer on the one W1 cursor + format | The coordinator maps the v1 cursor at its boundary, rebuilding the old identity from configuration (cluster, group, epoch, scope are all configured, `src/detent_delivery/configuration.cc:156-170`), as `namespace_cursor` already does | F2's refusal test; `detent_local`, `read_view`, `detent_testing`, benchmarks free of the old `Cursor` (grep job); T7 crash model and `test_derived_names` rerun; suites |
| P4 | Consumers on v2, D8 (agent and relay together) | Both move in one landing; the authority's v1 endpoints remain, unused | A3 with F8's oracle; F4; F5; F7; the 4,096 measurement (Probe first); agent client v1, one-shot receive and poll removed |
| P5 | Authority deletion | Nothing consumes v1 after P4 | A1, A2 (grep jobs: one-shot route, v1 session body, `AuthorizedScope`, `export_scope`, `subscription`, `DeliveryMode`, the old `Cursor`, `authority::NamespaceCursor`); suites |

P3 is optional: folded into P4 it saves the boundary mapping's one-landing life, at the cost of
putting the bytes-on-disk change into the riskiest landing. I recommend landing it separately. P1,
P3 and P5 are normal risk; P4 stays high and gets the durability and correctness reviews.

## Decision 3: criteria A1-A6 are not sufficient

Missing or underspecified, each a finding below: format refusal (F2), the relay dependency (F1),
reader over N entries (F4), readiness and per-namespace failure (F5), the unfreezable live
namespace (F6), scale on the only remaining path (F7), A3's oracle (F8), and A5's staging unit
(F9). A1, A2 and A4 are adequate as written.

## Decision 4: probe first, before freezing limits

Measure in P4's first task, as jobs, at 1, 1,000 and 4,096 namespaces (100 knobs each, plus the 5M
namespace in one arm):

1. Agent descriptors and mappings (`/proc/self/fd`, `vm.max_map_count` use) once ready, and during
   a cycle that republishes every namespace.
2. Startup to ready against namespace-delivery's 0.3 s restart gate: `AgentStore` verification of
   4,096 entries (opens, `.image` and `.read` SHA-256, `.live` checks) plus base recovery.
3. `encode_table`/`decode_table` and the SHA-256 per cycle and per reader re-read; at 1,000
   entries this counts against reader T2's 1 ms first-read gate.
4. Agent RSS and ledger use per idle namespace and per active one.
5. A 5M bootstrap answer on v2: authority delivery-ledger peak and agent staging peak (F7).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A3's relay chain on v2 depends on unlanded work the quest does not name. The relay's HTTP service answers only v1 sessions, and its upstream is a v1 `Coordinator` per bundle; W5/W6 from the mirror over HTTP is namespace-s8-wiring A1, still planned. | `src/detent_relay/service.cc:208` (`"version":1`); `src/detent_relay/host.cc:224`; `.campaign/detent/quests/namespace-s8-wiring.md` | Make namespace-s8-wiring a prerequisite in Contracts (P2), or fold its A1-A3 in explicitly. |
| F2 | yes | gap | No criterion covers the format change. The root and the flat header encode the old cursor, and readers bind it to the table (`namespace_cursor(descriptor.cursor) == entry.cursor`); replacing the type changes bytes on disk, and D43 requires an explicit refusal. | `paged_layout.cc:344-348,367-371`; `include/detent_local/layout.hh:16-18`; `generation.cc:146` | Bump `agent_store_layout` to 2 and the root to `DTROOT04`. Test: a store whose table carries layout 1, and a root with format 3, each synthesized from current bytes, are refused with one status and the agent bootstraps fresh; rerun T7. |
| F3 | yes | gap | D8's per-namespace resource cost is unbounded in the plan: two held descriptors per recovered paged generation (its own directory descriptor and its lease), so about 8,192 at 4,096 namespaces, past default soft limits. | `generation.cc:282,300`; `leased_paged_root.hh:10-13`; `configuration.cc:163` (4,096 namespaces) | Condition C2, plus the Probe first measurements 1-4 as a criterion with a stated bound, such as descriptors O(1) plus attached namespaces. |
| F4 | yes | gap | Readers are single-entry: `read_selected` refuses a table larger than `table_bytes(1)` and anything but one entry, and the system oracle requires one entry. No criterion moves them to N entries or covers reader lifetime across publications. | `generation.cc:101,112`; `tests/detent_system/managed.py:34` | Criterion: the read path finds its namespace's entry by name; a reader of B keeps its generation and lease through 1,000 publications of A and moves only when B moves; collection waits for held leases (C3). |
| F5 | yes | gap | Readiness and failure scope are defined for one selected generation. With N namespaces, nothing says when the agent is ready, and a generation that does not recover fences the whole store. | `src/detent_agent/managed_host.cc:19-27`; `docs/status/namespace-s7.md` ("Startup", "A generation that does not recover fences the store") | Criterion: ready when every followed namespace is published or known absent (with `all`: the directory reached and every live granted name published); a namespace failing verification is refetched as a full copy while the others serve (D10, `failed()`); only a refused table fences. |
| F6 | yes | gap | A6 covers half of cutover review F5. A live followed namespace is also omitted silently when the voter's table is not at the sealed state (`freeze` returns null for every name), while `all`'s directory still advances. | `src/detent_authority/namespace_history.cc:182-187`; `.campaign/detent/reviews/namespace-s6-cutover-correctness.md:73` | Extend A6: an unfreezable live namespace gets an explicit answer (503 or a retry frame) and the client keeps its cursor; test both cases. |
| F7 | yes | gap | v1's removal makes v2 the only bootstrap path at RC-3 scale. A v2 answer is buffered whole in the authority's delivery ledger and refused past `delivery.bytes`, and `all`'s bootstrap carries a full copy of every granted namespace in one answer. Nothing shows the 5M preset still bootstraps. | `src/detent_authority/frames_export.cc:30-37`; `docs/status/namespace-s6-cutover.md` ("Grants"); `tests/detent_system/presets/capacity-5000000.json` (`wire_bytes` 512 MiB) | Criterion: the 5M capacity system run bootstraps and catches up on v2 within its preset; or define how an over-bound answer splits across nexts (the client follows subsets; every next carries its own cursors) and test it. |
| F8 | no | gap | A3 names "the system suites" but not the oracle. | quest A3 | Every followed namespace's cursor and state commitment at the agent equals the authority's at that W1 cursor, through authority, relay, relay, agent, including a W7 re-creation and a drop through the chain. |
| F9 | no | gap | A5 leaves the staging unit open. Staging raw bytes would double peak memory; with D8 the natural unit is the response's prepared generations, published in one `AgentStore` cycle only after `FramesOutcome::Frames`. | `include/detent_delivery/namespace_session.hh:49`; s6 review F3 | Test a two-namespace response, namespace 1 complete in fragment 1 and fragment 2 corrupt: table, `change` words and coordinator cursors unchanged, and the ledger back to its prior use. Keep the early-apply mutant. |
| F10 | no | nit | "A coordinator generation and reader per namespace" (quest, D93) can be read as one `Coordinator` per namespace. | quest Decisions | Record C1 in D93. |
| F11 | no | nit | Under D8 the table costs O(N) per cycle: 626,784 B rewritten, fsynced and hashed at 4,096 names, and rehashed by every reader that re-reads it. | `store_table.cc:12,59-79` | Probe first, measurement 3; batch namespaces per cycle, as D8 already says. |
| F12 | no | nit | The flat-v1 image (`DTIMAGE1`, `LocalImageFormat::FlatV1`) also carries the old cursor, so it is a second format to revise. | `include/detent_delivery/coordinator.hh:18`; `src/detent_local/layout.cc:8` | Owner's call: revise it in P3 (keeps `image_format: flat-v1`), or delete it (removes a configuration value). |

## Not run

- No builds, tests or measurements; every number above is arithmetic from code or a preset, and
  "Probe first" lists what to measure.
- I did not read the relay mirror (`namespace_relay.cc`, `mirror.cc`) beyond its interface, nor
  check s8-wiring's own plan in depth.
- I did not audit the survey's line inventories; they are lexical and the build's grep jobs
  supersede them.
- I did not check `.read`/`.live` for cursor bytes beyond the s7 status doc (it says neither holds one).
