> Status: DRAFT (2026-11-03) Branch `reader/s5`; reader design D4 and namespace-delivery D8's `change` file (`docs/design/reader.md`, step S5). Workstream ledger: `docs/status/reader-s5-s6.md`.

# The `change` file

How the agent tells readers that `selected` moved, with one load per lookup and no stat. Code: `src/detent_local/change_file.{hh,cc}` (both sides), `src/detent_local/disk_store.cc` (the agent's increments).

## Shape

```
change:  word 0         word 1          word 2  ...   host-endian uint64, one 8-byte word each
         store word     slot 0 seq      slot 1 seq
```

- Word 0 is the store word: incremented after every replacement of `selected`.
- Word 1 + *slot* is a table entry's sequence: incremented after every replacement that changes that entry.
- The file grows by extension (new words read 0) and never shrinks; the agent never truncates, unlinks or replaces it.
- Today's store has one table entry, the selected generation, at slot 0, so the file is 16 bytes. Namespace delivery's table (its S7) assigns one slot per namespace; `ChangeWriter::reserve` and `publish` already take any slot set.

## Writer (the agent)

- `make_disk_store` opens `change` through the store's directory descriptor right after it takes `owner.lock`, creating it (`O_CREAT | O_EXCL | O_NOFOLLOW`, the store's `artifact_mode`) when absent, and maps it read-write shared. It refuses a file not owned by the store's owner, linked twice, group or other writable, with a non-mode ACL, or not a whole number of words; the store then does not open.
- The file exists before the startup census, so the census sees it: `change` is a known name (not quarantined as foreign), checked like `owner.lock` (owner, one link, no group or other write) plus a whole number of words, and counted in the disk ledger like every other entry. A fresh directory's census is therefore `owner.lock` plus a 16-byte `change`, and fresh bootstrap reserves one more entry and 16 more bytes.
- Startup: `ready()` runs after the startup barrier (or after a fresh store writes `storage.meta`) and calls `bump_all()`, one release `fetch_add` per sequence and then the store word, before the store reports ready. A crash between a rename and its increments therefore hides nothing (reader.md D4 rule 2, namespace-delivery D11).
- Publication: the publication's barrier phase (the directory sync after the rename of `selected`) calls `publish({0})`: the entry's sequence, then the store word, each a release `fetch_add`. The increments need no sync, since every start bumps every word.
- One writer: the store holds `owner.lock` with `flock(LOCK_EX | LOCK_NB)` for its whole life (completion's `acquire_owner`, `legacy_file` mode), so a second store on the directory fails to open. The atomic increments would keep a second writer from losing one regardless.

## Reader

`ChangeMapping::open(directory, publisher)` opens `change` `O_RDONLY | O_NOFOLLOW | O_CLOEXEC` from the trusted store directory and applies the record checks: owner `publisher`, one link, no group or other write, a mode-only ACL, a size that is a whole number of words covering word 0. It maps that size read-only and shared, so the writer's increments are visible without a remap. `sequence(slot)` is null past the mapping; the reader core remaps on that (S6). It returns `Absent` when there is no file and throws `ChangeRefused` on any failed check, which is how readers refuse a `change` the publisher does not own (reader.md D4 rule 4).

## Test seam

`ChangeTesting::install` runs a hook at the start of every `publish`. The test uses it to read `selected` at that moment and prove each increment follows the rename it announces.

## Evidence

`tests/detent_testing/test_change_file.cc` (`test_local_change_file`): word values after create, publish, grow, bump and reopen; every refusal on both sides; a live reader mapping seeing increments; through the test generation writer, `{1,1}` after a fresh start, `{r+1, r+1}` after publication r, the hook seeing `selected` at revision r when publication r bumps, a restart bump, `owner.lock` held exclusively, an unsafe file refusing the store, and a store without `change` gaining one.
