# Linux completion storage

The authority now selects the owned completion profile on Linux as well as macOS and FreeBSD. The reusable queue lives in the sibling cluster journal module; Detent keeps consensus and application state on its Asio owner executor. The on-disk format, legacy cluster protocol and synchronous public APIs are unchanged.

```text
Owner coroutine → Owned primitive → io_uring or bounded fallback
                       ↓                    ↓
               Retained operation ← Original completion
                       ↓
            Final barriers → Owner settlement
```

Set `"storage_backend": "native"` in the authority configuration to require successful io_uring setup and its read/write/fsync capabilities. The default `"auto"` permits bounded fallback when setup is unavailable, reporting the setup errno. `"fallback"` deliberately disables the ring for comparison or restricted environments. Explicit policy selection currently applies to Linux; other platforms accept the default auto policy. Requiring native setup does not eliminate validated composite fallbacks.

Read/write, full-file fsync and directory fsync use native completion. Rename/unlink use native completion when supported by the running kernel. Composite file opening/creation/type/device validation and directory iteration retain their existing checks on one bounded worker. `IOSQE_ASYNC` can use kernel workers; it keeps filesystem execution out of application owner turns without promising a filesystem latency bound. Startup/recovery remain synchronous before admission.

Health reports native/fallback submissions and completions, completed bytes, notification errors, maximum outstanding work, and per-primitive submission arrays. Linux additionally reports `io_native_available`, `io_setup_error` and `io_ring_errors`. Array order is Write, Sync, Create, Replace, DirectorySync, Read, Open, Size, Scan, Next, OpenCandidate, Remove. A missing capability or composite limitation selects fallback before submission; an execution error never reruns the operation through another backend.

Qualification uses a local QEMU guest with Ubuntu 24.04, Linux `6.8.0-142-generic`, ext4, a virtio qcow2 overlay and QEMU `cache=none`. The image SHA-256 matches Ubuntu's published checksum. Qualification passes 42 Detent tests, 11 cluster tests and five standalone journal suites natively. ASan/UBSan passes 41 Detent tests plus the added native snapshot profile, all 11 cluster tests and five journal suites. The real three-process TLS authority fixture requires native setup; the three-voter transport fixture also exercises nonempty snapshot catch-up through completion-driven storage. The macOS regression gate passes all 42 Detent tests, with targeted completion and transport sanitizer checks. Guest measurements describe this virtual disk and CPU; process-kill and guest-crash recovery do not certify host power-loss durability.

The kernel interface is intentionally narrow: one accepted original primitive, one reserved completion slot, ordinary rings and eventfd notifications. Published entries retain their operation, buffers, files and directory owner across enter failures and cancellation. Only matching reaped completion advances the shared mutation states. Final directory/file barriers precede quota and frontier settlement. Ordinary reaped IO errors fence storage normally. A fatal ring or completion-ownership fault preserves unresolved original resources and has fail-stop semantics: destroying a queue with accepted work terminates the process. A subprocess test of non-transient enter failure and permanently invalid completion ownership remains a hardening gate; current tests exercise transient enter errors and recoverable injected identity mismatch.

The Linux transport build exposed missing direct algorithm declarations and fixture assumptions about directory umask and ripgrep. Those fixes are committed separately and recorded in [the audit](AUDIT.md). Reactor `6f68823` also establishes listener readiness before a successful start returns; the HTTPS test now makes an immediate connection instead of retrying startup.

## Reproducible comparison

The [recorded samples](../benchmarks/linux-completion-baseline.json) contain three alternating native/fallback pairs, each with a fresh private journal, 200 appends and 4 KiB payloads. Build the sibling journal's `completion_bench` target in Release mode, then run `./completion_bench native 200` and `./completion_bench fallback 200` alternately on the same otherwise idle filesystem. The fixture waits for completion using eventfd polling; it measures a single journal rather than the authority's Asio service or fleet throughput.

| Median measurement | Native | Forced fallback |
| --- | ---: | ---: |
| Wall time for 200 appends | 1126.24 ms | 1102.45 ms |
| Process CPU | 325.907 ms | 324.098 ms |
| Owner-thread CPU | 134.259 ms | 128.443 ms |
| Append p50 | 5.56174 ms | 5.43621 ms |
| Append p95 | 6.22813 ms | 6.24716 ms |
| Append p99 | 6.69053 ms | 6.80966 ms |
| Peak process RSS | 3840 KiB | 3840 KiB |

Native completion is about 2.2% slower in median wall time here; CPU differences are within the observed variation. This sample establishes no throughput, CPU, cost or power saving. Both modes keep filesystem execution off the owner; the native mode completes 1400 native primitives and 200 composite fallbacks per run, compared with 1600 fallback primitives. Both write exactly 834400 bytes. The fixture's synthetic timer probe does not establish production owner-turn latency, and process CPU is not a whole-host energy measurement.

The consolidated high-effort Architect review found no blocking durability, ownership or compatibility defect in this component. FreeBSD native qualification, subprocess fail-stop fixtures, adversarial service overload and fleet capacity remain separate gates.
