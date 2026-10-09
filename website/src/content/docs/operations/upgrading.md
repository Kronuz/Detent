---
title: "Upgrading"
---

Detent has no tagged releases yet, so there's no version-to-version upgrade
matrix. These are the compatibility boundaries to know when you update a
deployment built from `main`.

---

## Wire and File Formats

| Format | Version | Compatibility |
| --- | --- | --- |
| JSON snapshots and patches | `schema_version: 1` | Stable |
| Legacy shared-memory layout | 2 | Older readers reject it |
| Local generation images | 1 | Old SDK readers can't consume managed leases |
| Distribution profile | 1 | Negotiated per request |

---

## Shared-Memory Layout Version 2

Group rollouts (`{"rollout_pct": N}` group overrides) changed the agent's
shared-memory layout to version 2. When upgrading across that change:

1. Upgrade the `detent` reader binaries on the host.
2. Restart the agent through its service manager, or stop a user agent with
   SIGTERM and let the next `detent get` start the new one.

Older readers refuse the new layout rather than misreading it.

---

## Managed Generations and Readers

Managed publication uses lease files that old SDK readers don't understand.
Upgrade every reader to a version with dual legacy/managed support **before**
enabling managed publication on a host. Use `--upgrade-managed` to adopt a
compatible legacy generation directory explicitly.

---

## Classic Distributor to Authority

Moving publishers from `/v1/snapshot` to the authority is an explicit,
reversible migration with
[`detent-authority-admin`](/Detent/reference/cli/detent-authority-admin/):
import into a new directory, verify, switch writers; export with a higher legacy
version to roll back.

---

## C++ ABI

Detent is consumed from source. Public structs such as `InitOptions` and
`HealthSnapshot` gain fields over time, so rebuild every consumer when you
update Detent.
