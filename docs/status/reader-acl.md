# Reader configuration ACL admission

**State:** Native macOS regression and independent security recheck pass; platform conditions remain.

## Done criteria

1. **K1 Descriptor ACL trust:** Reader capacity settings reject a foreign write-grant ACL on the opened configuration descriptor. The same internal predicate protects private managed credentials. Mode `0644` without an unsafe ACL remains valid. Linux POSIX ACL mode-mask behavior stays valid; FreeBSD rejects nontrivial NFSv4 and POSIX ACLs and denies inspection errors.
2. **K2 Legacy compatibility:** Configuration files without reader settings retain final-symlink and path-resolution behavior. Reader settings retain their existing ownership, mode and extent checks.
3. **K3 Fail/pass probe:** The reviewer's macOS `everyone allow write` probe fails before the fix and rejects after it. A permanent regression checks mode `0644`, the ACL denial, and legacy acceptance.
4. **K4 Scoped validation:** Run the reader-focused tests, the reviewer probe, and a scoped ASan/UBSan build. Show a mutant that the regression catches. Keep all builds and tests under `quiet light` with at most two jobs.
5. **K5 Docs:** Document the ACL admission rule, conservative platform behavior, and test limits.

## Evidence

- Baseline `8b97fff` reader binary: `.scratch/review-reader-security/probe_config.py` reports `everyone-write-acl-0644: accepted` under `quiet light` on macOS. The file's mode remains `0644`.
- FreeBSD ACL admission needs FreeBSD CI or host validation. macOS cannot execute that branch, and Linux POSIX ACL behavior needs Linux CI or host validation.

## Done

- Recovered reader regression passes (1/1, 0.29 seconds). The reviewer's unchanged probe reaches SR1 and rejects the ACL on the fixed binary; the probe's baseline acceptance assertion fails as expected.

## Next

- The recovered product reader/managed tests pass (3/3, 0.95 seconds); the CLI probe rejects SR1 and preserves the other expected outcomes.
- Qualify Linux rich/NFSv4 ACLs and native FreeBSD before extending the filesystem trust claim.

## Blocked on

- Native non-macOS qualification remains pending. The macOS security recheck approves with condition C1.

## Scoped sanitizer check

The descriptor predicate passes ASan/UBSan on macOS for an ordinary file, an invalid descriptor, 1,000 inspections of an extended write-grant ACL, and removal of that ACL. The retained probe is `.scratch/reader-acl-sanitizer/check.cc`. LeakSanitizer is disabled on this host; this is a focused predicate check, not a sanitizer build of the complete CLI.
