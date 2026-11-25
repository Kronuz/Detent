# First reads below 1 ms (quest first-read-1ms)

Where a fresh process's first read through the reader core spends its time, what this quest
removed, and what is left. All numbers come from an Intel Core i9-9980HK running macOS 15 (Darwin
24.6), which has no SHA-NI, so SHA-256 runs the sha256 library's portable kernel. The host ran
loaded (load 30-90, a 68% CPU speed limit) during this work: absolute times are about twice an
exclusive hold's, and the shares are the finding.

## Profile

`first-read-gate.sh profile-prepare OUT` builds `first_read_core` with wall-clock marks that
`first-read-probe.py` inserts into the core, attach and page table; `first-read-gate.sh profile
OUT stopped running` runs each cell as the gate does and prints each step's median and p90. Job
`261124-134303-81190d` (strict, main's reader code), medians in us:

| Step | 1 namespace | 1,000 namespaces |
|---|---|---|
| Total | 1,361 | 3,082 |
| Directory trust walk (12 components) | 567 | 534 |
| `selected` read, SHA-256 and decode | 96 | 1,837 |
| Attach: selection, lease, image, `.read` | 236 | 330 |
| First-touch pages (SHA-256 and node checks) | 141 | 11 |
| Segment open and map | 103 | 110 |
| `change` open and map | 76 | 75 |

Warm micro costs on the same host (strict): a directory `openat` for reading 9.7 us, for search
(`O_SEARCH`) 1.6 us; `fstatat` 4.8, `fstat` 1.2, an ACL read 3.1, a file `openat` and `close`
18.7; SHA-256 of the 141 KB 1,000-entry table 1,154 us (CommonCrypto 643-780 us).

## Changes

Each was screened with the gate's `measure` against main (strict beside light holds, six pairs of
200 reads, publisher stopped), as the median paired difference with its noise floor:

| Change | 1 namespace | 1,000 namespaces |
|---|---|---|
| The trust walk opens directories for search on macOS (`Detent:5d879068`) | -243 us (71) | -214 us (121) |
| With duplicate slots found by sorting (`Detent:e7dc35d6`) and one image read per attach (held) | -317 us (96) | -527 us (162) |

The trust walk opens each component before any `fstatat` (screened -333 and -348 us with the
first change, `Detent:dee58172`), and attach takes the image bytes the selection read
(`Detent:9c8a44ee`).

## Warm lookups

`benchmarks/reader_bench_ab.sh` runs `reader_bench` (100K knobs, int, core) interleaved against
main. Two changes, together, median p50 ns (strict beside light holds, 6 pairs):

| Case | Threads | main | head |
|---|---|---|---|
| hot | 1 | 737.5 | 592.5 |
| hot | 8 | 797.5 | 651.8 |
| scattered | 1 | 1,198 | 989 |
| scattered | 8 | 1,268.8 | 1,068 |

1. `Detent:182bb900` inlines `read_integer`, `decode_node` and `NodeView::slot` (9.5% and 8% of a
   hot lookup by `sample`, each an out-of-line call).
2. `Detent:d20bbadb` checks a matching leaf's key kind on the looked-up key in place, instead of
   zero-filling and copying a 202-byte key at every leaf.

## sha256 main (AVX2 kernel)

Detent pins sha256 at `sha256:f24692f`, before its AVX2 kernel (`sha256:07cdc83`). The branch built
against sha256 main (`FETCHCONTENT_SOURCE_DIR_SHA256`) and against the pin, strict screen, six
pairs of 200 reads, publisher stopped, medians in us:

| Cell | pin | sha256 main | paired diff (floor) |
|---|---|---|---|
| 1 namespace | 653 | 651 | -19 (67) |
| 1,000 namespaces | 1,505 | 1,252 | -303 (265) |

The kernel takes about a fifth off the whole-table hash; the 1,000-namespace cell stays above 1 ms
until the table is verified per page.

## Design: table pages (layout 3)

A first read verifies only the table bytes it reads, as a first lookup verifies only the trie pages
it reaches: per-page SHA-256 digests chained to one table digest, and a verified mark per page.

Layout. `selected` keeps its magic `DTTABLE1`; `agent_store_layout` becomes 3 (a layout-2 table is
`LayoutRefused`, D43). The 72-byte header adds the page count and `slots` (the highest entry slot
plus one) to layout 2's fields. The index follows: per page, its SHA-256 and its last entry's name
(length byte, bytes). The table digest, SHA-256 of the header and index, follows the index. Then
the pages: 4,096 bytes each, the last one shorter; each holds whole entries in name order, today's
entry encoding, and zero padding to its end. A 1,000-namespace table is 35 pages and a 1.6 KB index.

What the digest commits to. The table digest covers the header and the index, so it commits to
every page through the page's digest and to the name range each page holds through the last names.
`selected` stays the store's trust root, and nothing outside it names the table digest: its
descriptor passes the owner, link, mode and ACL checks of every store file before it is mapped
("Store file checks" below).

Older layouts. A read takes the magic and the layout before any size rule, since each layout has its
own. Layouts 1 and 2 have a 64-byte header and one trailing digest of the whole table, so their
smallest table, the empty one, is 96 bytes. A whole table of either is `LayoutRefused`, the empty one
included; one shorter than 96 bytes or with a digest that differs is damage (`TableRefused`).

What a read hashes. Opening a table reads the file, checks the header and the table digest, and
decodes the index: about 2 KB hashed for 1,000 namespaces. `find(name)` binary-searches the last
names, and on the page's first touch hashes that page (4 KB), checks its digest, decodes all its
entries (order within the page, after the previous page's last name, its last name equal to the
index's, each slot below `slots`, zero padding) and marks it verified. `decode_table`, the agent's
and the store's whole read, verifies every page plus what spans pages: entry count and unique slots.

Failure. A damaged header, index or digest refuses the table at its read (`TableRefused`, today's
status for the reader core: `DETENT_SOURCE_FAILURE`). A damaged page refuses each `find` that reaches
it with the same exception and is never marked; namespaces on other pages keep reading. The agent's
whole read still fences the store on any damage.

| Event | Reads and hashes | Mark | A lookup answers |
|---|---|---|---|
| Attach (table read) | the file; header, index and table digest | all pages clear | after `find` |
| First read of an entry | its page, once | sets the page's mark (release) | the entry, or not listed |
| Another entry on a verified page | nothing (mark read with acquire) | unchanged | the entry, from the page's bytes |
| A corrupt page | that page, on each `find` reaching it | stays clear | `TableRefused`: source failure |
| Publish (a new table renamed over) | the next table read, at the `change` bump | a new table: all clear | through the new table |
| Close (last view of the table) | nothing | freed with the table | none |

The marks are the trie's `VerifiedPages`: a table page starts at a multiple of 4,096 bytes in the pages
region, an aligned start it records as it records trie page starts, accounted to the reader's
mapping ledger. A page's hash and its entry checks (bounds, order, slot, last name, padding) run once,
at its first touch; a marked page answers from the offsets that touch kept, in place and unchecked
("Verified page index" below). Two threads that first touch a page together both check it; both marks
and both offset sets agree (`test_table_pages` `concurrent_first_touch`).

Marks and the mapping rely on `selected` never changing after its rename: the agent writes a new
table only under a `.tmp-` name and renames it over `selected`, and the checks below leave only the
owner or root able to write the mapped inode. An in-place write or truncation by them is outside the
model: on macOS a private mapping kept the bytes first mapped, and on Linux a truncation raises
SIGBUS in the reader (security review N1).

### Verified page index (correctness review C2)

Before the index, a lookup on a marked page decoded every entry of the page (a `TableEntry` and a
name copy each) and scanned its padding: 4.8-12.5 us against main's 0.3-0.5 us `StoreTable::find`
(job 261124-160446-6df9d3), paid by every lookup of an unlisted namespace (`View::listed`) and once
per attached namespace on each refresh. The index keeps what the first touch verified with its mark.

Structure. The marks' state gains one array, allocated with the marks at the table read and
accounted to the same ledger: per page, its entry count and each entry's offset in the page, as
`std::atomic<std::uint16_t>` (at most 29 entries a page: `table_page_bytes / table_entry_bytes(1)`),
60 bytes a page, 2.1 KB for 1,000 namespaces. There is no second cache: the offsets live and die with
the marks.

| Event | Hashes | Offsets | Mark | A lookup answers |
|---|---|---|---|---|
| Attach (table read) | header, index | allocated zeroed, with the marks | all clear | after `find` |
| First touch of a page | the page | written (relaxed) after every check of the page passes | set (release) after the offsets | from the offsets just written |
| Lookup on a marked page | nothing | read after the mark (acquire) | unchanged | binary search of the offsets comparing names in place; the match is answered in place (`TableEntryView`) |
| A corrupt page | the page, on each reach | not written | stays clear | `TableRefused` |
| Two first touches together | both | both write the same values | the first CAS sets it | each from its own walk |
| Without an account (no marks) | the page, on each reach | a local array per reach | none | from the local array |
| Publish | the new table's header and index | a new array | all clear | through the new table |
| Close | nothing | freed with the marks, returned to the ledger | freed | none |

What a marked page does not re-check: the entry bounds, order, slot, last-name and padding checks run
once, at the first touch, like the hash; the marks already rely on the mapped table never changing
(above). `decode()` walks every page with every check. `PagedTable::check` is the one walk: it
verifies a page, fills the offsets and, with an account, keeps them and sets the mark. `find` returns
a `TableEntryView`, the entry read in place (its name a `string_view`, its fields read from the page)
or none, as main's `StoreTable::find` returns a `const TableEntry *` or null: it borrows the table and
is valid as long as it. A caller that keeps an entry copies it (`entry()`): the reader core does so
only when it attaches a new generation.

Measure. `benchmarks/table_find_bench.cc` (a verified-page `PagedTable::find` against main's
`StoreTable::find`, 1,000 entries of 16- and 40-byte names, present and absent) and `reader_bench`
warm lookups, main against the head as interleaved strict pairs (`benchmarks/reader_bench_ab.sh
find|run`). Target: main's warm `find` cost.

In the reader core, a lookup's first attach that fails, on a damaged page as for any other reason,
is remembered for the store word (D4): namespaces already attached keep answering, and a new one
reports that failure until the word moves. The table's bytes, its
index and its marks live as long as the reader core's `Table` (shared by its views).

Files outside this quest's set that the format touches: `src/detent_reader/core.cc`,
`docs/LOCAL_IMAGE_FORMAT.md`, `tests/detent_local/CMakeLists.txt`,
`tests/detent_reader/test_reader_core.cc` and `tests/detent_agent/test_agent_store.cc` (its pinned
generation name includes the store layout, and its codec checks re-sign a trailing digest). The
system and delivery tests parse `selected` in Python: `tests/detent_delivery/verify_local_format.py`,
`tests/detent_system/managed.py` and `tests/detent_system/test_restart_capacity.py`.

## Store file checks

Every file the store reads or maps is checked on the descriptor it reads, by one helper,
`generation_file_refusal` (`src/detent_local/generation_file_access.hh`): a regular file, owned by
the publisher, one link, no group or other write, the expected size, and an ACL that grants nothing
beyond the mode. Directories pass the trust walk's rule (no group or other write, deny-only ACL).

| File | Read by | Check on the read descriptor |
|---|---|---|
| `selected` | reader open and switch (`read_table`), recovery, agent open, agent startup (`slurp`) | the helper, then mapped (the agent reads it) |
| `.image` | reader attach (`select_entry`), recovery (mapped), agent startup (`verify`) | the helper |
| `.read` | reader attach (`record`), trusted restart (`open_record`), agent startup (`verify`) | the helper |
| `.lease` | reader attach, recovery | the helper |
| `.live` | trusted restart (`open_record`), agent startup (`verify`), agent collection (a held generation's) | the helper |
| a reached name a publish reuses | agent publish (`slurp`, then its digest) | the helper |
| `.owner` | recovery and trusted restart | the helper |
| `segment-*.pack` | reader first touch (`lazy_page_access`), recovery (`SegmentMapping`) | the helper, then mapped |
| `change` | reader (`ChangeMapping::open`), agent (`ChangeWriter`) | owner, link, mode and ACL; its size is its own rule |
| store directory | reader trust walk, recovery opens (`store_directory`) | no group or other write, deny-only ACL |
| packs, scrubbed | `PackScrub` | none: it hashes the bytes against the owner record and trusts nothing it reads |

The agent reads every file's bytes through one function, `AgentStore`'s `slurp`, which opens the name
under the store directory's descriptor and passes it to the helper with the agent's own uid, the
file's size bounded by the caller's limit, before it verifies the bytes against the table.
`test_reader_core` `acl_grants` grants `everyone allow write` on each kind of file and on the
directory, and checks the reader and the recovery refuse each one they read and read again once the
grant is gone. `test_agent_store` `acl_grants` does the same for the agent: a granted `.image`,
`.read` or `.live` fails its namespace at startup, a granted `selected` refuses the open, and a
granted reached name refuses its reuse (`ReachedNameMismatch`).

## What is left

The A2 gate fails a cell on either rule: a pooled branch p99 of 1 ms or more (A2), or, where main
ran, a paired difference or pooled p99 beyond main's plus their floors (D140). The running
1,000-namespace cell has the bound alone: main's test writer cannot publish that store.
