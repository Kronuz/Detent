# Namespace S6: the namespace session wire (W1, W5, W6, W13)

S6 of `docs/design/namespace-delivery.md`, split by D84: this step lands the library pieces of the
per-namespace session; namespace-s6-cutover moves the authority onto them, removes the scope body,
`POST /v2/distribution` and the old `Cursor`; namespace-s8 makes the relay a W5/W6 reader.

| Piece | Where | Tests |
| --- | --- | --- |
| W1 cursor | `include/detent/cursor.hh`: `NamespaceCursor` {cluster, incarnation, position}, `DirectoryCursor` {cluster, position} | the agent store uses them (`include/detent_local/agent_store.hh`) |
| W13 grants | `include/detent_distribution/grants.hh`: `NamespaceGrants` | `tests/detent_distribution/test_grants.cc` |
| History interface (S5.2 implements it) | `include/detent_distribution/history.hh`: `NamespaceHistory`, `select_answer` | `tests/detent_distribution/test_session_frames.cc` |
| W6 frames | `include/detent_distribution/session_frames.hh` | `tests/detent_distribution/test_session_frames.cc` |
| W5 request (version 2) | `include/detent_distribution_http/session_request.hh` | `tests/detent_delivery/test_session_request.cc` |
| Agent client | `include/detent_delivery/namespace_session.hh` | `tests/detent_delivery/test_namespace_session.cc` |

## The history interface

A server answers a namespace from a `NamespaceHistory` frozen for one answer:

```text
 base: state at p | spans tiling (p, s], oldest first | ring entries (s, h]
                    named = a persisted block; unnamed = a level-0 section (always the newest)
```

Seven calls: `head()` (cluster, incarnation, h), `cut()` (p), `ring_start()` (s), `spans()`,
`read_span(i)` (key order), `read_ring(after)` (by position, then key, each key once per
position) and `read_full_copy()` (the state at s, live records in key order). A record is
{key, revision, definition or null for a tombstone}. The encoder checks every promise and throws
`HistoryFailure` on a broken one, a source failure and never a client's fault.

`select_answer` picks D6's tier for a store at cursor R:

| Store | Answer |
| --- | --- |
| none, another cluster or incarnation | full copy: the state at s, then the ring's entries |
| R > h | Ahead: no answer until the source catches up (D1) |
| R = h | Current: nothing |
| s <= R < h | Entries: the ring's entries (R, h] |
| p <= R < s | Blocks: the span holding R + 1 trimmed above R, each younger named span whole, the unnamed spans merged into one piece (younger record wins), then the ring's entries |
| R < p | full copy |

A store whose cursor carries the namespace's identity at position 0 (a namespace it learned from the
directory) is an empty store of that incarnation and gets entries or blocks; with no store it gets a
full copy, empty at position 0 for an empty namespace.

## W6: the frames body

One session response carries every namespace's frame. The body is a sequence of MessagePack
values, carried in DTDIST01 fragments of `UpdateKind::Frames` (SHA-256 per fragment, optional zstd):

```text
[1, directory | nil, frame count]
per frame:  [name, kind, after, [cluster, incarnation, position], piece count]
per piece:  [piece kind, first, last]  record*  nil
record:     [key, revision, definition | nil]          nil: a tombstone
directory:  [cluster, from, to, [[name, incarnation, live], ...]]
definition: [default, rollout_pct | nil, rollout_value | nil, [[group, 0, value] | [group, 1, pct], ...]]
```

Frame kinds: 1 entries, 2 blocks, 3 full copy. Piece kinds: 1 entry [p, p]; 2 block, a named span
sent whole, fetchable by (namespace, incarnation, first, last): every named span that starts above
R; 3 span, trimmed (the span holding R + 1 when it starts at or below R) or merged (level-0); 4 full
copy [0, s], fetchable by (namespace, incarnation, s). Frames come in name order; a piece's records
in key order, each key once. Records end with nil instead of a count, so a server streams a trimmed
block or a merged level-0 without reading it twice.

The decoder checks, before the sink sees the value that completes them: the frame starts after
`after` (full copy: after 0) and its pieces run contiguously to the target position; the first span
holds after + 1; a whole block starts at the next position; entries follow spans or the copy and
each carries its own position; no piece follows the target; every record lies inside its piece and
above `after`, belongs to the frame's namespace, and a full copy holds no tombstone; definitions pass `definition_error` and the
`Limits` bounds. An agent applies a tombstone for a key it lacks as a no-op (D8 "Catch-up input").

Golden answer (`test_session_frames.cc`): x = 1, y = 2, delete x; block [1, 2]; store at 1:
76 bytes, a span [1, 2] holding y at 2, then entry 3 deleting x.

## W5: the request (version 2)

```text
open  {"version":2, "profile":1, "coding":"raw"|"zstd", "cluster"?, "namespaces":[...], "all"?, "wait_ms"?}
next  {"version":2, "session", "cluster"?, "namespaces":[...], "all"?, "wait_ms"?}
close {"version":2, "session"}
namespace: [name] (no store) or [name, incarnation, position]
```

Names are valid and strictly increasing; a cursor or a nonzero `all` needs `cluster`; a session
follows at least one namespace or `all`. The cluster is the request's, once: a store of another
cluster earns the same full copy as no store, so the client sends it as `[name]`.

Responses: open 200 `{"version":2,"session","wait_ms"}`; next 200
`application/vnd.detent.frames` with `X-Detent-Session-Version: 2`, `X-Detent-Session` and
`X-Detent-Coding`, or 204 when nothing changed within `wait_ms`; close 204. Errors keep the
version 1 statuses and `error` names (`detail::session_reset_reason`, one mapping for both).
`X-Detent-Kind` and `X-Detent-Revision` are gone. Poll mode is next with `wait_ms` 0 every
`poll_interval_ms` (Q4); the cutover removes `delivery_mode` and the one-shot endpoint.

## W13: grants

A pattern is an exact name, a whole-component prefix `p.*` (every name below `p.` at any depth,
never `p` itself) or `*`; any other form is refused. `allows` costs one binary search for the exact
names plus one per `.` in the name for the prefixes.

## Measured

Bytes per answer over the test churn (40 transactions over ten keys of one namespace; cut 8, blocks
[9, 16] and [17, 24], level-0 [25, 28] and [29, 30], ring (30, 40]): a full copy 545 bytes (22
records); a blocks answer from the cut 835 bytes (38 records) falling to 445 (17) at R = 29; ring
answers 414 (15) at R = 30 down to 73 (1) at R = 39. The cut's placement is S5.2's (the cost rule);
these tilings are the test's, so a blocks answer above the copy here says nothing about the rule.
