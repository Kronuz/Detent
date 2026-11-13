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

`mirror` is a 32-byte header (magic `DTMIRR01`, layout 1, entry count, sequence), the entries sorted by name (name, incarnation, seal, ordinal, cut, policy, then each file's kind, first, last, size, digest and upstream name), then the SHA-256 of everything before it. A table that does not decode refuses the mirror.

## Adoption

`MirrorStore::adopt(layouts, dropped, fetch)` runs D6's commit with the upstream's layout in place of the rules:

1. Validation: the next table, whole. A file the durable table reaches under the same name and holds is reused, never fetched; the same name with another size or digest is the upstream contradicting an immutable name (`FetchRefused`). When nothing is fetched and the entries are unchanged, nothing is written.
2. Fetch and stage: each other file is fetched by name, checked against its size and digest, and written under a `.tmp-` name, one file in memory at a time. A failed or refused fetch removes the staged names and publishes nothing; the mirror stays usable.
3. Link: `linkat` from the `.tmp-` name. An existing name is not one the durable table holds, so it is unlinked and relinked (it may hold bytes whose sync never ran).
4. One sync group (D11): the fetched files and the directory.
5. The table: `.tmp-`, fsync, rename over `mirror`, directory sync.
6. Collector: every file the new table no longer reaches is unlinked. A cut before the unlinks leaves orphans, which startup removes.

An I/O error after staging leaves the mirror needing a restart.

## Startup

Take `owner.lock`, one barrier (a directory sync), read `mirror`, check each reached file by name and size, and remove every other name (`.tmp-` names and orphans). Nothing is re-hashed (D12.2): a reached file was synced before the table that reaches it was renamed. A namespace with a reached file missing or of the wrong size is `failed()` until an adoption fetches that file again.

## Reads and repair

`read(file, fetch)` returns a reached file's bytes. Its digest is checked at its first read in the process; a file that fails is fetched again by name, written under `.tmp-`, fsynced, renamed over its name and the directory synced (D6 Recovery: "fetched by name from a peer, which holds the same bytes").

## Reading and answering (W8)

`NamespaceRelay` is the relay as a reader and a source. It reads an `Upstream`, three calls: `layouts(names)`, `fetch(file)` by upstream name, and `frames(positions)`, the W6 body for W5 positions. It is an `Upstream` itself, so a relay reads another relay as it reads the authority, and it holds no consensus state: the library links `detent::authority` for the block codec and no consensus.

`sync(names)`:

1. Adopt the upstream's layouts (the mirror's adoption above); a name the upstream no longer holds is dropped with its ring.
2. Each ring keeps its entries after the new s; one behind s, or of another incarnation, restarts empty at s.
3. Ask the upstream for frames at each ring's head. Only entries are expected: anything else means the upstream sealed in between (`UpstreamMoved`), and the sync starts over, at most three times. Frames apply only once the body decodes whole.

`frames(positions)` answers from a `MirrorHistory` per namespace, S6's `NamespaceHistory` over the mirror: the cut and spans from the layout (blocks named, sections unnamed), each span decoded from its file at read time, the full copy from the cached copy or the base and every tier merged without tombstones, and the ring from the upstream's entries. S6's `select_answer` and encoder pick the tier and write the bytes, so a relay answers exactly as its upstream does. A namespace `failed()` at startup is neither answered nor offered downstream until it is adopted again.

## Evidence

| Criterion | Test | What it shows |
| --- | --- | --- |
| A1 | `test_relay_chain` | Three real voters (S5.2's history under the cluster harness) and a chain A, B, C: A reads a different voter each step, B reads A, C reads B. Over 14 steps with seals, roll-ups, a policy change, a drop and a re-creation (an empty base), every file of every voter's layout is on every relay under its name with the voter's bytes, and every relay's table equals each voter's layout |
| A1 (relay half) | `test_relay_mirror` `chain` | A scripted upstream and three relays: every file byte-identical at every hop over 24 steps, and no relay reaches past its upstream |
| A2 | `test_relay_chain` restart; `test_relay_mirror` `restart` | C restarts with its upstream refusing files, A restarts on the voter: each re-reads its ring through W6, fetches 0 files, and answers every cursor with the same bytes; the store alone reads every file unchanged and fetches only new names at the next seal |
| A3 | `test_relay_mirror_crash` | T7 over completion's sync seam: a power cut after every primitive of a first adoption, a second (reuse, fetch, cut, squash, drop, policy change, collector) and a repair, then a restart that adopts again, cut in turn; a kill after every primitive, then a restart cut in turn |
| A4 | `test_relay_mirror` `follows`; `test_relay_chain` | The scripted upstream over 24 steps (seals, roll-ups, squashes, cuts, a policy change, a cached copy, a new incarnation, a drop, a new namespace): the table equals the upstream's layout, the directory holds exactly the reached names, the bytes fetched are exactly the new names'; over real voters, the relays follow a period change and a drop |
| A5 | `test_relay_chain` | W8: every relay answers every store cursor (none, another incarnation, each R from 0 to h + 1) with the same W6 bytes as an oracle that replays every write, tiled as the voter's layout; B and C read only their upstream relay, and only through `Upstream` |

## Left open

1. The HTTP wiring: the relay's routes for `layouts`, `fetch` and W5 sessions, and an `Upstream` over HTTP. The authority's side of `layouts` and `fetch` has no endpoint yet (namespace-s5-3 provides history files by name; nothing exports the layout), and it serves W5/W6 only after namespace-s6-cutover.
2. A fetch returns one file's bytes whole; a base of a large namespace wants a streamed fetch into the `.tmp-` file.
3. The ring is unbounded between seals; a bound in bytes, as the authority's, belongs with the HTTP wiring's ledgers.
