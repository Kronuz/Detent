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
| Local generation images | 1 | Old SDK readers can't consume managed leases |
| Distribution profile | 1 | Negotiated per request |

---

## Managed Generations and Readers

Managed publication uses lease files that old SDK readers don't understand.
Upgrade every reader to a version with dual legacy/managed support **before**
enabling managed publication on a host. Use `--upgrade-managed` to adopt a
compatible legacy generation directory explicitly.

---

## C++ ABI

Detent is consumed from source. Public structs such as `InitOptions` and
`HealthSnapshot` gain fields over time, so rebuild every consumer when you
update Detent.
