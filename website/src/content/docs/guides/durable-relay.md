---
title: "Durable Relay"
---

The optional `detent-relay` daemon receives authenticated binary subscriptions from the authority, publishes durable local generations and serves shared snapshots and deltas to agents. Applications continue reading their local agent generations.

```text
Authority → Relay → Agents → Application readers
            durable   durable
            cache     generations
```

Build with `-DDETENT_BUILD_RELAY=ON`. A relay-only build can disable distributor and authority targets; the relay does not link authority state or consensus. Hosting has Stage 5B component acceptance; incremental publication and sustained fleet qualification remain separate [roadmap](/Detent/roadmap/) gates.

## Configuration and services

The default private configuration is `relay.json` in the standard user configuration directory. `--system` selects system locations, and `--config PATH` selects an explicit file. TLS listens on loopback port `8318` unless configured otherwise. Each configured bundle uses its own private upstream credential and durable directory; downstream credentials grant exact bundle access. See the [complete relay configuration guide](https://github.com/Kronuz/Detent/blob/main/docs/RELAY.md) for the JSON schema and initialization procedure.

Run in the foreground under the supplied systemd, launchd or rc.d template. `--status` queries local health without starting a daemon; unmanaged runs also support `--daemonize`. Durable roots require service ownership and mode `0700`, with artifacts remaining `0600` in system mode. Readiness requires recovery and fresh upstream authentication for every required bundle.

## Bounded shared work

One pre-admitted completion port per bundle feeds a fixed owner FIFO and one native completion driver. Participant count does not add disk workers. Each grant executes one current primitive through authentic settlement; queued cancellation may finish an operation or change its request to cleanup before submission. Network and control handling continue while disk completion is pending.

The relay retains cached encoded frames across downstream delivery, so simultaneous readers borrow the same payload. This establishes bounded component ownership, rather than a sustained fleet capacity claim. Shutdown drains listener originals and disk operations before retiring stores and workers.

For system services, use absolute bundle directories under `/var/lib/detent-relay` on Linux or `/var/db/detent-relay` on macOS and FreeBSD. Relative bundle directories resolve beside the host configuration; placing them below `/etc` is incompatible with the Linux unit’s filesystem protection. Durable state belongs outside runtime directories, which are cleared at boot.

Relay admission is configurable through `connections` and `service_limits` in the private host configuration. Defaults admit 1,024 connections and sessions, with 640 polls. Raising these ceilings also requires heap and descriptor capacity. See the [capacity settings](https://github.com/Kronuz/Detent/blob/main/docs/RELAY.md#capacity-and-continuity) for ranges and relationships.

A relay upstream source can configure an authority endpoint pool as described in [managed delivery](managed-delivery.md). Downstream agents use a single relay endpoint and must explicitly set `"delivery_mode":"subscription"`. Relay Open establishes access to its recovered bundle; it does not prove a fresh authority quorum, so authority endpoint pools must not contain relay addresses.
