---
title: "HTTP API"
---

`detentd` speaks plain HTTP/1.1 with JSON bodies. It has two API surfaces,
depending on how it's started.

| Profile | Started with | Endpoints | Authentication |
| --- | --- | --- | --- |
| Classic distributor | `detentd` | [`/v1/snapshot`](/Detent/reference/http-api/snapshot/), [`/v1/health`](/Detent/reference/http-api/health/) | None |
| Durable authority | `detentd --authority CONFIG` | [`/v2/*`](/Detent/reference/http-api/authority/), [`/v1/health`](/Detent/reference/http-api/health/) | Bearer token, optional TLS |

Examples on these pages are written as REST snippets:

```rest
GET /v1/snapshot
```

Hover a snippet and use the copy button to get the equivalent `curl` command
against `http://localhost:8317`.

---

## Common Behavior

1. **Bodies are bounded.** Request bodies over 16 MiB are rejected before
   they're buffered (256 KiB per authority transaction).
2. **Compression is negotiated.** Responses honor `Accept-Encoding: zstd, gzip`,
   preferring zstd on a tie. Bodies under 256 bytes are sent uncompressed.
3. **Errors are JSON.** A rejected request returns `{"error":"..."}` with an
   appropriate status code.
4. **Requests are short.** No endpoint long-polls; every request is answered
   immediately.

:::hint[The classic API has no authentication]{.warning}
Anyone who can reach a classic distributor's port can publish. Keep it on a
trusted network, or put an authenticating TLS proxy in front of it. See
[Security](/Detent/operations/security/).
:::
