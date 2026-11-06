# Detent security review: reader S6

1. Date: 2026-11-05.
2. Reviewed: `reader/s6` at `Detent:78195dd`, code head `Detent:31ac047`; the diff against main; `src/detent_reader/core.cc`, `src/detent_local/prepared_reader.cc`, `lazy_page_access.cc`, and their file-validation helpers.
3. Checked against: `docs/design/reader.md` D1–D5, D10, T3, T6 and Limits 2, 5, 6; `docs/reviews/calibration.md`; `reader-design-preliminary.md`, `reader-s3.md`, `prepared-reader-component-2026-10-26.md`, `reader-capacity-security-2026-10-23.md`; the candidate ledger and main handoff; the campaign reviewer constraints.
4. Checked by: four existing ASan/UBSan regression tests and a standalone synthetic file-shape unit test, linked against the candidate's separate Debug ASan/UBSan build. Compiler: Apple `/usr/bin/c++`, C++20, `-fsanitize=address,undefined -O1 -g`. No candidate source modifications. Every run uses `quiet light` with a hard timeout.
5. Review artifacts: `.claude/worktrees/review-reader-s6-security/.scratch/review-reader-security/`. `file_shape.cc`, `build.sh`, `file-shape.log`, and `regression.log` are retained. The review worktree is `review/reader-s6-security` at the candidate head, with no tracked changes.

## Verdict

**REJECT: a malformed `change` file can block before its file-type validation. The original fork timeout also remains unexplained.**

No high or critical vulnerability is established. SR1 is a reproduced medium-severity availability defect within the publisher boundary. This review stops at that blocker; the pending checks below have no approval.

## Attackers considered

1. A local user outside the configured publisher account, attempting to redirect a reader through unsafe names, links or writable files. The synthetic link and mode cases are refusals; this review establishes no permission bypass.
2. A malformed store within a directory admitted for `publisher_uid`, including a nonregular file where the format requires a regular file. This is the boundary exercised by SR1.
3. Accidental corruption and publisher changes after a mapping is verified. The design explicitly permits unchecked subsequent page corruption and publisher-induced truncation faults. Those owner-visible limits are preserved; this review does not impose a new mutation policy.
4. Concurrent callers and applications that fork with open handles, including cold lazy-page work. Their pending lifetime checks remain distinct from malicious-publisher authority.

## Findings

### SR1 (medium, blocking): a FIFO blocks before regular-file validation

`ChangeMapping::open` calls blocking `openat(O_RDONLY | O_CLOEXEC | O_NOFOLLOW)` before `fstat` and `checked_words` reject nonregular files (`src/detent_local/change_file.cc:100`, `:107`). A FIFO without a writer waits inside `openat`, so the regular-file check never runs. `detent_open` consequently has no bounded error result for this invalid file shape.

The synthetic unit test creates six isolated stores inside the review's scratch directory, each with one malformed `change` entry. Directory, symlink, hardlink, writable-mode and seven-byte-file cases return `DETENT_SOURCE_FAILURE`. The FIFO case receives `SIGALRM` after two seconds instead of returning. `file-shape.log` records `change fifo bounded refusal=NO exit=-1 signal=14`; the unit test exits 1. The child alarm and outer 120-second timeout bound the test; all children are reaped and the fixtures removed.

A party able to install a malformed entry inside the admitted store can stall the opening thread. On a refresh of a registered handle, the same open runs while the handle owns `refreshing`; `fork_prepare` waits for that flag (`src/detent_reader/core.cc:205`). The process-wide fork consequence follows from the source and is not separately executed here. The same blocking-open pattern occurs in `prepared_reader.cc:31` and `lazy_page_access.cc:125`; their FIFO behavior is not separately executed.

Attribution: the `change` helper lands in S5; the prepared-reader pattern predates S6. S6 exposes the helper through its new C ABI and makes registered refreshes participate in process-wide fork preparation. This is not attributed to the selected-replacement fix.

Required change: open these already-required regular files with a mode that cannot wait for a FIFO peer, then retain the descriptor-based regular-file, ownership, extent and ACL checks. `O_NONBLOCK` before validation is one suitable POSIX mechanism for the FIFO case. Add bounded unit tests for `change` and the selected/lease/pack paths, and verify that ordinary regular-file reads retain their values. This preserves the declared accepted file types.

### SR2 (medium, source finding; runtime check pending): `change` identity is read from another lookup

`open_store` obtains a mapping through `ChangeMapping::open` (`core.cc:317`) and then assigns its stored device/inode from `fstatat` of the pathname (`:329`). The mapping helper checks an opened descriptor and closes it after `mmap` (`change_file.cc:108`, `:114`). A replacement between the two operations therefore permits a mapping of inode A to be tagged with inode B's identity. The force-refresh comparison uses that tag (`core.cc:371`), so it need not remap when the pathname still names B.

This contradicts Limit 2's explicit-refresh recovery promise for a replacement concurrent with attachment. It does not establish an outside-publisher permission bypass, and replacement already violates the normal agent invariant. Return the identity captured from the mapped descriptor alongside the mapping; compare that identity with the name when deciding whether to retry. A deterministic local replacement regression remains pending. No runtime result is claimed for this finding.

## Evidence that holds

1. Independent ASan/UBSan rerun: `test_reader_core`, `test_reader_syscalls`, `test_local_lazy_page_access`, and `test_local_lazy_page_bitmap` pass 4/4 in 10.72 seconds. The bitmap test reports 52 neighbor-corruption refusals, both concurrent publish/verification windows, and raced-admission recovery. `regression.log` contains the raw CTest output.
2. `checked_or_unlinked` retries at most three times and discards the unchecked record on each retry (`prepared_reader.cc:39`, `:51`). It does not return unchecked bytes after a failed validation. A missing/replaced record becomes unavailable; persistent non-unlinked failures propagate. No accepted-byte bypass is identified in this source check.
3. Attachment checks the descriptor/lease/storage/root/metadata identity chain and corpus bounds before returning `AttachedGeneration` (`prepared_reader.cc:132`). The first-touch extent check precedes the bitmap hit (`lazy_page_access.cc:207`). Post-verification mutations remain the explicit design limits, including the separate S3b concern about reading a rewritten page header.
4. Supplied proof at `.claude/worktrees/agent-a1fbf8344cbf48ecb/.scratch/s6-final-proof/` records the final selected-replacement revert killed at line 68, restored core passes, focused fork passes, and the two sanitizer trial series. These are supporting evidence, not independent reruns of those series.

## Blocking items

1. SR1: remove the blocking-open validation gap and pass bounded synthetic-file regressions against the corrected candidate.
2. Concurrency closure: attribute the raw fork-reply timeout in `.scratch/overnight/done/detent-s6-soak-fixed/x4-1-4.log`. That log reports 792,825,041 lookup pairs and T3 completion, then fails the fork-reply assertion at line 390 of that test version. Successful subsequent trials do not explain this failure. No causal link to SR1 or SR2 is established.

## Pending checks and conditions

1. SR2: force replacement between mapping and identity capture in a synthetic-file regression; check that explicit refresh recovers and later publications are observed. This requires no permission bypass or change to the publisher mutation policy.
2. Fork during lazy-page preparation: `LazyPageWindow::Publish` occurs after a segment and its mapping are constructed but before the segment is reachable from the published slots (`lazy_page_access.cc:173`, `:180`). The atfork handlers quiesce `refreshing`, not cold page resolution. Check child cleanup and resource accounting after a fork in this window, including two live handles. Unreachable child resources are a source concern, not a reproduced leak or a diagnosis of the timeout.
3. Resource accounting: the new core creates effectively unbounded private ledgers (`core.cc:608`), while `ChangeMapping` maps the validated file size without charging that mapping to them. Operator capacity options are assigned to later reader work. A resource-exhaustion policy or arbitrary size cap is not imposed here; any change to accepted capacity belongs to the owner. Size/accounting stress remains pending.
4. CI at the review's status check: run 41974132576 has all executed non-TSan jobs successful; the rerun of `ci / linux-tsan` is in progress. FreeBSD and headers are skipped. Final landing still requires the applicable terminal CI result.

## Not run

1. A new full dependency build, a full product test suite, benchmarks, fuzzing, remote hosts or AWS. The local regression executable reuses the candidate's separate ASan/UBSan archives; it compiles no modified candidate source.
2. Independent TSan execution, the 20-trial sanitizer series, reproduction or diagnosis of the original parallel fork timeout, simultaneous-handle fork windows, and lazy-page child cleanup.
3. Runtime tests for SR2, malformed descriptor/ownership/root/metadata combinations, ACL manipulation, payload/record FIFOs, large sparse change mappings, and corruption after verification. Work stops after the verified SR1 blocker.
