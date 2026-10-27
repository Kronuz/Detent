# Prepared activation component review

**Date:** 2026-10-26. **Scope:** Complete isolated component through production `Detent:0631d1d`, documentation `Detent:02ecefe`, against [the validated blueprint](../PREPARED_ACTIVATION_ARCHITECTURE.md). Main `Detent:205ece1` contains the opt-in reader and was merged into this branch; activation has not landed on main. **Verdict:** Source GO; no concrete P1/P2 blocker remains. Managed default activation is conditional on the three qualification gates below. This review runs no builds or tests and changes only this file.

## Source closures

1. [Coordinator readiness](../../src/detent_delivery/coordinator.cc#L1231) uses owner-held `confirmed_prepared`, assigned after recovery settlement and applicable publication confirmation. It requires the exact Known selected descriptor and eligible lifecycle, without reading worker-mutated results. The pause regression checks false readiness after result emplacement and before metadata probing, then closes/drains. Startup separately proves readiness after confirmed preparation. Supplied native, ASan/UBSan and TSan results each pass 5/5.
2. [Managed readiness](../../src/detent_agent/managed_host.cc#L17) distinguishes explicit Absent empty-store liveness from Uncertain replacement. Known selection requires a descriptor and, in prepared mode, confirmed preparation. Initialization and terminal-state guards apply in both modes. Native and sanitizer readiness/timeout checks pass 2/2.
3. Metadata probing uses the protected walker and recovered-directory identity. Foreground timeout closes and drains before reporting failure; CLI allowance starts before launch. Idle reconciliation preserves preparation evidence, with stable selection/readiness across two failed source rounds. Same-cursor republish, caller caps, explicit UID trust and corruption refusal remain within the accepted mechanism.

## FreeBSD corrections

[ACL policy](../../include/detent/config_trust_internal.hh#L36), corrected in `Detent:ad94e91`, permits mode-only trust only after unsupported ACCESS retrieval and exact-zero EXTENDED/NFS4 capability results on the same descriptor. Other retrieval errors, enabled capabilities and negative queries refuse. Successful ACL retrieval still requires inspected triviality and successful cleanup. This matches [FreeBSD 14.4 UFS capability semantics](https://github.com/freebsd/freebsd-src/blob/releng/14.4/sys/ufs/ufs/ufs_vnops.c#L2577).

The failure-policy fixture covers both policies, all nine capability pairs from {-1,0,1}, and retrieval errors without capability queries. The real default-UFS probe records both capabilities zero and both ACL retrievals unsupported. The private 32 MiB POSIX-ACL UFS fixture records EXTENDED=1/NFS4=0, accepts trivial paths, exercises aliases/replacement races, and refuses a named-user read grant despite no group/other write bits. Mount/device cleanup succeeds.

[Legacy configuration reopening](../../src/detent_cli/config.cc#L119) recognizes FreeBSD's [documented EMLINK](https://github.com/freebsd/freebsd-src/blob/releng/14.4/lib/libc/sys/open.2#L571) alongside ELOOP. Crucially, `no_symlink` remains false: ordinary legacy fields load, while trusted reader fields still refuse. The existing reader-capacity regression covers both outcomes. All nine focused native FreeBSD checks pass in 1.00 second.

## Evidence and activation gates

The supplied managed profile passes 164/164 checks; privileged path fixtures pass without skips across six hosted configurations. The recovery-pressure fixture correction passes 100 native and 20 sanitizer repetitions; controlled planner refunds demonstrate its unstable nominal-budget oracle without proving the earlier ARM assertion's identity.

[Exclusive macOS 5M qualification](prepared-managed-qualification-2026-10-26.md) passes preparation/restart in 117.609/108.307 seconds, preserving revision 39064, 1,441 packs and the restart descriptor. All 400 SDK and 80 CLI checks pass. Resident first-read samples meet 1 ms for 200/200 live and 198/200 stopped clients, with zero major faults. Timing includes Client construction, excludes executable loading and follows per-key warmup. These results belong to `Detent:0e862f0`; they are not repeated FreeBSD measurements.

Activation requires:

1. Successful terminal [hosted matrix 40439430608](https://github.com/Kronuz/Detent/actions/runs/40439430608) for the composed source.
2. Successful full FreeBSD managed native profile.
3. Successful nine-check FreeBSD ASan qualification, including non-skipped private-filesystem trust coverage.

After these gates pass, no additional component approval is required to land and activate the unchanged reviewed source. NFS4-enabled UFS/ZFS, cold storage, shared source contexts, sustained/fleet load and broader release qualification remain open. Existing manual IO cuts do not establish complete network/process-crash coverage. This verdict establishes neither universal latency guarantees nor release readiness.
