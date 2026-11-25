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
  * One load of the change sequence, one lookup and one evaluation per read.
  * No network, lock, syscall or allocation on a warm read.
  * No reader thread: a lookup sees a new generation on its own.

##### Safe updates:
  * Strict validation off the read path.
  * Atomic whole-generation switches; last known good on any failure.
  * A fallback on every read, with the status that explains it.

##### Distribution:
  * Managed delivery from the authority into immutable local generations.
  * A host-local managed agent and `detent get` for any language.

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
