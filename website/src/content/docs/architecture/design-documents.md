---
title: "Design Documents"
---

Detent's engineering record lives in the repository, next to the code. These
documents are written for contributors: they record decisions, acceptance
gates, measurements and audit findings in more depth than this site.

---

## Specification

| Document | What it covers |
| --- | --- |
| [justknobs-implementation-spec.md](https://github.com/Kronuz/Detent/blob/main/justknobs-implementation-spec.md) | The original client specification Detent implements, with sourced facts and filled blanks marked |

## Architecture Blueprints

| Document | What it covers |
| --- | --- |
| [VALIDATED_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/VALIDATED_ARCHITECTURE.md) | The reviewed large-state architecture and its stage gates |
| [SCALING.md](https://github.com/Kronuz/Detent/blob/main/docs/SCALING.md) | Capacity model, the 100k/500k-knob baseline, and the scaling plan |
| [AUTHORITY_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/AUTHORITY_ARCHITECTURE.md) | The bounded authority application and runtime |
| [ASYNC_AUTHORITY_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/ASYNC_AUTHORITY_ARCHITECTURE.md) | Authenticated multi-node transport and completion-based I/O |
| [AUTHORITY_TRANSPORT.md](https://github.com/Kronuz/Detent/blob/main/docs/AUTHORITY_TRANSPORT.md) | Peer hello, framing, TLS and fixed-voter configuration |
| [AUTHORITY_READINESS.md](https://github.com/Kronuz/Detent/blob/main/docs/AUTHORITY_READINESS.md) | Checkpoint ownership, admission, telemetry and qualification |
| [AUTHORITY_EXPORT.md](https://github.com/Kronuz/Detent/blob/main/docs/AUTHORITY_EXPORT.md) | Scoped export, change history and the distribution endpoint |
| [DISTRIBUTION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/DISTRIBUTION_ARCHITECTURE.md) | Portable distribution and immutable local publication |
| [DISTRIBUTION_CODEC.md](https://github.com/Kronuz/Detent/blob/main/docs/DISTRIBUTION_CODEC.md) | The codec comparison behind the MessagePack choice |
| [DELIVERY_INTEGRATION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/DELIVERY_INTEGRATION_ARCHITECTURE.md) | Authenticated authority-to-local-view delivery |
| [SUBSCRIPTIONS_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/SUBSCRIPTIONS_ARCHITECTURE.md) | Stage 5A sessions, snapshot/history handoff and durable publication feedback; implementation in progress |
| [LOCAL_IMAGE_FORMAT.md](https://github.com/Kronuz/Detent/blob/main/docs/LOCAL_IMAGE_FORMAT.md) | The generation image and descriptor formats |
| [DISK_LIFECYCLE_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/DISK_LIFECYCLE_ARCHITECTURE.md) | Durable ownership, census and lease-protected GC |
| [COMPLETION_ALLOCATION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/COMPLETION_ALLOCATION_ARCHITECTURE.md) | Physical admission through completion controls |
| [src/detentd/CLUSTERING.md](https://github.com/Kronuz/Detent/blob/main/src/detentd/CLUSTERING.md) | The classic Raft clustering design |

## Status and Evidence

| Document | What it covers |
| --- | --- |
| [IMPLEMENTATION.md](https://github.com/Kronuz/Detent/blob/main/docs/IMPLEMENTATION.md) | Stage-by-stage implementation status |
| [AUDIT.md](https://github.com/Kronuz/Detent/blob/main/docs/AUDIT.md) | The ongoing source audit and every fix it produced |
| [AUTHORITY_COMPONENT_REVIEW.md](https://github.com/Kronuz/Detent/blob/main/docs/AUTHORITY_COMPONENT_REVIEW.md) | The consolidated authority review |
| [LINUX_COMPLETION.md](https://github.com/Kronuz/Detent/blob/main/docs/LINUX_COMPLETION.md) | io_uring storage qualification |
| [`benchmarks/*.json`](https://github.com/Kronuz/Detent/tree/main/benchmarks) | Raw measurement artifacts |

## Operations

| Document | What it covers |
| --- | --- |
| [USAGE.md](https://github.com/Kronuz/Detent/blob/main/docs/USAGE.md) | Deployment scenarios, end to end |
| [SERVICES.md](https://github.com/Kronuz/Detent/blob/main/docs/SERVICES.md) | Service accounts, directories and supervision |
| [LOGGING.md](https://github.com/Kronuz/Detent/blob/main/docs/LOGGING.md) | Daemon logging and its measured footprint |
