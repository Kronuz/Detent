> Status: CURRENT (2026-11-03) how `detentd::Distributor` publishes an accepted snapshot and serves a consistent GET, read from `src/detentd/detentd.cc` and `detentd.hh` on `chore/salvage`.

# Distributor publication

A `Distributor` serves one published snapshot: the parsed form, its JSON bytes, the ETag, the
gzip and zstd forms, and the cached diff from the previous version. PUT and PATCH replace all of
it at once; GET reads one consistent copy.

## PublishedState

`PublishedState` (`detentd.hh`) holds the snapshot, `version`, `has_snapshot`, `bytes`, `etag`,
`gzip`, `zstd`, and the one-step diff (`has_diff`, `diff_base_version`, `diff_bytes`, `diff_gzip`,
`diff_zstd`). `published_` is a `unique_ptr<PublishedState>` guarded by `snapshot_mutex_`.
`prepare_publication` builds a state from a snapshot and its bytes, computing the ETag and the
compressed forms. Startup (`load_persisted`) uses the same function, so the persisted bytes are
served exactly as written.

## Commit (`resolve_and_commit`)

Under the exclusive lock:

1. `resolve` produces the new snapshot from `published_->snapshot` (PUT returns it, PATCH merges
   onto it). A version at or below `published_->version` is rejected.
2. The new state is prepared: bytes, ETag, compressed forms.
3. If a snapshot is already published, the diff from it to the new one is serialized into the
   new state and pushed onto `diff_history_` (`history_staged`). `composed_diff_cache_.clear()`
   advances the cache generation, which never rewinds, even if the attempt fails.
4. `persist_locked` writes `<path>.tmp` with stream exceptions on, flushes and closes it, then
   renames over the target. Any refusal throws.
5. A throw from steps 2 to 4 pops the staged history entry (`diff_history_.pop_back()`) and
   leaves `published_` unchanged. Otherwise `install_publication` swaps the pointer (`noexcept`),
   then `diff_history_` is trimmed to `max_diff_history_`.

No file or directory fsync is performed; the contract is the same as before this change.

## GET (`route_get_snapshot`)

Under a shared lock the handler reads the version and ETag, evaluates `If-None-Match`, and
selects the body: the cached one-step diff when `X-Detent-Base-Version` equals its base, a
composed diff from `diff_history_` when the base is older, else the full bytes. It copies only
the chosen payload, releases the lock, and then calls the response writer. A writer that
re-enters the Distributor (a PUT between the headers and the payload) therefore does not change
the body or validators of the response in flight: `X-Snapshot-Version`, `ETag` and body all
come from the state read under the lock.

## Tests

`tests/detentd/test_publication_atomic.cc` sweeps allocation failures across PUT and PATCH,
refuses the stream, a write, a flush and the rename, and reenters the Distributor from the
writer. It covers recovery from the persisted file and history limits of zero, one and the
default.
