# Managed CLI reader capacity security review

**Date:** 2026-10-23  
**Scope:** `3334fdc` and `ceb2487` against `5721d53`; native source and the equivalent `8b97fff` reader worktree artifact.  
**Verdict:** **REJECT**. SR1 bypasses the configured reader file permission policy on macOS.

## Boundary checked

A local user with write access to a public `agent.json` is the attacker. The capacity file may raise reader admission, so its reader fields require a regular file owned by root or the effective user, without group or other write permission or a final symlink. The opened descriptor, rather than a second path lookup, supplies the bytes and both metadata checks. A same-size rewrite by the file owner remains within that owner's authority; the loader promises extent stability, not an immutable snapshot against its owner.

The reader settings come from flags, environment, then the public config. They do not come from the image or the private managed credential file. The config error paths include the public path or field name, not a field value or credential. The parser rejects duplicate keys, negative or nonintegral capacities, values beyond unsigned 64-bit, and width violations before a reader is opened. Explicit resources have a 1 MiB retirement baseline and separate heap and mapping ledgers; the command owns the domain until its views, client and source have been destroyed.

## Blocking finding

**SR1 (medium): macOS ACL write access bypasses trusted file admission.** [`config.cc:136`](../../src/detent_cli/config.cc) checks only `st_mode & 0022`. A mode `0644` file owned by the effective user can also have `group:everyone allow write` in its macOS ACL. The file then meets this check despite granting other local users write access. An attacker who can edit that public file can raise `reader_heap_bytes`, `reader_mapping_bytes`, or `reader_owners` for a service reader and cause resource pressure beyond the administrator's intended limits.

The probe `.scratch/review-reader-security/probe_config.py` creates exactly that file with `chmod +a "everyone allow write"`, asserts that `stat` still reports `0644`, and runs the real `detent get` binary. The CLI accepts its `reader_records` field. `.scratch/review-reader-security/probe.log` records `0: group:everyone allow write` and `everyone-write-acl-0644: accepted`. Reject write granting ACL entries on a reader authority file, or conservatively reject any extended ACL, using the already opened descriptor. Recheck the same probe after the fix.

## Other evidence

1. The descriptor trust check is in [`config.cc`](../../src/detent_cli/config.cc): final `O_NOFOLLOW`, `fstat` before and after the bounded read, regular type, effective-user/root owner, mode, and unchanged extent. Legacy symlinks reach the same parser but cannot authorize a reader field.
2. [`reader_capacity.cc`](../../src/detent_agent/reader_capacity.cc) parses complete decimal `uint64_t` values, checks format and host widths, then creates the private runtime and ledgers. The holder is noncopyable and nonmovable; member destruction closes its domain before destroying the runtime.
3. [`main.cc`](../../src/detent_agent/main.cc) constructs the holder before the managed `Client`, source and read view. Status also destroys its local view before the holder. The reader path never opens `managed.json` for capacity.
4. `.scratch/review-reader-security/probe_config.py` ran against `.scratch/reader-capacity-build/src/detent` under `quiet light`. The CLI accepted owned mode `0644` and a legacy symlink without reader fields. It rejected group writable mode, a final symlink with a reader field, duplicate keys, negative and overflowing integers, and a bad flag. Environment and flag precedence passed. The build artifact comes from the equivalent `8b97fff` worktree.
5. The existing targeted gate `.scratch/reader-domain-address-tests.log` reports `test_agent_cli` and `test_reader_capacity` passed (2/2). The separate `.scratch/reader-capacity-full-tests.log` reports 218/218 product tests passed. Those logs corroborate the review; the focused CLI probe above was run independently.

## Limits

**Not run:** A new sanitizer build, the full product test matrix, the one-million-record CLI case, a live same-extent writer race, an ACL write from a second user account, and a fresh flat/paged managed process setup. The source and existing process logs cover the last path, but this review only reran the focused CLI probe. No remote hosts or shared measurements were touched.

## Re-check: `5fda9ad` (2026-10-23)

**Verdict:** **APPROVE WITH CONDITIONS** for the reviewed ACL fix. **SR1 is closed on macOS.** No reproduced blocker remains on the native reader path.

The new `mode_only_acl(fd)` rejects any macOS extended ACL on the already opened public config descriptor. The reader still accepts ordinary mode `0644` and still permits a legacy symlink when no reader field is present. The private managed credential loader now applies the same helper before and after its read. On FreeBSD the helper asks whether an NFSv4 ACL, or a POSIX ACL when NFSv4 is unsupported, is trivial; inspection errors reject the file. Other unrecognized platforms reject trusted admission.

I reran `.scratch/review-reader-security/probe_config_fixed.py` against the rebuilt `.scratch/reader-capacity-build/src/detent` under `quiet light`. `.scratch/review-reader-security/recheck.log` records the mode `0644` file with `group:everyone allow write` and `everyone-write-acl-0644: rejected`; the other 12 cases retain their expected results. The existing `.scratch/reader-acl-product-tests.log` reports the reader, TLS certificate and managed config tests passed (3/3). I did not rerun those tests.

**C1 (scope condition):** On Linux, `mode_only_acl` relies on POSIX ACL write grants appearing in the group-class mode mask. It does not inspect Linux NFSv4 or rich ACLs. This re-check cannot certify the stated no-group/other-write policy on a filesystem where those ACLs can grant write while `st_mode` remains read-only. Before claiming that policy for such mounts, either enforce it with a descriptor-based check or restrict deployment to filesystems whose ACL grants are reflected in the mode mask. This is a coverage limit, not a reproduced Linux bypass.

**Blocking items:** None for the native macOS path. C1 remains a condition for a filesystem-independent Linux trust claim.

**Not run:** Linux NFSv4/rich ACL or FreeBSD execution, a second-user ACL write, sanitizer and full product test matrices, and the one-million-record CLI case. No build or shared measurement ran in this re-check.
