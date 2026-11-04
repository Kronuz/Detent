---
title: "How It Works"
---

A knob change starts at a producer, lands in the authority, travels through a
relay to one agent per host, and ends as immutable files on that host's disk.
Applications map those files and read knobs straight from them, with no request
to anyone.

This page follows that path in seven steps: the actors, the store on disk, the
trie inside it, reading, updating, seeding from an image, and restart. Each step
says what runs on `main` today and marks what is designed but not built.

---

## What Exists Today

| Piece | State | Where it is recorded |
| --- | --- | --- |
| Authority, relay, managed agent | On `main` | [ACTORS.md](https://github.com/Kronuz/Detent/blob/main/docs/ACTORS.md) |
| Paged store: packs, `.read`, `.live`, `selected`, `change`, `storage.meta` | On `main`; one generation holds every namespace, so `change` has one slot | [change-file.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/change-file.md), [trusted-restart.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/trusted-restart.md) |
| Trusted restart, first-touch checks, the paced scrub | On `main` (namespace step S3) | [status/namespace-s3.md](https://github.com/Kronuz/Detent/blob/main/docs/status/namespace-s3.md) |
| Damaged-pack repair | In progress (step S3b) | [status/namespace-s3.md](https://github.com/Kronuz/Detent/blob/main/docs/status/namespace-s3.md), "Blocked on" |
| `libdetent_reader`, the lock-free reader core | Built on branch `reader/s6`, not on `main` (reader step S6) | [design/reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md), [status/reader-s5-s6.md](https://github.com/Kronuz/Detent/blob/main/docs/status/reader-s5-s6.md) |
| A `selected` table with one entry and one `change` slot per namespace | Designed (namespace delivery D8, step S7) | [design/namespace-delivery.md](https://github.com/Kronuz/Detent/blob/main/docs/design/namespace-delivery.md) |
| Compacted blocks of history | Designed (namespace delivery D6, `Detent:814b605`), not built | [design/namespace-delivery.md](https://github.com/Kronuz/Detent/blob/main/docs/design/namespace-delivery.md) |
| `detent-image` and seeded stores | Designed (namespace delivery D9, step S9), not built | [design/namespace-delivery.md](https://github.com/Kronuz/Detent/blob/main/docs/design/namespace-delivery.md) |
| Removal of the unmanaged agent and its shared-memory copy | Built on branch `legacy/wave2-g` (legacy wave 2, step G), not on `main` | [status/legacy-wave2.md](https://github.com/Kronuz/Detent/blob/main/docs/status/legacy-wave2.md) |
| Removal of the Client's file and poll sources | In progress (legacy wave 2, step H) | [status/legacy-wave2.md](https://github.com/Kronuz/Detent/blob/main/docs/status/legacy-wave2.md) |

---

## The Actors

```d2 alt="The authority and relay upstream; on each host, the agent writes the store and applications read it"
grid-rows: 2
vertical-gap: 80

upstream: "Upstream" {
  label.near: top-left
  direction: left
  authority: "Authority\ndetentd, Raft voters" {style.multiple: true}
  relay: "Relay\ndetent-relay"
  authority -> relay: "subscribe"
}
host: "One host" {
  label.near: top-right
  direction: right
  app: "Application\nlibdetent_reader"
  agent: "detent-agent\n(managed)"
  store: "Store directory" {shape: cylinder}
  agent -> store: "writes"
  store -> app: "mapped, read"
}

upstream.relay -> host.agent
```

**The authority** decides what the knobs are. It is `detentd --authority
authority.json`, one process per voter: a single voter for development, or a
fixed set of 3, 5 or 7 that replicate a Raft log and answer a producer once a
majority has the entry on disk
([ACTORS.md](https://github.com/Kronuz/Detent/blob/main/docs/ACTORS.md)). It
keeps the last 4,096 transactions, up to 16 MiB, in memory to serve deltas
(`src/detent_authority/change_history.hh:8`).

**The relay** is optional fan-out. `detent-relay` takes one subscription per
configured bundle from the authority and serves many agents from its own durable
copy, so the authority sees one subscriber per bundle instead of one per host.
Each bundle keeps 64 recent updates (64 MiB) as exact deltas
(`include/detent_relay/configuration.hh:8-9`).

**The agent** is one managed `detent-agent` per host, started with `--managed
managed.json --generation-dir DIR`. It is the only writer of its store directory
and holds `owner.lock` exclusively for the store's whole life
([change-file.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/change-file.md)).

**Applications** read knobs from that directory through `libdetent_reader`, one
reader core with a C ABI; in the design, C++, Rust and Python wrap it and
`detent get` calls it
([reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md)
D1). Until reader step S6 lands, applications read through `detent::Client` with
`make_local_read_source` (`include/detent_local/client_source.hh:31`), over the
same files.

**Legacy wave 2 removes the unmanaged agent.** It kept one `detent::Client` in
its own memory, polled a file or the classic HTTP distributor, and copied the
whole snapshot into a shared-memory region capped at 4 MiB
(`src/detent_agent/agent.hh`, `src/detent_agent/shm_format.hh:80`). That is a
second copy of data the managed store already gives every process through file
mappings, it had no path to authority data, and the classic distributor it
polled is already removed. The owner ruled to drop it (decision 7 of the wave 2
ledger on `legacy/wave2-g`). Step G deletes it on branch `legacy/wave2-g`; until
that lands, `main` still builds it.

---

## The Store on Disk

```d2 alt="The store directory: selected names a generation, whose files reference packs; change and storage.meta beside them"
dir: "Store directory" {
  label.near: top-left
  grid-rows: 2
  grid-columns: 3
  horizontal-gap: 110
  vertical-gap: 60
  selected: "selected\ncurrent generation"
  gen: "generation-<id>\n.image .read .live"
  packs: "segment-<id>.pack\npages, up to 1 MiB" {style.multiple: true}
  change: "change\nsequence words"
  meta: "storage.meta\nhost identity"
  selected -> gen
  gen -> packs
}
```

The store is one directory. A *generation* is one complete, immutable copy of
the knob state, made of a few small records and the packs they reference.

| File | What it holds | Size |
| --- | --- | --- |
| `segment-<id>.pack` | A segment: pages packed end to end | At most 1 MiB (`include/detent_local/paged_layout.hh:7`) |
| `segment-<id>.owner` | The segment's owner record: storage identity, id, length, SHA-256 | 128 B |
| `generation-<id>.image` | The root of the trie | 320 B |
| `generation-<id>.read` | Reader metadata: exact counts, bound to the storage, the generation and the root checksum, with a trailing SHA-256 | 192 B |
| `generation-<id>.live` | Per segment, the live page and byte counts; the segment list; the override histogram; a trailing SHA-256 | 1,152 B plus 72 B per segment plus 32 B |
| `generation-<id>.lease` | The generation's ownership record | 128 B |
| `selected` | Which generation is current: its id, root length and SHA-256, cursor and profile | 176 B |
| `change` | Sequence words that tell readers `selected` moved | 8 B per word |
| `storage.meta` | The store's host identity | 64 B |
| `owner.lock` | The agent's exclusive lock | Empty |

Sizes come from `include/detent_local/ownership.hh:8-9`,
[LOCAL_IMAGE_FORMAT.md](https://github.com/Kronuz/Detent/blob/main/docs/LOCAL_IMAGE_FORMAT.md)
and
[trusted-restart.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/trusted-restart.md).

**Files are immutable.** A new generation is new files: new packs for the pages
that changed, a new root, new `.read`, `.live` and `.lease`. Packs that did not
change are not copied; the new generation references them where they are. Two
names are the exception: `selected`, replaced by an atomic rename, and `change`,
whose words are incremented in place and which only grows. The collector removes
an old generation's files once no selected, staged or reader-held generation
reaches them.

**The `change` file** is an array of 8-byte words. Word 0 is the store word,
incremented after every replacement of `selected`. Word 1 + *slot* is one table
entry's sequence, incremented after every replacement that changes that entry.
Today the store has one entry, the generation that holds every namespace, so the
file is 16 bytes
([change-file.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/change-file.md)).
A slot per namespace comes with the namespace table (D8, step S7).

**`.read` and `.live`** are what make a restart cheap. `.read` carries the
counts a reader needs before it touches a page. `.live` carries what a full walk
of the pages would otherwise compute: which segments are live and how full, and
the override histogram. Both are written and synced with the generation's other
files before `selected` names it.

---

## The Trie

Inside a generation, knobs live in a **canonical, path-compressed byte-radix
trie**: radix 256, one child per possible next byte, with runs of shared bytes
compressed into a prefix on one page
([INCREMENTAL_PUBLICATION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md),
"Format and logical identity"). Canonical means the same knobs always make the
same trie, whatever their update history.

**Keys.** Two kinds, both terminated by NUL so no key is a prefix of another
(`src/detent_local/paged_layout.cc:83-109`):

- A namespace watermark: `W namespace NUL`.
- A definition: `K namespace NUL local-name NUL`.

```d2 alt="Root, branch pages and definition pages, spread across two segments"
direction: right

# Each page shows its kind and its compressed prefix; edge labels are the child label bytes.
root: "Root\n320 B"
segA: "segment A" {
  label.near: top-left
  head: "Branch\n(no prefix)"
  w: "Namespace\npayments NUL"
  ns: "Branch\npayments NUL"
}
segB: "segment B" {
  label.near: top-left
  d1: "Definition\new_checkout NUL"
  d2: "Definition\netry_limit NUL"
}

root -> segA.head
segA.head -> segA.ns: "K"
segA.head -> segA.w: "W"
segA.ns -> segB.d1: "n"
segA.ns -> segB.d2: "r"
```

Each box is one page: its kind and its compressed prefix. The letter on each
arrow is the child's label byte. Looking up `payments:new_checkout` walks the
key `K payments NUL new_checkout NUL`: the root branch has no prefix, label `K`
leads to a branch with prefix `payments NUL`, label `n` leads to a definition
page with prefix `ew_checkout NUL`, and the key ends there. Pages of one
generation can sit in different segments; a reference names the segment.

**One node per page.** A page is (`include/detent_local/paged_layout.hh`):

| Part | Size | What it holds |
| --- | --- | --- |
| Header | 80 B | Kind, length, prefix length, child count, body length, key count, logical digest |
| Prefix | At most 512 B | The compressed key bytes this page consumes |
| Children | 96 B each, up to 256, sorted by label | A one-byte label, seven zero bytes, and an 88-byte reference |
| Body | For a definition, at most 33,816 B | A 24-byte header (revision, type, default, optional rollout, override count), then the overrides |

A **reference** is 88 bytes: the segment id (16), the page's offset (4) and
length (4), its physical SHA-256 (32) and its logical SHA-256 (32). A page is at
most 64 KiB and at least 80 B, starts on an 8-byte boundary, and fits inside one
segment (`src/detent_local/paged_layout.cc:27-31`). The 320-byte root holds the
cursor, the profile, the totals and the reference to the first page.

**Kinds.** A *Branch* has at least two children and no body. A *Definition* has
one key and a body. A *Namespace* page holds the 8-byte watermark, the
namespace's revision. *Overflow* is reserved; the format does not use it
(`src/detent_local/paged_layout.cc:41-53`). The largest definition, 256
overrides with 128-byte keys, is 33,816 bytes and fits one page.

**Two digests per page.** The *physical* digest is SHA-256 of the page's exact
bytes. The *logical* digest is SHA-256 of what the page means: its kind, its
prefix, each child's label and logical digest, and its body, but not where any
child is stored (`src/detent_local/paged_layout.cc:130-169`). That makes the
trie a Merkle tree over content: the same knobs have the same logical hashes
however the pages are packed, so moving pages between segments changes no
logical hash, while the physical digest still binds every byte.

**Updates are copy-on-write.** Changing a knob writes a new definition page, a
new copy of every branch on the path from the root to it, and a new root. Every
other subtree is reused by reference. Deleting a key removes its page, and a
branch left with one child collapses into it, so the trie stays canonical.

**A lookup** starts at the root's reference, checks the page, matches the page's
prefix against the next bytes of the key, binary-searches the children for the
next key byte, follows that reference, and repeats until it reaches the
definition page (`src/detent_local/paged_read_state.cc:38-110`). It also reads
the namespace page and refuses a definition whose revision is above the
namespace's watermark. Then it evaluates the definition.

---

## Reading

**File mappings.** A file mapping and a shared-memory segment end the same way:
physical pages of memory that several processes see at once. A shared-memory
segment is memory with no file behind it, so a writer has to copy the knobs into
it, and every reader sees that copy. A file mapping shows a process the file's
own pages from the kernel's page cache. The agent already wrote the knobs to its
files, so mapping those files gives every application the same bytes with no
second copy. Packs are mapped read-only
(`src/detent_local/segment_mapping.cc:54`).

```d2 alt="Open, first lookup, first touch, then warm lookups; a moved sequence sends a lookup back to attach"
direction: right

open: "Open\ndirectory trust,\nchange, selected"
attach: "First lookup\nattaches namespace"
touch: "First touch\nmaps segment,\nchecks page"
warm: "Warm lookup\none load,\nbit tests"

open -> attach -> touch -> warm
warm -> attach: "sequence moved" {style.stroke-dash: 4}
```

The reader core works in four steps
([reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md)
D2-D5). It is built on branch `reader/s6`, not on `main`.

**Open.** `detent_open` checks that the store directory is trusted (owner, mode,
ancestry) once, opens and checks `change`, reads `selected` and attaches nothing
yet. The caller names the publisher's uid; trust is never inferred from the
directory.

**First lookup in a namespace.** It attaches that namespace: opens its `.read`
record and lease, checks them, and publishes a view that includes it. Attach
reuses the store's directory descriptor, so it never repeats directory trust.

**First touch.** The first lookup that reaches a segment opens and maps it, with
the same ownership and mode checks. The first lookup that reaches a page checks
its physical and logical digests against the parent's reference and sets that
page's bit in the segment's bitmap, one bit per 80 bytes. A page is checked once
per mapping and never again.

**Warm lookup.** One acquire load of the namespace's sequence word in `change`,
compared with the sequence the view was built from, and an increment on one of
64 per-stripe counters that pins the view. Then the walk: per page, one bit test
and the bytes. No syscall, no lock and no allocation
([reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md)
D3, D5).

**Generation switch.** When the sequence word moved, one lookup wins a flag,
rereads `selected`, re-attaches only the namespaces whose generation changed,
and publishes a new view; every other thread keeps reading the old view without
waiting. A segment kept by the new generation keeps its mapping and its verified
bits. Old views retire through a ring of four slots with per-stripe reader
counters; the last reader out lets the one freer unmap them
([reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md)
D4, D5).

**Today on `main`.** `detent::Client` with `make_local_read_source` reads the
same files. Page access already uses the lock-free segment table and
verified-page bitmap
([verified-page-bitmap.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/verified-page-bitmap.md)),
but a background thread polls `selected` every 200 ms
(`include/detent_local/client_source.hh:10`) instead of the one sequence load,
and one generation holds every namespace, so a switch re-attaches the whole
store.

---

## Updating

```d2 alt="A new root and new branch reuse the unchanged subtree and definition; only the changed path is new"
grid-columns: 3
horizontal-gap: 140
vertical-gap: 90

old: "old root"
gap: "" {style.opacity: 0}
new: "new root"
ob: "old branch"
x: "unchanged\nsubtree"
nb: "new branch"
od: "old definition"
y: "unchanged\ndefinition"
nd: "new definition"

old -> ob
old -> x
ob -> od
ob -> y
new -> x {style.stroke-dash: 4}
new -> nb
nb -> y {style.stroke-dash: 4}
nb -> nd
```

**What arrives.** The agent subscribes upstream with its current position and
receives either a delta (the changes after its position) or a full snapshot.
Today a delta needs the agent's position inside the upstream's in-memory window:
the authority's last 4,096 transactions, or the relay's 64 recent updates per
bundle. Below that window, the agent gets the full snapshot.

**What the agent writes.** From a delta and the generation it already has, the
agent builds new pages only along the changed paths: new definition pages, the
branches above them and a new root. Untouched references pass through without
visiting what is under them
([INCREMENTAL_PUBLICATION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md),
"Changed-path structural overlay"). The new pages go into new packs; every
unchanged pack is reused by reference. Validation of the result checks the new
pages only.

**How it publishes.** The new packs, their owner records and the generation's
records are written and synced first. Then the agent replaces `selected` with a
rename, syncs the directory, and only then bumps `change`: the entry's sequence,
then the store word
([change-file.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/change-file.md)).
A reader sees the whole old generation or the whole new one. A crash between the
rename and the bump hides nothing, because every agent start bumps every word.

**Compaction.** Reuse leaves packs with dead pages over time. Every
`compaction_interval` (60 s by default,
`include/detent_delivery/coordinator.hh:31`) the agent checks for sparse packs
and rewrites them into new ones. Pages move and their logical digests stay the
same, so this is local maintenance and sends nothing upstream.

**A client pays only for what it is missing.** This is the owner's rule for
catch-up (namespace delivery P1): an agent with a store of any age gets the
changes its store lacks, never the whole dataset, as long as those changes cost
less than a fresh copy. The design answers an agent at position R from the first
of three tiers that reaches it (D6):

| Tier | When | What the agent gets |
| --- | --- | --- |
| Ring | R + 1 is still in the upstream's in-memory window | The exact entries after R |
| Compacted blocks | R is older than the ring but newer than the cut | One block holding each changed key's last value since R, with tombstones for deletes, then the newer blocks and the ring |
| Full copy | R is older than the cut, or the agent has no store | The namespace's state, then the ring |

The cut sits where serving history would cost more than the full copy.
**Compacted blocks are designed, not built** (`Detent:814b605`); today only the
ring and the full snapshot exist.

---

## Seeding from an Image

:::hint[Designed, not built]{.construction}
Namespace delivery D9 designs seeding and step S9 builds it; none of this
section runs yet.
:::

```d2 alt="The agent adopts the read-only image layer, writes only its own small files, and fetches (R, h] upstream"
direction: right

up: "Relay or\nauthority"
agent: "detent-agent\nin a container"
image: "Read-only image layer" {
  label.near: top-left
  files: "selected, change,\ngenerations, packs" {style.multiple: true}
}
own: "Agent's own files" {
  label.near: top-left
  new: "storage.meta, .lease,\nnew generations"
}

up -> agent: "(R, h]"
agent -> image.files: "adopts\nread-only"
agent -> own.new: "writes"
```

`detent-image build --source managed.json --out DIR` asks upstream for each
namespace's full copy and writes the store: the generation files and packs,
`selected` and `change`, but no `storage.meta` and no `.lease`. It runs the
agent's own full-copy build, so its files are byte-identical to the files an
agent would write from the same copy.

The image ships those files in a read-only layer at the agent's
`generation_dir`. The agent **adopts** them read-only: it opens every seeded
file `O_RDONLY`, so a container never copies the store up into its own layer,
and containers on one host share the same page-cache pages. What the agent
writes is small or new: `storage.meta`, a `.lease` per seeded generation, its
own `selected` by rename, new generations, and `change`, the one seeded file it
writes (8 bytes per namespace plus 8, copied up once).

After adoption the agent asks each namespace for **(R, h]**: the changes after
the image's position R up to the upstream's head h. A week-old image costs the
changes made since it was built, as long as they cost less than a full copy.

**`managed.json` is connection config, not data.** It names the authority or
relay address, the identities, the principal, the namespaces, the token and the
CA ([USAGE.md](https://github.com/Kronuz/Detent/blob/main/docs/USAGE.md),
"Managed authority delivery"). It tells `detent-image` and the agent where to
fetch; it holds no knobs, and readers never open it.

An image owned by another account is not adopted, and the agent reports
`seed_owner_mismatch` in health. Foreign files in the agent's own directory are
never opened; the agent bootstraps a fresh store beside them. A foreign
directory leaves the agent not ready.

---

## Restart and Integrity

```d2 alt="Restart hashes no pack; first touch and the scrub find damage; repair is S3b"
direction: right

ready: "Restart\nno pack hashed"
touch: "First touch\nchecks a page"
scrub: "Scrub\nhashes packs"
damage: "Damage\nreported"
repair: "Repair\n(S3b)" {style.stroke-dash: 4}

ready -> touch
ready -> scrub
touch -> damage
scrub -> damage
damage -> repair {style.stroke-dash: 4}
```

**A generation is verified once, by the build that writes it**
([trusted-restart.md](https://github.com/Kronuz/Detent/blob/main/docs/mechanisms/trusted-restart.md)).
After that, nothing rehashes it wholesale.

**Trusted restart.** A cold start reads `selected`, the lease, `storage.meta`,
the 320-byte root (hashed against `selected`), `.read` and `.live` (each against
its own digest and its bindings), and per segment its `.owner` record and a
`stat`. It maps every pack and reads none of them. A test counter proves it:
zero pack bytes hashed at startup (`test_testing_trusted_restart_oracle`). A
store whose `.live` or `.read` is missing or wrong is refused, and the operator
bootstraps a fresh one.

**First touch.** Every page the agent reads from a trusted mapping is checked
against its parent's reference the first time any read reaches it, with the same
bitmap readers use. A damaged page fails that read with `SourceFailure`; it
never returns a wrong value.

**The scrub.** A task on the agent's owner loop hashes whole packs against their
owner records, paced at `scrub_bytes_per_second`: 524,288 by default, 0 disables
it
([LOCAL_IMAGE_FORMAT.md](https://github.com/Kronuz/Detent/blob/main/docs/LOCAL_IMAGE_FORMAT.md)).
It catches damage on pages nobody reads, and in the padding between pages, which
no page digest covers. A damaged pack shows up in the coordinator status, as a
warning in the agent's log, and in the relay's `/v1/health`
(`scrub_damaged_packs`, `scrub_source_failure`).

**Repair is in progress** (step S3b). The design fails only the damaged
namespace, asks upstream for a full copy, publishes a fresh generation under new
names that share no damaged pack, and lets the collector remove the damaged
packs (D10.3). Today, reads of damaged pages fail, the scrub reports the pack,
and the operator bootstraps a fresh store.

**Limits.** A page checked before it was damaged stays checked for the life of
its mapping. Trust ends at the store's own files: an account that can write the
store can forge them.

---

## Where to Read More

- [ACTORS.md](https://github.com/Kronuz/Detent/blob/main/docs/ACTORS.md): what, how, when and where for every actor.
- [design/reader.md](https://github.com/Kronuz/Detent/blob/main/docs/design/reader.md): the reader core.
- [design/namespace-delivery.md](https://github.com/Kronuz/Detent/blob/main/docs/design/namespace-delivery.md): per-namespace positions, compacted blocks, the store table, seeding and restart.
- [INCREMENTAL_PUBLICATION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md): the paged format and copy-on-write publication.
- [mechanisms/](https://github.com/Kronuz/Detent/tree/main/docs/mechanisms): one note per landed mechanism, including `change-file.md`, `trusted-restart.md` and `verified-page-bitmap.md`.
