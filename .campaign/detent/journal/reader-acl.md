# reader-acl: journal

State: landed
Quest: .campaign/detent/quests/reader-acl.md (the lead's; never edit it)

History: landed at `Detent:a963c4e` on 2026-10-23, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | open: partial, macOS only: Linux POSIX and rich/NFSv4 ACLs and FreeBSD need CI or host validation (Evidence, Blocked on; review condition C1) in `docs/status/reader-acl.md` |
| A2 | `docs/status/reader-acl.md`; `docs/reviews/reader-capacity-security-2026-10-23.md`: "still permits a legacy symlink when no reader field is present." |
| A3 | `docs/status/reader-acl.md`: "The reviewer's unchanged probe reaches SR1 and rejects the ACL on the fixed binary" |
| A4 | open: partial: no mutant recorded, and the sanitizer run is a focused predicate check, not a sanitizer build of the CLI (Scoped sanitizer check) in `docs/status/reader-acl.md` |
| A5 | `docs/status/reader-acl.md`; `docs/USAGE.md`: "World-readable mode `0644` is allowed when its ACL is safe. On macOS and FreeBSD, nontrivial ACLs are refused" |

## Result

The fix `Detent:3771317` landed on 2026-10-23; evidence recorded through `Detent:a963c4e`.
Baseline `8b97fff` accepted `everyone-write-acl-0644`; the fixed binary rejects it. Regression 1/1; reader/managed tests 3/3.
Scoped ASan/UBSan of the predicate: ordinary file, invalid descriptor, 1,000 inspections of a write-grant ACL, ACL removal.
Review: `docs/reviews/reader-capacity-security-2026-10-23.md`, REJECT (SR1), then its macOS re-check approves with condition C1.
Docs: `docs/USAGE.md`, `docs/SERVICES.md`. No hosted CI run is recorded in the ledger.
Left open: A1, A4.

## Rulings I made

None recorded.
