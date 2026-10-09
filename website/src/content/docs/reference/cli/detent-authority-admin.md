---
title: "detent-authority-admin"
---

```text
detent-authority-admin import CONFIG SNAPSHOT
detent-authority-admin export CONFIG OUTPUT LEGACY_VERSION
```

Moves data between a classic distributor snapshot and a durable authority.
Built with `-DDETENT_BUILD_AUTHORITY=ON`, at
`build/src/detent_authority/detent-authority-admin`.

---

## `import`

```sh
detent-authority-admin import authority.json legacy-snapshot.json
```

1. Validates the legacy snapshot within the authority's bounds.
2. Applies it in namespace-scoped batches within transaction limits.
3. Compares every resulting definition with the source.
4. Publishes an application checkpoint.

Import only into a **new, separate** authority directory, with publishers
stopped until you've verified the result. Legacy version numbers never become
authority revisions. A failed import leaves its directory for inspection; it
can't be resumed silently or overwrite an existing authority. The import uses
one producer slot.

---

## `export`

```sh
detent-authority-admin export authority.json rollback-snapshot.json 1000
```

Recovers the committed state and writes it as a legacy snapshot with the given
`LEGACY_VERSION`.

1. Stop the authority first; its storage lock excludes a running daemon.
2. Choose a version greater than the destination distributor's current
   version, or the destination will reject it.
3. The output is created durably with mode `0600`; an existing file is never
   overwritten.

Verify the export and publish it through the destination's ordinary interface
before switching writers.
