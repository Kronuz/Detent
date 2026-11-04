---
title: "FAQ"
---

### How fast does a change reach my processes?

Within one agent poll interval of the authority committing it, while the
authority is reachable: `poll_interval_ms`, 200 ms by default in poll mode, or a
long poll in [subscription mode](/Detent/guides/managed-delivery/). Once the
agent publishes the generation, a reader's next lookup sees it.

### What happens if the source is down?

The agent keeps its last durable generation and retries with jittered
backoff. Readers keep answering from that generation; only freshness suffers.
A process that starts on a host with no generation yet gets each read's
fallback, with the status `UNAVAILABLE`.

### What does reading a missing knob return?

The fallback the caller passes, never an error. A missing knob is almost always
a bug, a typo or a knob that was never published, so `lookup` reports it as
`MISSING`; check that status where a silent fallback would hide a mistake. See
[Errors and Failures](/Detent/reference/errors/).

### Can I store strings or JSON in a knob?

Not yet. Detent supports bool and int knobs only, which keeps evaluation
trivial and reads allocation-free. See the [Roadmap](/Detent/roadmap/#knob-model).

### Is the same user always in the same rollout bucket?

Yes. The bucket is a SHA-256 of the knob name and the hash value, so it's the
same on every host, in every language and across restarts. Raising the
percentage only adds users. Different knobs put the same user in unrelated
buckets.

### Do I need the agent?

Yes. The agent is the one process on a host that talks to the authority; every
reader, in C++, Python, Rust or the shell, reads the generations it publishes.

### Can two people publish at the same time?

Yes, through the [durable authority](/Detent/guides/durable-authority/). It
assigns revisions itself, and compare-and-set preconditions keep one publisher
from overwriting another's change.

### Is the authority secure?

The durable authority authenticates publishers with bearer tokens, serves them
over TLS when it isn't bound to loopback, and authenticates voters with mutual
TLS. See [Security](/Detent/operations/security/).

### Is Detent JustKnobs?

No. Detent started as an independent implementation of the publicly observable
behavior of Meta's JustKnobs, and keeps its knob model: namespaced bool and int
knobs, percentage rollouts, per-group overrides, and whole-snapshot
replacement. It's not a Meta product, and the agent, authority
and delivery system are Detent's own design.

### How does Detent relate to Xapiand?

They share libraries. Detent's HTTP server, reactor, Raft clustering, logger
and many smaller pieces were extracted from Xapiand into standalone
repositories, and Detent's durable consensus work lives in the same `cluster`
library without changing Xapiand's wire format.
