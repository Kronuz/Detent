---
title: "About"
---

[GitHub]: https://github.com/Kronuz/Detent

Detent is a _fast_, _simple_ and _safe_ runtime-configuration system for feature
flags, kill switches and live settings. It features:

##### Simple knob model:
  * Namespaced `bool` and `int` knobs.
  * Percentage rollouts with stable SHA-256 buckets.
  * Per-group forced values and per-group rollouts.

##### Allocation-free reads:
  * One atomic load, one lookup and one evaluation per read.
  * No network, disk, lock or allocation on the read path.
  * Pinned views for consistent multi-knob reads.

##### Safe updates:
  * Strict validation off the read path.
  * Atomic whole-snapshot swaps; last known good on any failure.
  * Fail-fast on missing knobs, with baked defaults shipped alongside code.

##### Distribution:
  * A small HTTP distributor with ETags, cached diffs and zstd/gzip.
  * Optional Raft replication.
  * A host-local shared-memory agent and `detent get` for any language.

##### Durable authority:
  * Authenticated, atomic, per-namespace transactions with compare-and-set.
  * Durable consensus with mutual TLS between fixed voters.
  * Authenticated delivery of snapshots and deltas into immutable local
    generations.

##### Everywhere:
  * C++20 library, Python and Rust bindings, and a shell reader.
  * Linux, macOS and FreeBSD, with systemd, launchd and rc.d templates.

You can find the source code for Detent on [GitHub].

---

## Author

* [Germán Méndez Bravo (Kronuz)](https://Kronuz.github.io)
