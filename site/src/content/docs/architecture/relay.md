---
title: "Relay"
---

`detent-relay` is optional fan-out between the authority and the agents. It
holds one upstream subscription per configured bundle, keeps its own durable
copy, and serves many agents from it, so the authority sees one subscriber per
bundle instead of one per host.

---

## Inside the Relay

```d2 alt="The relay's upstream coordinator subscribes to the authority for each bundle, publishes a durable generation into the bundle directory and keeps encoded updates in a bounded cache; the session service answers agents over TLS with a delta from the cache or a snapshot"
direction: down
authority: "Authority\n(leader voter)"
relay: "detent-relay" {
  upstream: "Upstream coordinator\n(one per bundle)"
  store: "Bundle directory\n(durable generations)" {shape: cylinder}
  cache: "Update cache\n(recent deltas)" {shape: queue}
  sessions: "Session service\n(TLS, port 8318)"
  upstream -> store: "publish"
  upstream -> cache: "encoded update"
  store -> cache: "rebuild snapshot"
  cache -> sessions: "snapshot or delta"
}
agents: "Agents\n(subscription mode)" {style.multiple: true}
authority -> relay.upstream: "/v2/distribution/session/*"
relay.sessions -> agents: "/v2/distribution/session/*"
```

---

## What, How, When, Where

**What.** One relay serves a fixed list of bundles. A bundle is one upstream
scope: an authority identity, a principal, a policy epoch and a set of
namespaces.

**How.** Upstream, each bundle's `source` file uses the managed delivery
configuration, and relay sources always subscribe: an explicit
`"delivery_mode": "poll"` is rejected. Downstream, the relay listens with TLS
on `127.0.0.1:8318` by default and serves only `POST
/v2/distribution/session/open`, `/next` and `/close`, plus `GET /v1/health`.
A downstream bearer token grants a list of bundle indices. An agent behind a
relay must set `"delivery_mode": "subscription"`.

Each upstream update becomes a durable generation in the bundle's directory and
an entry in a bounded cache (64 recent edges and 64 MiB per bundle by
default). An agent whose cursor the cache covers gets a delta; anything else
gets a snapshot. Simultaneous agents borrow the same encoded payload.

**When.**

1. First start: a bundle with `"mode": "fresh"` provisions an empty store.
   Switch it to `"existing"` (or remove it) before the next restart.
2. Startup: the relay recovers every bundle and authenticates every upstream
   source; health reports ready only after both.
3. Steady state: upstream updates become generations and cache entries. Cache
   pressure produces a gap, and the relay rebuilds a current snapshot with
   bounded work.
4. Restart: it resumes from each bundle's last durable selection.

**Where.** Between the authority and the agent hosts. Build with
`-DDETENT_BUILD_RELAY=ON` (off by default; requires OpenSSL 3.0). The
configuration is `relay.json` in the user configuration directory, or the
system one with `--system`. Bundle directories are private (`0700`, files
`0600`); system services put them under `/var/lib/detent-relay` on Linux or
`/var/db/detent-relay` on macOS and FreeBSD. The source is in
`src/detent_relay/`.

---

## Read More

1. [Durable Relay guide](/Detent/guides/durable-relay/): configuration and
   services.
2. [Delivery and Local Images](/Detent/architecture/delivery/): the generation
   format the relay and agents share.
3. [Relay qualification and configuration](https://github.com/Kronuz/Detent/blob/main/docs/RELAY.md)
   in the repository.
