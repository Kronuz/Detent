---
title: "Update Path"
---

The update path runs on one background thread per client. Its job is to turn
untrusted bytes from a source into a new immutable snapshot, or to change
nothing at all.

---

## The Loop

```d2 alt="The updater polls the source, parses strictly, checks the version, merges over defaults and atomically publishes; stale or invalid candidates and transport errors go to jittered backoff and retry"
direction: down
poll: "poll source\n(file metadata or HTTP)"
parse: "parse + validate\n(strict RapidJSON)"
gate: "version > accepted?" {shape: diamond}
merge: "merge over baked defaults\n(whole-definition replace)"
publish: "SnapshotRef::store()\n(one release store)"
notify: "on_update hook\n(optional)"
backoff: "full-jitter backoff"
poll -> parse: "bytes"
poll -> backoff: "transport error"
parse -> gate
parse -> backoff: "invalid"
gate -> merge: yes
gate -> poll: "no: count stale"
merge -> publish -> notify
notify -> poll: "next interval"
backoff -> poll: retry
```

---

## Full Snapshots and Patches

A source returns either a full snapshot or a patch (`is_patch`). The client
keeps two snapshots:

1. **`live_snapshot_`**: the last accepted live data, *before* merging with
   defaults. Only the updater thread touches it.
2. **`current_`**: the merged snapshot that readers load.

A patch is always applied onto `live_snapshot_`, the client's own last
accepted state, never onto whatever base the source claims to have used. Then
the result is re-merged over the baked defaults. That's what makes deleting a
live knob correct: it falls back to its baked definition instead of vanishing,
and only the unmerged live snapshot can tell "overlaid" from "baked".

It also means a source's patch support is purely a bandwidth optimization. The
worst a confused source can cause is a rejected, stale update, exactly like a
full snapshot with an old version.

---

## Validation

Validation happens completely, off the read path, before anything is
published: JSON well-formedness, UTF-8, the schema, unknown fields, duplicate
keys, name rules, ranges, knob count and byte size. One bad knob rejects the
whole update. See [Snapshot Format](/Detent/reference/snapshot-format/#strictness).

---

## Backoff and Staleness

Transport failures and invalid updates back off with full jitter, using the
[random](https://github.com/Kronuz/random) library, so a fleet of clients that
lost the same distributor doesn't reconnect in lockstep. Staleness is measured
on `steady_clock`, so an NTP step can't make it negative or hide an outage.

---

## The `on_update` Hook

`InitOptions::on_update` is called on the updater thread right after each
accepted snapshot is published. The agent uses it to serialize every accepted
snapshot into shared memory. It must be fast and must not throw.

---

## Cost at Scale

Building a new snapshot copies the knob map. That's negligible for typical
configurations (35 µs for 100 knobs) but grows with size: applying a one-knob
patch to 500,000 knobs takes about 780 ms. The
[durable authority](/Detent/architecture/authority/) and
[managed delivery](/Detent/architecture/delivery/) replace whole-map copies with
persistent trees and exact-base deltas.
