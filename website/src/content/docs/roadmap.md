---
title: "Roadmap"
---

Detent is a work in progress with a complete architecture and a careful,
staged implementation. This page states plainly what works today, what's in
progress, and what's still missing. Items marked as unimplemented elsewhere in
these docs are collected here.

---

## Status at a Glance

| Area | Status |
| --- | --- |
| Client library: reads, evaluation, file and HTTP sources | **Done**, covered by golden vectors, precedence tests and concurrency stress |
| Classic distributor: PUT/PATCH/GET, persistence, diffs, compression | **Done** |
| Classic Raft clustering | **Done at small scale**: real 3-node tests and an end-to-end chaos harness |
| Host-local agent and `detent get` | **Done** |
| Python and Rust bindings | **Basic**: file source only |
| Durable authority | **Component accepted**; fleet-scale qualification open |
| Managed delivery and SDK source | **Component accepted** |
| Resumable subscriptions, relays, incremental publication | **In progress** (Stage 5) |
| Sustained system, fleet and failure qualification | **Pending** (Stage 6) |

---

## Implementation Stages

| Stage | Deliverable | State |
| --- | --- | --- |
| 0 | Architecture, contracts and failure assumptions | Accepted |
| 1 | Legacy Raft safety corrections | Accepted; Xapiand compatibility retained |
| 2 | Durable storage and strict consensus | Accepted |
| 3 | Bounded transaction API and authority runtime | Accepted |
| 4 | Portable distribution and immutable local views | Accepted, including managed agent and SDK |
| 5 | Resumable delivery, subscriptions, relays, incremental publication | 5A and 5B components accepted; 5C incremental publication in implementation |
| 6 | Sustained system, fleet and failure qualification | Pending |

The planning workload for stages 5 and 6 is 500,000 knobs, 300 concurrent
publishers, 1,000 transactions per second and 50,000 agents. Those are test
targets, not demonstrated capacity.

---

## Build and Release

1. **Pin dependency revisions** instead of following `main`.
2. **Tagged releases** with a changelog, and a version reported by the binaries.
3. **Continuous integration** on Linux, macOS and FreeBSD, including the
   sanitizer runs.
4. **Packages**: Homebrew bottles (the tap builds from source today),
   Debian/RPM, FreeBSD ports, container images, a Python wheel and a Rust
   crate.

## Knob Model

1. String, float and structured (JSON) knob types.
2. Group overrides for int knobs.
3. Knob metadata: owner, description, creation date and expiry, with stale-flag
   reporting.
4. Rule-based targeting beyond exact group names, such as attribute matching
   or segments.

## Client and Bindings

1. A shipped command-line flag parser for CLI overrides
   (`--just-knob name=value`) and a config-path flag.
2. An opt-in environment switch that freezes baked defaults for tests.
3. Filesystem notifications for the file source instead of metadata polling.
4. Python: HTTP source, `read_view()`, `check()`, category-specific exceptions,
   type stubs.
5. Rust: HTTP source, views, structured errors, test guards.
6. Go and Java bindings, and a documented stable reader ABI for the shared
   snapshot.
7. A change-notification API for applications that want to react to updates.

## Distribution

1. Resumable subscriptions with snapshot/history handoff and durable publication feedback have Stage 5A component acceptance. Native, sanitizer and focused supported-platform checks establish component behavior; sustained fleet capacity remains a Stage 6 qualification target.
2. Regional relays for fan-out have Stage 5B component acceptance; sustained fleet qualification remains in Stage 6.
3. Incremental page publication, warm recovery, shared-segment reclamation, migration and bounded maintenance have private-component source GO and affected native/platform/sanitizer qualification. Opt-in agent/relay format configuration is under assembled-process qualification; sustained pressure and fleet capacity remain in Stage 6.
4. Multi-region replication for the classic distributor.
5. Classic `/v1/snapshot` serving from the authority, or a migration path for
   unmanaged agents.

## Operations and Observability

1. A metrics endpoint (Prometheus/OpenMetrics) for the daemons, and an exporter
   for client counters.
2. A versioned, documented schema for the authority's health document.
3. Audit history: who changed which knob when, with the ability to list and
   revert past versions.
4. A publishing CLI (`detent set`, `detent publish`) so operators don't need
   hand-written `curl`.
5. A web UI for browsing and editing knobs.
6. Dry-run validation of a snapshot or transaction without publishing it.

## Qualification

1. Real network conditions, not only loopback.
2. Multi-hour and multi-day soak tests.
3. Fleet-scale fan-out at 1,000, 10,000 and 50,000 agents on enough hosts.
4. Power-loss and filesystem-level durability on target hardware.
5. Combined pressure/churn and whole-service telemetry overhead for the
   authority.

The shared-segment collector, warm recovery, mapped admission and complete private paged delivery pass consolidated source review. Final affected Linux, FreeBSD, ASan/UBSan and TSan gates pass. Two native parallel 500k fixture failures pass in separate and CTest serial repeats; controlled-contention attribution remains open. Production configuration and complete fleet qualification remain separate gates.
