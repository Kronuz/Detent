> Status: CURRENT (2026-11-06) Findings for the dense-pages lead on why reader T2 (200/200 first reads below 1 ms) fails, with one measured fix on this branch.

# dense-pages: reader T2 first reads

- Branch: `dense-pages-first-read`, from `quest/dense-pages` at `Detent:861d652`. Not pushed, not merged.
- Failing job: `261106-173358-3b82ed` (`dense_reader_measure.py --mode first`) stops at "candidate-1ns misses the 200/200 first-read gate". Its own samples:

| arm (5M, stopped publisher) | below 1 ms | median | attach | lookup |
|---|---|---|---|---|
| baseline (main) | 90/200 | 1031 µs | 578 µs | 428 µs |
| dense, 4 namespaces | 115/200 | 974 µs | 548 µs | 379 µs |
| dense, 1 namespace | 137/200 | 855 µs | 501 µs | 344 µs |
| dense, 1,000 namespaces | 79/200 | 1039 µs | 554 µs | 470 µs |

## The failure is not specific to dense pages

The baseline misses the gate in the same run, and dense pages is at or below it. The cost is in opening the store, not in the page format: a first read spends about 85% of its time in syscalls and process-cold effects.

## Where a first read goes

Phase timestamps (scratch-only instrumentation, `.scratch/dense-t2/diagnostics-full.patch` in the main checkout), median per read, 1-namespace store, i9-9980HK, macOS 15.8.1. "Cold" is the gate's case: a fresh process after a warmup process. "Warm" is a fresh `Client` inside an already-running process.

| phase | cold | warm |
|---|---|---|
| directory trust walk, 10 path components | 394 µs | 192 µs |
| segment open and map, 3 to 4 segments | 259 µs | 119 µs |
| generation records (5 record opens) | 255 µs | 114 µs |
| SHA-256 page verification, 4 to 7 pages, portable kernel | 87 µs | 60 µs |
| before the source starts (code page faults, static init) | 84 µs | 2 µs |
| total | 1139 µs | 538 µs |

1. Every syscall costs about twice as much in a cold process. On this host `openat` is about 12.8 µs warm, a dirfd `fstatat` 5.3 µs and `fstat` 1.4 µs.
2. Each path component of the trust walk issued 5 syscalls: lstat, open, fstat, an ACL fetch (itself an `fstatx_np`) and a re-check `fstatat`. Each segment issued 7.
3. Path depth matters. The measurement stores live 10 components deep, under `~/.scratch/campaign/jobs/<id>/a1/out/candidate/`. A clone 4 components deep is about 125 µs faster.
4. About 60 of the roughly 90 minor faults are first-touch faults on the probe's 600 KiB of `__TEXT`. Prefaulting it saves 100 to 170 µs.
5. A busy core elsewhere on the package saves 40 to 150 µs, so CPU idle and turbo state is part of the cold penalty.
6. The host itself is noisy: baseline medians range from 1031 to 1236 µs across runs today. The 2026-10-26 qualification measured a 715 µs median on the same host.

## Fix on this branch: fewer syscalls, same checks

The commit `Merge each trust status and ACL check into one fstatx_np` keeps every check and every error. It changes only how many syscalls each check takes.

1. macOS `acl_get_fd_np` is `fstatx_np` plus `filesec_get_property(FILESEC_ACL)`. `status_and_acl` issues it once and returns both, in place of `fstat` followed by `acl_get_fd_np`. A probe of files with no ACL, a deny-only ACL and an allow ACL gives the same errno and the same ACL text through both paths. Other platforms keep `fstat` plus `acceptable_acl`.
2. The trust walk opens each component first, with `O_NOFOLLOW | O_DIRECTORY`, and inspects it with `fstatat` only when the open refuses it, which is how symlinks are found. The alias rules, the re-check and the error messages are unchanged.
3. Generation records and segments use one status call for the shape check plus the ACL, in place of up to two `fstat` calls and an ACL fetch.

Measured against an identical probe at `Detent:861d652`, interleaved, `quiet exclusive`, 80 cold samples per arm, two rounds:

| arm | round 1 median | round 2 median | below 1 ms |
|---|---|---|---|
| base | 1225 µs | 1201 µs | 12/80, 16/80 |
| this fix | 1145 µs | 1167 µs | 19/80, 23/80 |
| plus no re-checks (experiment) | 1099 µs | 1096 µs | 29/80, 29/80 |
| plus prefaulted `__TEXT` (experiment) | 1042 µs | 1020 µs | 29/80, 32/80 |

Warm, 500 fresh `Client`s per arm over three rounds: 532, 531 and 542 µs base against 507, 524 and 487 µs with the fix.

The fix is a real reduction of about 5%, roughly 30 to 80 µs per first read. It does not meet T2 on its own.

Verification:

1. Full `ctest`, Release, 179 tests: 178 pass.
   - `test_local_read_counters` fails, identically at `Detent:861d652`. Its line 47 expects the old layout's 2 segments and 5 pages, and dense pages touches 1 and 1. It needs updating on the quest branch.
   - `test_local_trusted_paths` requires root and is skipped. Run it as root before merging this commit.
2. An equivalence probe calls `open_publisher_directory` at base and at this commit over 10 cases: plain, missing, a file component, group- and world-writable, allow and deny-only ACLs, a user-owned symlink, `..` and a relative path. Every verdict and every error message is identical.

## What would meet the gate (lead decisions)

The gate needs about 350 to 400 µs off the cold median, plus headroom for the tail.

1. **Drop the name re-checks** (about 55 µs measured). Both the trust walk and `open_mapping` `fstatat` the name after opening it, to confirm it still refers to the same inode. Every check and every later open goes through the descriptor, and pages are SHA-verified against their references, so the re-check guards nothing the descriptor chain does not already guard. It is still a security-policy change, so it is not on this branch.
2. **Open fewer files per first read** (about 50 µs cold per file avoided). A first read opens 5 generation records and 3 to 4 segments.
   - Folding `selected`, the lease, `storage.meta` and the reader metadata into fewer records saves one open each.
   - A dense placement rule that keeps the top levels of the trie in one segment would cut the 3 to 4 segment opens toward 1 or 2. Dense pages owns that placement decision now (D-rules, T2 and T8 identity).
3. **Shorten the trusted path** (about 21 µs per component cold). Either measure at a production-depth path, or anchor trust at a root-owned store parent so that the walk does not start at `/`. Design D2 decides this.
4. **Code page-in** (about 100 to 170 µs). An order file grouping the first-read path, or a smaller first-read code path, reduces the roughly 60 text faults. Prefaulting in the probe is a diagnostic, not a fix, because it moves the cost before the timer.
5. **SHA-256** (about 60 µs warm). This CPU has no SHA extensions, so it runs the portable kernel. Kronuz/sha256 `sha256:35e8639` (local, not pushed) adds an AVX2 kernel: 1.4× the portable kernel at 64 B and 1.6× on bulk data on this host. That would save roughly 20 to 25 µs per first read once Detent picks it up.
6. **Host conditions.** Record the host load and power state with every T2 run. The same baseline binary measured a 715 µs median on 2026-10-26 and 1031 to 1236 µs today.

## Reproduce

- Probe and driver: `.scratch/dense-t2/first.py PROBE STORE --rounds N` in the main checkout. It uses `dense_reader_measure.py`'s cases and warmup procedure.
- Builds: `.scratch/dense-t2/build` (this branch) and `.scratch/dense-t2/build-base` (`Detent:861d652` with the same probe). Both are Release with `-g`, Apple clang, read counters on.
- Store: `~/.scratch/campaign/jobs/261106-113938-0f7177/a1/out/candidate/store-1ns`.
