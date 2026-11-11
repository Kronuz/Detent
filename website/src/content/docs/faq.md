---
title: "FAQ"
---

### How fast does a change reach my processes?

Within one poll interval of the source changing, while the source is
healthy: 200 ms by default for the agent and for clients that keep the default.

### What happens if the source is down?

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

No. A C++ process can poll a file or an HTTP endpoint directly. The agent is for
shell scripts, programs in other languages, and hosts where you'd rather have
one poller than one per process.

### Can two people publish at the same time?

Yes, through the [durable authority](/Detent/guides/durable-authority/). It
assigns revisions itself, and compare-and-set preconditions keep one publisher
from overwriting another's change. A snapshot file has no such protection: its
writers must coordinate versions themselves.

### Is the authority secure?

The durable authority authenticates publishers with bearer tokens, serves them
over TLS when it isn't bound to loopback, and authenticates voters with mutual
TLS. See [Security](/Detent/operations/security/).

### How does Detent relate to Xapiand?

They share libraries. Detent's HTTP server, reactor, Raft clustering, logger
and many smaller pieces were extracted from Xapiand into standalone
repositories, and Detent's durable consensus work lives in the same `cluster`
library without changing Xapiand's wire format.
