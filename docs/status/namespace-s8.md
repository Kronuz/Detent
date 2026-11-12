# Namespace S8: the relay mirror

S8 of [namespace delivery](../design/namespace-delivery.md) (D7, W8). A relay keeps, per namespace it serves, the upstream's base, blocks above level 0, level-0 sections and cached full copy, byte for byte, and answers W6 from its own disk after a restart. The ring stays in memory, mirrored from the session stream (S6). Code: `include/detent_relay/mirror.hh`, `src/detent_relay/mirror.cc` (target `detent::relay_mirror`).

## What the mirror is

The mirror computes no layout and parses no file. It adopts the upstream's layout, a `NamespaceLayout` per namespace: the incarnation, the upstream seal and ordinal, the policy in force (the upstream's bytes), and the files, each with its kind, the positions it covers, its size and SHA-256:

```text
base [0, p] | block [p+1, b1] ... | section [.., ..] ... (to s) | copy [0, s], optional
```

`check_layout` refuses anything else: a base first, blocks then sections contiguous from p + 1 to s, at most one copy, at s and last, names unique. The mirror stores each upstream name under `m-` and 32 hex digits of its SHA-256 (`mirror_file_name`), so a namespace name with `/` is one flat entry.

## Directory

```text
mirror/                one directory, the mirror's alone
  mirror               the table, replaced by rename
  owner.lock           held exclusively by the one writer
  m-<hex>              one file per upstream name the table reaches
  .tmp-<random>        a file being written; startup removes it
```

`mirror` is a 32-byte header (magic `DTMIRR01`, layout 1, entry count, sequence), the entries sorted by name (name, incarnation, seal, ordinal, policy, then each file's kind, first, last, size, digest and upstream name), then the SHA-256 of everything before it. A table that does not decode refuses the mirror.

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

## Answers

`plan_answer(layout, incarnation, R)` picks D6's tier: `Ring` at or above s (the session stream's entries), `Blocks` for p ≤ R < s (the file holding R + 1, trimmed above R, then every younger file whole, sections merged at answer time), `Copy` below the cut or for another incarnation (the cached copy, or every file merged without tombstones). It names files and the filter; S5.2's codec trims and merges. When S6 lands its frame selection over a history interface, the mirror implements that interface and `plan_answer` goes.

## Evidence

| Criterion | Test | What it shows |
| --- | --- | --- |
| A1 (relay half) | `test_relay_mirror` `chain` | An authority and three relays in a chain, each fetching from the one before through its own reads: every file byte-identical at every hop over 24 upstream steps, and no relay reaches past its upstream |
| A2 | `test_relay_mirror` `restart` | After 20 steps, every answer for R at or above the cut (and every answer) is the same after a restart with the upstream unreachable; adopting the same layout fetches 0 bytes; the next seal fetches exactly the new names' bytes |
| A3 | `test_relay_mirror_crash` | T7 over completion's sync seam: a power cut after every primitive of a first adoption, a second (reuse, fetch, cut, squash, drop, policy change, collector) and a repair, then a restart that adopts again, cut in turn; a kill after every primitive, then a restart cut in turn |
| A4 | `test_relay_mirror` `follows` | Over 24 steps with seals, roll-ups, squashes, cuts, a policy change, a cached copy, a new incarnation, a drop and a new namespace: the table equals the upstream's layout, the directory holds exactly the reached names with the upstream's bytes, the bytes fetched are exactly the new names', and a store below the new cut gets the copy |

## Left open

1. A1's voter half and the wiring into the relay service: no voter writes blocks until S5.2 (`namespace-blocks`); the upstream's layout and file-by-name endpoints come with S5.2 and S6.
2. A5 (W8, D84): the relay reads W5/W6 from the authority or an upstream relay; built against S6's types when S6 lands.
3. A fetch returns one file's bytes whole; a base of a large namespace wants a streamed fetch into the `.tmp-` file.
