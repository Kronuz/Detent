> Status: CURRENT (2026-11-03) how the classic distributor commits a snapshot and serves it without a lock, read from `src/detentd/detentd.cc` at the head of `phase/w2-s2`.

# Classic commit and published state

`detentd::Distributor` serves `GET /v1/snapshot`, `GET /v1/health`, `PUT` and `PATCH` as tasks on the
HTTP reactor loops. Only the commit belongs to an owner.

## Writes

`PUT` and `PATCH` take a write slot in `create_extension` (at most `kWriteSlots`, 1024; none free is a 503
with `Retry-After: 1` before any body byte), then `co_await submit_command(body, is_patch)`, which is
`detent::owner_submit` on the distributor's `OwnerLoop` (`owner-loop.md`). On the owner:

1. `stopping_` set: the write was never applied; the reply is the retry reply.
2. Unclustered: `try_accept` / `try_accept_patch` run the commit in `accept` and the write is settled before
   the wait starts.
3. Clustered: a `Write` is registered under a correlation id in `pending_` (owner thread only) and
   `raft_->add_command` is called. `on_raft_apply` (Bus loop) spawns `apply_envelope` on the owner, which
   runs in log order, settles the entry and sets its event.

## Published state

`resolve_and_commit` (owner thread only) resolves against `current_snapshot_`, persists, then builds a
`Published` (version, ETag, the bytes and their gzip and zstd forms, the one-step diff, the diff history as
`shared_ptr` steps, a `generation_cache::Cache` of composed diffs for that version) and swaps it into an
`atomic_shared_ptr`. GET and health load the pointer and never wait on a commit; a persist failure
leaves the published pointer untouched.

## Shutdown

`begin_shutdown()` sets `stopping_` and requests the service's stop source. The owner reads `stopping_`
before each write, so writers behind a long commit are answered at once with the retry reply; a commit
already started finishes and returns its real result. `main.cc` then runs the four phases across the HTTP
service and the owner (`teardown.md`).

## Tests

`tests/detentd/test_distributor_owner.cc`: a commit held on the owner while GET and health answer on the
only reactor; `begin_shutdown()` with a commit in progress and a queued writer; a clustered writer
without a quorum; teardown with a write suspended on the owner and no live frame left.
