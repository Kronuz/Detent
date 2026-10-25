# Large HTTP publication deadline investigation

**Date:** 2026-10-25. **Status:** Cause unassigned; targeted Intel macOS diagnostics pending.

The recovery candidate's [Intel macOS job](https://github.com/Kronuz/Detent/actions/runs/40183675919/job/121139836233) misses the 500K publication-feedback deadline. At the deadline, the snapshot is received and validated, publication is acknowledged at revision 1, recovery still reports revision 0, and the phase is `Discarding`. The worker is not pending. Closing then settles the coordinator within a few milliseconds and sends a session Close instead of publication feedback.

This sample does not establish a cleanup stall. It lacks publication and discard entry times. Publication may consume most of the allowance. Source review finds monotonic disposal credits and no shared-pointer uniqueness wait in staging, image-builder or decoded-snapshot cleanup. Closing the bundle drops different handles from its staging slot, so rapid shutdown does not prove a dependency released by fencing.

## Discriminator

The next profile records acknowledgement, discard and recovery timestamps. Each cleanup part reports slice count, charged work bytes, completion time and the maximum interval between resumed slices. The order is publication staging, image builder, then decoded input. Charged bytes describe work-budget consumption, not physical memory released; a resume interval includes both preceding work and scheduling.

The manual coordinator profile matches the shared matrix's Intel macOS runner, LLVM compiler, SDK, optimization, assertions and libc++ extensive hardening. It repeats the real 500K HTTP fixture with independently captured driver logs. The existing timeout remains in force.

Late discard entry points to preceding work. Many charged slices point to cleanup/scheduling cost. Repeated slices without charged work point to a nonprogress branch. A long resume interval needs finer work-versus-executor profiling. If ownership is suspected, a deterministic small-budget cleanup fixture must retain relay/cache handles and complete without fencing.

Twenty concurrent native controls pass before this instrumentation; three instrumented native controls pass. These controls do not replace the failing matrix profile or establish the cause.
