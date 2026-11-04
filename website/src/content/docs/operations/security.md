---
title: "Security"
---

The durable authority authenticates publishers and voters, bounds every
admission layer, and fences storage. Host-local reads trust the host.

## Host-Local Reads

1. A **user** agent's runtime directory is private (`0700`).
2. A **system** agent's snapshot is world-readable (`0644`) by design, and only
   writable by the service account. Every local user can read every knob.
3. Readers map the snapshot read-only and bounds-check every offset and length
   against the payload size before dereferencing.
4. A **file source** accepts any valid snapshot in its file, so whoever can
   replace the file can publish to that host. Keep it writable only by the
   publisher.

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
