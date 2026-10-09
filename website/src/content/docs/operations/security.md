---
title: "Security"
---

Detent's security model is simple to state: the **classic** profile trusts its
network, and the **durable authority** doesn't.

---

## Classic Distributor and Agents

:::hint[No authentication, no encryption]{.warning}
`detentd`'s classic HTTP API and its multicast Raft transport have no
authentication, authorization or TLS. Anyone who can reach the port can
publish a snapshot and change every knob.
:::

Deploy it behind one of:

1. A firewall or security group that only admits publishers and pollers.
2. An authenticating TLS reverse proxy for publishers, with the distributor
   bound to a private interface.
3. The durable authority instead.

Things the classic profile does protect against:

1. **Malformed and oversized input.** Bodies over 16 MiB are rejected before
   buffering; JSON nesting is bounded; parsing is strict.
2. **Partial updates.** A snapshot is validated completely before it's
   accepted.
3. **Replays.** Stale and duplicate versions are rejected.

## Host-Local Reads

1. A **user** agent's runtime directory is private (`0700`).
2. A **system** agent's snapshot is world-readable (`0644`) by design, and only
   writable by the service account. Every local user can read every knob.
3. Readers map the snapshot read-only and bounds-check every offset and length
   against the payload size before dereferencing.

Never put secrets in knobs.

## Durable Authority

1. **Publishers** authenticate with bearer tokens, over HTTPS when not bound to
   loopback. Producer identity comes from the credential; each credential is
   authorized for specific namespaces, and `publish: false` makes it read-only.
2. **Voters** authenticate each other with mutual TLS plus a per-voter SHA-256
   certificate pin. A semantic fingerprint in the peer hello rejects voters
   with a different quorum or limits.
3. **Admission** is bounded at every layer: per-request bodies, aggregate
   bytes, request slots, per-producer concurrency, handshakes and connections,
   with deadlines on headers, bodies and writes.
4. **Configuration files** holding tokens must be regular, single-link files,
   owned by root or the service account, without world access.
5. **Managed agents** verify the authority's certificate chain and hostname,
   and keep their token in a private file that readers never open.

## Reporting a Vulnerability

Please report security issues privately through
[GitHub security advisories](https://github.com/Kronuz/Detent/security/advisories/new)
rather than public issues.
