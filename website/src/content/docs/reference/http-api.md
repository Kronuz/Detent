---
title: "HTTP API"
---

`detentd --authority CONFIG` runs the durable authority, which speaks HTTP/1.1
with JSON bodies, over TLS when its configuration enables publisher TLS.

| Endpoints | Authentication |
| --- | --- |
| [`/v2/*`](/Detent/reference/http-api/authority/) | Bearer token |
| [`/v1/health`](/Detent/reference/http-api/health/) | None |

Examples on these pages are written as REST snippets:

```rest
GET /v1/health
```

Hover a snippet and use the copy button to get the equivalent `curl` command
against `http://localhost:8317`.

---

## Common Behavior

1. **Bodies are bounded.** Authority request bodies are limited to 256 KiB.
2. **Errors are JSON.** A rejected request returns `{"error":"..."}` with an
   appropriate status code. Authentication and body-admission failures close
   the connection.
3. **Identity is exact.** Every `/v2` request names the authority's `cluster`,
   `group` and `epoch`.
