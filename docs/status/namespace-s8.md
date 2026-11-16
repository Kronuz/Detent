# Namespace S8: the relay mirror

S8 of [namespace delivery](../design/namespace-delivery.md) (D7, W8). A relay keeps, per namespace it serves, the upstream's base, blocks above level 0, level-0 sections and cached full copy, byte for byte, and answers W6 from its own disk after a restart. The ring stays in memory, mirrored from the upstream's W6 frames (S6). Code: `include/detent_relay/mirror.hh` and `namespace_relay.hh`, `src/detent_relay/mirror.cc` and `namespace_relay.cc` (target `detent::relay_mirror`).

## What the mirror is

The mirror computes no layout. It adopts the upstream's layout, a `NamespaceLayout` per namespace: the incarnation, the upstream seal and ordinal, the cut p, the policy in force (the upstream's bytes, `encode_policy` on a voter), and the files, each with its kind, the positions it covers, its size and SHA-256:

```text
base [0, p], none when empty | block [p+1, b1] ... | section [.., ..] ... (to s) | copy [0, s], optional
```

`check_layout` refuses anything else: an optional base at the cut and first, blocks then sections contiguous from p + 1 to s, at most one copy, at s and last, names unique. A voter writes no file for an empty base (a re-created namespace starts with one), so the cut is a field of its own. On a voter, a block is `block-<ns>-<incarnation>-<first>-<last>`, a base `base-<ns>-<incarnation>-<p>`, and a section `seal-<index>/<block name>`, served from its seal file, so a relay never receives another namespace's records. The mirror stores each upstream name under `m-` and 32 hex digits of its SHA-256 (`mirror_file_name`), so a namespace name with `/` is one flat entry.

## Directory

```text
mirror/                one directory, the mirror's alone
  mirror               the table, replaced by rename
  owner.lock           held exclusively by the one writer
  m-<hex>              one file per upstream name the table reaches
  .tmp-<random>        a file being written; startup removes it
```

`mirror` is a 32-byte header (magic `DTMIRR01`, layout 1, entry count, sequence, upstream seal), the entries sorted by name (name, incarnation, seal, ordinal, cut, policy, then each file's kind, first, last, size, digest and upstream name), then the SHA-256 of everything before it. A table that does not decode refuses the mirror.

## Adoption

`MirrorStore::adopt(seal, layouts, dropped, fetch)` runs D6's commit with the upstream's layout in place of the rules:

1. Validation: the next table, whole, at the upstream seal of the answer adopted, never below the table's. A file the durable table reaches under the same name and holds is reused, never fetched; the same name with another size or digest is the upstream contradicting an immutable name (`FetchRefused`). When nothing is fetched and the entries and seal are unchanged, nothing is written.
2. Fetch and stage: each other file is fetched by name, checked against its size and digest, and written under a `.tmp-` name, one file in memory at a time. A failed or refused fetch removes the staged names and publishes nothing; the mirror stays usable.
3. Link: `linkat` from the `.tmp-` name, then the `.tmp-` name unlinked. An existing name may be one the durable table reaches (a file failed at startup), so its replacement is fsynced and renamed over it, as a repair is: a power cut never leaves a reached name with unsynced bytes that startup's size check would accept.
4. One sync group (D11): the fetched files and the directory.
5. The table: `.tmp-`, fsync, rename over `mirror`, directory sync.
6. Collector: every file the new table no longer reaches is unlinked. A cut before the unlinks leaves orphans, which startup removes.

An I/O error while staging or after it leaves the mirror needing a restart (review D3: staging errors could clean up and keep the mirror usable; open).

## Startup

Take `owner.lock`, one barrier (a directory sync), read `mirror`, check each reached file by name and size, and remove every other name (`.tmp-` names and orphans). Nothing is re-hashed (D12.2): a reached file was synced before the table that reaches it was renamed. A namespace with a reached file missing or of the wrong size is `failed()` until an adoption fetches that file again.

## Reads and repair

`read(file, fetch)` returns a reached file's bytes. Its digest is checked at its first read in the process; a file that fails is fetched again by name, written under `.tmp-`, fsynced, renamed over its name and the directory synced (D6 Recovery: "fetched by name from a peer, which holds the same bytes").

## Reading and answering (W8)

`NamespaceRelay` is the relay as a reader and a source. It reads an `Upstream`, three calls (and `sources`, `skip` to move past a lagging one): `layouts(names)`, a table of the names' layouts at one upstream seal, `fetch(file)` by upstream name, and `frames(positions)`, the W6 body for W5 positions. It is an `Upstream` itself, so a relay reads another relay as it reads the authority, and it holds no consensus state: the library links `detent::authority` for the block codec and no consensus.

`sync(names)`:

1. Refuse an answer older than the mirror (`UpstreamBehind`): one at a seal below the table's, a layout below the one held for its name, another layout at the same seal, or a name missing at a seal where the mirror holds it. A layout is a function of its seal, so such an answer is a lagging voter, and nothing is adopted, dropped, fetched or collected. The relay moves to the upstream's next source (`skip`) and starts over; once every source answered behind, the sync fails and the service backs off.
2. Adopt the upstream's layouts (the mirror's adoption above) at the answer's seal; a name the upstream no longer holds is dropped with its ring.
3. Each ring keeps its entries after the new s; one behind s, or of another incarnation, restarts empty at s.
4. Ask the upstream for frames at each ring's head. Only entries are expected: anything else means the upstream sealed in between (`UpstreamMoved`), and the sync starts over, at most three times. Frames apply only once the body decodes whole.

`frames(positions)` answers from a `MirrorHistory` per namespace, S6's `NamespaceHistory` over the mirror: the cut and spans from the layout (blocks named, sections unnamed), each span decoded from its file at read time, the full copy from the cached copy or the base and every tier merged without tombstones, and the ring from the upstream's entries. S6's `select_answer` and encoder pick the tier and write the bytes, so a relay answers exactly as its upstream does. A namespace `failed()` at startup is not answered until it is adopted again, but its layout is still offered: a downstream relay keeps what it holds, and a missing file it asks for is fetched by name from this relay's upstream through `read`.

Both classes are single-threaded: `frames` holds references into the table that `adopt` replaces, and `read` updates the verified and missing sets. The service runs both on one thread (review D4; "The service" below).

## The service

detent-relay serves its mirror over HTTPS beside its v1 bundles (W8: a reader, never a member). A `mirror` section in the relay configuration names the mirror directory (mode 0700), the authority's cluster, the namespaces mirrored, and the upstream: the authority's voters or another relay, with a credential token and a CA. A relay may run with no bundles when it has a mirror, and its credentials carry W13 `namespaces` patterns for version 2 beside or instead of `bundles`.

One mirror thread (`MirrorService`) owns the `NamespaceRelay`, so the single-thread rule (review D4) holds by construction: it syncs every `interval_ms` (each failed sync in a row doubles the wait, up to 30 s or one interval) and answers every request the HTTP lanes hand it. The relay reports `ready` after the first sync, and its `/v1/health` carries the mirror's counters (`syncs`, `fetched_files`, `reused_files`, `answers`, `sessions`, ...). A mirror that stops on a logic error stops the relay.

`HttpUpstream` is the `Upstream` over HTTPS: one request per connection, each step under one deadline, the next endpoint after a failure or a lagging answer (`skip`). Its W5 session is opened on first use and again on `410`; every next carries the ring heads with `wait_ms` 0, and a `204` is a body with no frames. A `409 session_busy` (the session's previous answer not yet retired) keeps the session and asks again, up to ten times, waiting 1 ms and doubling.

The routes, the same at a voter and at a relay, all `POST` with a bearer credential:

| Route | Request | Answer |
| --- | --- | --- |
| `/v2/history/layouts` | `{"namespaces": [name, ...]}`, each granted | `200`, `application/vnd.detent.layouts`: a DTMIRR01 table (sequence 0, the seal it was built at) of the live names' layouts, the encoding the mirror stores; a name not live is left out |
| `/v2/history/file` | `{"namespace": name, "name": file}`, the namespace granted | `200`, `application/octet-stream`: the file's bytes, only when the namespace's current layout names it (a section as `seal-<i>/<block name>`, cut from its seal file); `404` otherwise |
| `/v2/distribution/session/{open,next,close}` | W5, version 2 | W6 frames in DTDIST01 fragments, exactly as the authority's |

At a voter, both history routes answer on the owner from the voter's own sealed history, with no quorum read: the files are immutable, and the layouts answer is built from the sealed state alone (liveness, incarnation and origin at the seal, the files from the table at that seal), so every voter at one seal answers the same bytes. Each answers `503 history_unavailable` until positions are enabled and the history is sealed, and a layouts request triggers the enabling entry as a W5 next does. A file is read and checked against the table's digest once, and its size is cached; a file above the reply bound (`reply_bytes` less 256) answers `413 reply_bound`, before it is read once a layouts answer sized it. A relay refuses any answer older than its mirror (`sync` above), so a lagging voter, which `HttpUpstream` reads after it rotates, is never followed backwards, and the relay moves on to the next voter.

At a relay, the history routes and W5 version 2 go to the mirror thread; version 1 stays with the bundles. A W5 session at a relay follows only names it mirrors and the credential grants (`403 scope_forbidden` otherwise, `400` for `all`), its history routes answer only granted names (`403 scope_forbidden`), and a next before the first sync gets `503 source_behind`. Member routes (`/v2/open`, `/v2/transactions`, `/v2/read`, `/v2/result`, ...) are `404`, a configuration with `voters` is refused, and detent-relay links no consensus.

The authority's configuration takes the layout policy its enabling entry proposes as `application_limits.layout` (`seal_transactions`, `period`, `carry_bytes`, ...), validated as `SetLayout` validates it.

`test_relay_service` runs three detentd voters, relay A reading them and relay B reading A, all over HTTPS, and reads each through `HttpUpstream`:

1. A1: after writes before and while the relays run, every base, block and section and every W5/W6 answer (none, another incarnation, each R from 0 to h + 1, raw and zstd) has the same SHA-256 at the leader, A and B, and A's and B's disks hold each file under `m-<hash of its name>` with those bytes. The same probe through a proxy that answers every other next `409 session_busy` gets the leader's answers unchanged.
2. A2: A and B restart, report 0 files fetched and the rest reused, and answer every cursor unchanged; after more writes, each fetches at most the files new to the layout.
3. A3: both relays answer member routes `404`, a `voters` configuration is refused, and `nm detent-relay` holds no `kronuz::consensus` symbol.

## Evidence

| Criterion | Test | What it shows |
| --- | --- | --- |
| A1 | `test_relay_chain` | Three real voters (S5.2's history under the cluster harness) and a chain A, B, C: A reads a different voter each step, B reads A, C reads B. Over 14 steps with seals, roll-ups, a policy change, a drop and a re-creation (an empty base), every file of every voter's layout is on every relay under its name with the voter's bytes, and every relay's table equals each voter's layout |
| A1 (relay half) | `test_relay_mirror` `chain` | A scripted upstream and three relays: every file byte-identical at every hop over 24 steps, and no relay reaches past its upstream |
| A1 (lagging voter) | `test_relay_mirror` `behind`; `test_relay_chain` | A relay that read a newer upstream refuses an older one, before and after a restart: the same incarnation, another incarnation, a dropped name, a name created later, another layout at the same seal; nothing is adopted, fetched or collected. Each voter's own layouts export equals the oracle's at every step, and a drop applied after the latest seal leaves it unchanged until the next seal |
| A2 | `test_relay_chain` restart; `test_relay_mirror` `restart` | C restarts with its upstream refusing files, A restarts on the voter: each re-reads its ring through W6, fetches 0 files, and answers every cursor with the same bytes; B restarts with a file lost and C, syncing from B first, deletes and fetches nothing; the store alone reads every file unchanged and fetches only new names at the next seal |
| A3 | `test_relay_mirror_crash` | T7 over completion's sync seam: a power cut after every primitive of a first adoption, a second (reuse, fetch, cut, squash, drop, policy change, collector), a repair, and the replacement of a reached file failed at startup, then a restart that adopts again, cut in turn; a kill after every primitive, then a restart cut in turn |
| A4 | `test_relay_mirror` `follows`; `test_relay_chain` | The scripted upstream over 24 steps (seals, roll-ups, squashes, cuts, a policy change, a cached copy, a new incarnation, a drop, a new namespace): the table equals the upstream's layout, the directory holds exactly the reached names, the bytes fetched are exactly the new names'; over real voters, the relays follow a period change and a drop |
| A5 | `test_relay_chain` | W8: every relay answers every store cursor (none, another incarnation, each R from 0 to h + 1) with the same W6 bytes as an oracle that replays every write, tiled as the voter's layout; B and C read only their upstream relay, and only through `Upstream` |

## Left open

1. A fetch returns one file's bytes whole; a base of a large namespace wants a streamed fetch into the `.tmp-` file. Until then a voter refuses a file above its reply bound (16 MiB by default, while a base can reach `state_bytes`, 128 MiB) with `413`, and a relay that needs one fails each sync (one refused file stops the whole adoption) and backs off.
2. The ring is unbounded between seals: the service's `answer_bytes` account funds answers, not the ring.
3. From the durability review: D3 (a staging I/O error poisons the store until restart instead of cleaning up), D4 (the single-thread rule, above), D5 (`RingSink` trusts entry positions inside (after, to]), D6 (the ring is memory only; the quest's Outcome says "persisted mirror of ... ring"), D7 (one refused namespace stops the whole adoption).
