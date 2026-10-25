# Large HTTP publication deadline investigation

**Date:** 2026-10-25. **Status:** Focused controls attribute deadline pressure to work preceding cleanup; fixture allowance corrected, composed qualification pending.

The recovery candidate's [Intel macOS job](https://github.com/Kronuz/Detent/actions/runs/40183675919/job/121139836233) misses the 500K publication-feedback deadline. At the deadline, the snapshot is received and validated, publication is acknowledged at revision 1, recovery still reports revision 0, and the phase is `Discarding`. The worker is not pending. Closing then settles the coordinator within a few milliseconds and sends a session Close instead of publication feedback.

This sample does not establish a cleanup stall. It lacks publication and discard entry times. Publication may consume most of the allowance. Source review finds monotonic disposal credits and no shared-pointer uniqueness wait in staging, image-builder or decoded-snapshot cleanup. Closing the bundle drops different handles from its staging slot, so rapid shutdown does not prove a dependency released by fencing.

## Discriminator

The next profile records acknowledgement, discard and recovery timestamps. Each cleanup part reports slice count, charged work bytes, completion time and the maximum interval between resumed slices. The order is publication staging, image builder, then decoded input. Charged bytes describe work-budget consumption, not physical memory released; a resume interval includes both preceding work and scheduling.

The manual coordinator profile matches the shared matrix's Intel macOS runner, LLVM compiler, SDK, optimization, assertions and libc++ extensive hardening. It repeats the real 500K HTTP fixture with independently captured driver logs. The existing timeout remains in force.

Late discard entry points to preceding work. Many charged slices point to cleanup/scheduling cost. Repeated slices without charged work point to a nonprogress branch. A long resume interval needs finer work-versus-executor profiling. If ownership is suspected, a deterministic small-budget cleanup fixture must retain relay/cache handles and complete without fencing.

Twenty concurrent native controls pass before this instrumentation; three instrumented native controls pass. These controls do not replace the failing matrix profile or establish the cause.

## Exact-profile control

[Run 40204684340](https://github.com/Kronuz/Detent/actions/runs/40204684340), source `522c353`, passes 20 repetitions across two concurrent drivers. Driver totals are 546.86 and 547.36 seconds. Individual cases span 47.11–59.30 seconds against the 60-second fixture deadline. Native controls are substantially shorter, so they do not reproduce this runner cost. The successful run captures only failure output and therefore does not establish the publication-versus-cleanup split.

The next focused run retains successful phase traces and verbose CTest output. A deadline adjustment requires evidence that cleanup progresses and that the allowance, rather than a stalled state transition, accounts for the failure. The original failed matrix remains a qualification failure.

## Successful phase attribution

[Run 40206466162](https://github.com/Kronuz/Detent/actions/runs/40206466162), source `e982b66`, passes four further cases across two drivers with successful traces retained. Acknowledgement occurs at 42,362–50,320 ms; discard starts in the same millisecond. Recovery begins 1,290–1,401 ms later. Each case completes the builder cleanup in 5,005 slices charging 41,000,267 work bytes and decoded-input cleanup in 6,854 slices charging 56,139,904 work bytes. The sink stage finishes in one slice. These counts match the native control. The largest observed cleanup resume interval is about 105 ms.

The native control acknowledges at 8,740 ms and begins recovery 214 ms later. The remote controls therefore show substantial pre-cleanup time and progressing disposal, not a shared-owner wait. They cannot reconstruct the missing phase timestamps in the original failure or identify which earlier operation consumed its time.

The 500K relay-HTTP fixture now allows 120 seconds for publication feedback and 180 seconds for the complete CTest case. Smaller fixtures retain their existing bounds. Production timeouts, budgets, cleanup order and functional assertions are unchanged. This allowance provides headroom above the observed 59.30-second passing case; it is not a throughput claim or a production performance fix. Keep phase traces so later failures remain attributable.
