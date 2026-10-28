# Initial release qualification

**Updated:** 2026-10-27. **Status:** Stage 6 active. The owner includes the remaining reusable-library extractions before release.

Use the [extraction checklist](EXTRACTION_CHECKLIST.md) to resume library work from a clean session. It separates remaining adoption from conditional candidates and points to existing implementations and standalone handoffs.

## Remaining release checklist

Stage 6 contains the following exits. Each stays open until its recorded evidence passes. New optimizations need a demonstrated blocker against one of these exits; they do not become additional release requirements automatically.

| Exit | Remaining work | Closure |
| --- | --- | --- |
| RC-1: Required integration | EX-1 resources, EX-2 HTTP, EX-3 bounded MessagePack, EX-4 persistent map; CL-2 owner-mode forwarding; finalized consensus transport; asynchronous authority/relay HTTP log completion | Reviewed public pins, deleted private engines, compatible bytes and failure/lifetime checks. Record each component separately. |
| RC-2: Client performance and updates | First-read repeatability, incremental refresh and retained-reader behavior; daemon startup attribution | Preserve the sub-millisecond resident-data first-read target and record the measured limits. Full daemon preparation is measured separately from client attachment. An optimization needs an attributed cost and a controlled comparison. |
| RC-3: Sustained capacity | Ingestion calibration, raw/zstd 500/2k/10k-session ladder, traffic/retry amplification and mixed-pressure soak; sustained 5M qualification | Publish an operating envelope supported by the actual topology and host. Preserve resource refusals. The exploratory 10M trial depends on available resources and does not silently raise the supported ceiling. |
| RC-4: Failure and restoration | Fresh three-voter restore under a new epoch; combined failures, receipts and resource conservation | Complete-state and durable recovery checks pass with no unresolved release-blocking defect. |
| RC-5: Composed release candidate | Final supported-platform/sanitizer matrix, clean installation, service lifecycle and upgrade/rollback | Qualify the integrated source and exact public dependency closure, then document installation and measured limits. |

EX-0 flat-map adoption and the prepared-reader activation component are complete. Their existing evidence is retained in the final composed checks. The rejected btree rewrite, conditional extractions without a second consumer, new storage frameworks and speculative tuning add no release requirement.

Work proceeds serially. A failed component gate blocks that component's landing. Preserve its candidate and evidence, then advance an independent required component when further work awaits a decision or host access. The active resource lookup diagnostic is limited to one 20-key paired screen; it either supports a specific fix or records the unresolved attribution before HTTP reconciliation continues. No unchanged performance retries are used to obtain a passing sample.

The active [qualification index](status/README.md) tracks the remaining gates. Public flat-map adoption, recovery/delivery fixes and the opt-in prepared reader are on main. The reduced three-voter/one-relay/one-agent topology verifies every persisted knob in the 5M image. Main's corrected source passes all six enabled hosted jobs. The [fixture record](reviews/hosted-reader-fixtures-2026-10-26.md) separates the 500K Intel macOS fixture correction from production behavior.

Managed prepared-reader activation satisfies the [component review](reviews/prepared-activation-component-2026-10-26.md) conditions: all six enabled hosted jobs pass, the complete managed FreeBSD native profile passes 170 checks, and nine focused FreeBSD sanitizer checks pass. [Real 5M preparation and restart](reviews/prepared-managed-qualification-2026-10-26.md) pass, as do 400 fresh SDK reads and 80 CLI checks. All 200 resident-data reads with the publisher running return below 1 ms. NFS4-enabled UFS/ZFS, cold storage and broader release gates remain outside this acceptance. No first release is claimed.

That 200/200 result describes the recorded activation trial, not a repeatability guarantee. The subsequent resource-adoption comparison records 196/200 baseline and 195/200 migrated resident-data first reads below 1 ms. Resource adoption remains blocked pending attribution; the sub-millisecond goal is unchanged. The [extraction checklist](EXTRACTION_CHECKLIST.md) links the complete results and independent NO-GO ruling.

The [5M exploratory trial](CAPACITY_QUALIFICATION.md) completes replicated seeding but fails original agent convergence. A reduced candidate topology with three voters, one relay and one agent reaches durable selection and passes the complete 5M SDK check. The two-relay/two-agent control reaches a host resource guard. These results do not establish sustained capacity. Complete the primary 5M qualification and attempt 10M if resources permit, preserving refusals and interrupted runs as results.

The initial release supports a regional fixed three-voter authority, durable relays and host-local last-known-good readers. Implementation acceptance for earlier stages does not establish sustained capacity, complete restoration or deployment qualification. Release requires the gates below; passing short transport probes does not certify a fleet of durable agents.

| Component | Current evidence | Required exit |
| --- | --- | --- |
| Updated libraries A–E | Compatible pins adopted with reviewed footprint exception; corrected-layout sanitizer gate and 214 distinct native tests across two invocations pass | Final composed product/profile qualification after remaining extractions |
| Retained-state resources | Paired library source published with green CI; Detent Linux/FreeBSD host tests and corrected-layout macOS sanitizer tests pass | Default footprint and final transport dependency closure |
| Shared library extraction | Resources published with 24 green CI jobs; isolated HTTP held-session native/sanitizer gates pass; bounded MessagePack passes 189 functional and three additional network cases, Linux/FreeBSD library sanitizers and focused Xapiand compatibility | Shared HTTP clients, bounded MessagePack and persistent map integrated without parallel implementations; final application qualification |
| Container correctness | Public flat-map pin adopted; full native suite and six-job matrix pass; rejected btree lifetime repair remains experimental | Keep container evidence in the final composed qualification; no further btree rewrite is required for this release |
| Recovery and delivery | Decoder reclamation pacing and active-session idle fixes are merged; native suite, six-profile matrix and complete reduced-topology 5M SDK verification pass | Keep larger-topology and sustained qualification separate |
| Final client attachment | Managed prepared-reader default passes real 5M preparation/restart and 400 fresh SDK checks, with live resident-data median 0.715 ms and p99 0.835 ms | Retain cold-storage, incremental-refresh and source-sharing qualification; profile the remaining dataset-wide daemon startup cost |
| Daemon and HTTP logs | Shared severity tint/color-depth controls and classic distributor HTTP metadata adopted; clean public-pin build, functional checks and bounded logging-cost comparison complete | Complete the asynchronous authority/relay HTTP completion observer and composed-product checks |
| Consensus/journal adoption | Coordinated restart directly into finalized v2 agreed | Track C transport complete, coherent pins, old-state recovery, mixed-version refusal and rollback qualification |
| Sustained capacity | Reduced functional runs and short 10k-session evidence | Calibrated ingestion, raw/zstd 500/2k/10k session ladder, per-hop traffic and retry amplification, mixed pressure and soak at the supported operating point |
| Failure and restoration | Failure tooling and restore design available | Fresh three-voter restore under a new epoch; combined failure, receipt, complete-state and resource conservation checks |
| Packaging and product contract | Profiles and deployment documentation exist | Clean installation/build profiles, service lifecycle, upgrade/rollback and documented measured limits |

Finish dependency-only comparisons before mixing extraction changes into their application source. Extract one mechanism at a time into its existing library where one fits; create a small library only when the catalog has no suitable owner. Preserve public interfaces and valid wire/disk bytes, and update the cpp-libraries index with each extraction. Generic daemon plumbing needs a demonstrated shared consumer; otherwise retain it in Detent and record that decision.

The [shared resource blueprint](RESOURCE_EXTRACTION_ARCHITECTURE.md) establishes optional targets in `allocators`. The resource libraries are published; isolated Detent integration removes its private allocation, retirement and DiskLedger accounting bodies. The [HTTP client blueprint](HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md) defines the next component, followed by the [bounded MessagePack component](MSGPACK_EXTRACTION_ARCHITECTURE.md) and the persistent map. The bounded `msgpack` target must configure without fetching its DOM dependencies; client-only HTTP targets must preserve optional server and TLS dependencies. Each component removes the migrated private implementation and receives one complete review.

Resource extraction preserves the difference between automatic memory refunds and explicit durable disk settlement. Task scheduling, mapped-file residency and transfer helpers are reuse evaluations, not blanket replacements for Detent's protocol and ownership state machines. No release requires inventing a general framework around application-specific machinery.

Use complete-component architecture reviews and final implementation reviews rather than per-commit approvals. Track C may finish transport while Detent completes independent extractions; intermediate v2 revisions remain excluded from adoption. Final integrated qualification follows all selected source and pin changes.

The container gate is closed by the public flat-map adoption. The [comparison report](CONTAINER_SELECTION_REPORT.md) preserves the rejected btree approaches and measured trade-offs. The active gates are fast final-client attachment, measured ingestion and bootstrap costs, and the remaining shared-library integration. The owner-loop scheduling blueprint exists on the recovery candidate; the [grouped storage-barrier blueprint](STORAGE_BARRIER_ARCHITECTURE.md) defines a proposed completion extension. Neither optimization is implemented or performance-qualified. See [dependency qualification](DEPENDENCY_UPGRADES.md) for immutable source attribution and adoption boundaries.

The [Stage 6 blueprint](STAGE6_ARCHITECTURE.md), [adoption blueprint](LIBRARY_ADOPTION_ARCHITECTURE.md), [retained-state blueprint](RETAINED_STATE_ARCHITECTURE.md), [library boundary audit](LIBRARY_BOUNDARIES.md) and [fleet gates](FLEET_QUALIFICATION_ARCHITECTURE.md) define the detailed checks. Independent failure-domain, device power-loss and physical fleet certification require suitable hardware; state those limits explicitly in the release contract.
