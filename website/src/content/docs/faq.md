---
title: "FAQ"
---

### How fast does a change reach my processes?

Within one poll interval of the distributor accepting it, while the source is
healthy: 200 ms by default for the agent and for clients that keep the default.
On a single machine, 1,000 pollers at 200 ms converge with p99 around
200–215 ms.

### What happens if the distributor is down?

Every client keeps serving its last known good snapshot, and keeps retrying in
the background with jittered backoff. Reads are unaffected; only freshness
suffers, which you'll see in `staleness_seconds`. Processes that start during
the outage serve their baked defaults.

### Why does reading a missing knob fail instead of returning `false`?

Because a missing knob is almost always a bug, a typo or a knob that was never
published, and `false` or `0` would hide it. Put a safe default in your baked
defaults instead. See [Errors and Failures](/Detent/reference/errors/).

### Can I store strings or JSON in a knob?

Not yet. Detent supports bool and int knobs only, which keeps evaluation
trivial and reads allocation-free. See the [Roadmap](/Detent/roadmap/#knob-model).

### Is the same user always in the same rollout bucket?

Yes. The bucket is a SHA-256 of the knob name and the hash value, so it's the
same on every host, in every language and across restarts. Raising the
percentage only adds users. Different knobs put the same user in unrelated
buckets.

### Do I need the agent?

No. A C++ process can poll the distributor or a file directly. The agent is for
shell scripts, programs in other languages, and hosts where you'd rather have
one poller than one per process.

### Can two people publish at the same time?

To a classic distributor, only if they coordinate versions; the second
publisher to use a version gets `409` and must retry. The
[durable authority](/Detent/guides/durable-authority/) supports concurrent
publishers properly, with server-assigned revisions and compare-and-set.

### Is the distributor secure?

The classic distributor has no authentication or TLS; keep it on a trusted
network or behind an authenticating proxy. The durable authority authenticates
publishers and peers. See [Security](/Detent/operations/security/).

### Is Detent JustKnobs?

No. Detent started as an independent implementation of the publicly observable
behavior of Meta's JustKnobs, and keeps its client semantics: namespaced bool
and int knobs, per-group overrides, fail-fast missing knobs, and asynchronous
whole-snapshot replacement. It's not a Meta product, and the distributor, agent,
authority and delivery system are Detent's own design.

### How does Detent relate to Xapiand?

They share libraries. Detent's HTTP server, reactor, Raft clustering, logger
and many smaller pieces were extracted from Xapiand into standalone
repositories, and Detent's durable consensus work lives in the same `cluster`
library without changing Xapiand's wire format.
