# reader-acl: Reader configuration ACL admission

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-acl`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/reader-acl.md`.

## Outcome

Reader capacity settings and private managed credentials refuse a configuration whose opened descriptor carries a foreign write-grant ACL, on macOS, while mode-only files and legacy configurations keep their behavior.

## Acceptance criteria

- A1 [artifact] K1 descriptor ACL trust: a foreign write-grant ACL on the opened configuration descriptor is refused, by the same predicate that protects private managed credentials; mode `0644` without an unsafe ACL stays valid: `docs/status/reader-acl.md`
- A2 [artifact] K2 legacy compatibility: configurations without reader settings keep final-symlink and path-resolution behavior: `docs/status/reader-acl.md`
- A3 [artifact] K3 the reviewer's macOS `everyone allow write` probe fails before the fix and rejects after it; a permanent regression checks `0644`, the denial and legacy acceptance: `docs/status/reader-acl.md`
- A4 [artifact] K4 reader-focused tests, the reviewer probe and a scoped ASan/UBSan build pass, and a mutant is caught: `docs/status/reader-acl.md`
- A5 [artifact] K5 the ACL admission rule, conservative platform behavior and test limits are documented: `docs/status/reader-acl.md`

## Contracts

Changes which configuration files reader settings accept: an extended write-grant ACL is refused.

## Decisions

1. Linux rich/NFSv4 ACLs and FreeBSD NFS4/ZFS remain unqualified (review condition C1); they belong to RC-5's platform matrix.

## May land early

None.
