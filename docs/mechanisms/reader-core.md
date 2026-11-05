> Status: CURRENT (2026-11-04) Branch `reader/s6`; reader design D1, D2, D4, D5, D7 (C++), D10 (`docs/design/reader.md`, step S6). Workstream ledger: `docs/status/reader-s5-s6.md`.

# Reader core

How `libdetent_reader` answers a lookup from the agent's mapped generation with no lock, allocation or syscall once warm, switches generations without a thread, and survives `fork`. Code: `src/detent_reader/core.cc`; public faces `include/detent/reader.h` (C ABI) and `include/detent/reader.hh` (C++); test seams `src/detent_reader/testing.hh`.

## Objects

```
detent_reader (handle)
  directory fd (trusted once)   ring[4]: Slot { 64 padded counters, atomic View * }   current (seq)
                                                       |
                         View (immutable): change *, store_seen, table?, entries[]
                                                       |            \
                         Change { ChangeMapping, dev/ino, views }    Entry { Generation *, sequence *, seen }
                                                                            |
                         Generation { AttachedGeneration (lease fd, records), PageTable, PagedReadState, views }
```

- A **Generation** is one attached table entry: the lease descriptor holding a shared `flock`, the checked records from `PreparedReader::attach` (which takes the store's directory descriptor, so attach never repeats directory trust), a `PageTable` (D3's segment table and verified bitmap, plain ownership: destruction unmaps) and the `PagedReadState` a lookup walks.
- A **Change** is one read-only mapping of the store's `change` file (`docs/mechanisms/change-file.md`), a member of every view built over it.
- A **View** is what a lookup reads: the store word and table it was built from and its attached entries. Generations and changes carry a count of the views holding them; only the holder of `refreshing` changes those counts, so they are plain integers.
- Today's store has one table entry, the selected generation, at `change` slot 0, serving every namespace. A view's `find` returns that entry for any namespace. Namespace delivery's table (its S7) brings an entry per namespace; `find` becomes a binary search by name.

## A lookup

1. Validate `ns:key` (split at the first colon, each part 1-198 name characters, 200 together) and the handle's namespace list (`DETENT_NOT_OPENED`). No handle state is touched for a refused name.
2. Pin (D5): load `current` (acquire), add 1 to this lookup's stripe of slot `current % 4` (seq_cst), reload `current` (seq_cst); a different value undoes the increment and retries. The stripe is a hash of a stack address (`>> 12`, multiplicative hash, 64 stripes), computed once per lookup.
3. Find the entry. Load its sequence (acquire) through the view's slot pointer. Equal to the entry's `seen`: walk the generation, evaluate (`detent::local::evaluate`), unpin. That is the warm path: two read-modify-writes on the stripe's line, two loads of `current`, one load of the sequence, the trie walk, and no clock, lock, allocation or syscall.
4. A moved sequence: unless a failure is remembered at this store word or the refusal backoff is running, try `refreshing` (compare-and-swap). The winner unpins, refreshes, and retries on the new view. Everyone else answers from the installed view without waiting.
5. No entry (first lookup in the namespace): an unchanged store word with no table answers `DETENT_UNAVAILABLE`. Otherwise the lookup takes `refreshing` and attaches; a loser waits on `refreshing` (C++20 atomic wait) and retries, so no lookup answers a real knob with its default because it lost the race (D2).

Lookups write the caller's fallback for every status but `DETENT_OK`; `detent_read` has no fallback and writes a zeroed `detent_value`.

## A refresh (only the holder of `refreshing`)

1. **Next slot first.** If slot `(current + 1) % 4` holds a view whose counters do not sum to zero (seq_cst loads), the publish is refused before anything is built: `refused` counts it and a backoff starts (1 ms doubling to 200 ms, reset by a successful publish). Otherwise the old view there is freed.
2. **Store.** With no `change` mapped yet (Q6), the open is retried at most once per 200 ms: directory trust (`open_publisher_directory`) the first time, then `change`. `detent_refresh` also re-stats `change` and remaps a replaced file (Limit 2).
3. **Load before read (D4 rule 1).** Load the store word and the entry's sequence, then read `selected`. A slot past the mapping is remapped here.
4. **Build.** Keep the installed entry when the table's generation id is unchanged; otherwise attach (A4: a regression within one incarnation and a storage identity change are refused). Attach reads `selected` again and takes the generation's lease; a record whose file a publish unlinked between the open and the check (`selected` replaced by rename) is opened again, up to three times, then reported unavailable. Nothing new: no publish.
5. **Publish.** Count the new view's members, store the view in the next slot (release), store `current + 1` (seq_cst), then free every other slot whose counters sum to zero.

A failure frees what it built, keeps the installed view and remembers the status at the store word the attempt read; lookups answer from the installed view (or return that status for an unattached namespace) until the store word moves or `detent_refresh` runs. A publish that lands during the attempt has already moved the word, so the lookup retries at once.

The Dekker pair: a reader's seq_cst increment and recheck against the publisher's seq_cst `current` store and counter scan. Either the recheck sees the new sequence and the reader backs off without reading the view, or the scan sees the increment and keeps the view. Sequences never repeat, so a reader that pinned a reused slot fails its recheck.

## Open and close

`detent_open` copies the store path and namespace list, takes `refreshing` itself and runs one refresh that attaches nothing: directory trust, `change`, the table. A store with no directory or no `change` yet opens (Q6 (a)); a `selected` without a valid `change` is refused (`DETENT_SOURCE_FAILURE`, D4 rule 5); an untrusted directory is `DETENT_UNTRUSTED`. `detent_close` runs alone (D1 item 7): it unregisters the handle and frees every slot. The core starts no thread and does not use a `RetirementDomain`.

## Fork (D10)

The first open registers `pthread_atfork` handlers. Live handles sit on a list under one mutex that open and close take. Prepare takes the mutex, then every handle's `refreshing` (waiting out any refresh in progress). Parent releases each and the mutex. Child zeroes every counter of every slot of every handle (its one thread is not inside a lookup, D1 item 8), releases each `refreshing` and the mutex. The child's lease descriptors share the parent's open file descriptions.

## Status mapping

`GenerationUnavailable` is `DETENT_UNAVAILABLE`; `bad_alloc` and ledger pressure `DETENT_NO_MEMORY`; `system_error` `DETENT_IO`; a refused directory `DETENT_UNTRUSTED`; everything else from a record, page or `change` check `DETENT_SOURCE_FAILURE`.

## Test seams

`PreparedReaderTesting` provides a thread-local hook after a record open and before its trust check. The replacement regression publishes revision 2 in that window while opening revision 1, then requires the C reader to answer value 2 and revision 2. A check that rejects the now-unlinked descriptor without retrying fails this test.

Built only into `detent_reader_hooked` (`DETENT_READER_TEST_HOOKS`): park windows `BeforePin`, `Pinned`, `Refreshing`, `AfterFree`, `Attaching`, and per-handle stats (live views, publishes, refused, attached, generations, `current`).
