# Initial release qualification

**Updated:** 2026-10-18. **Status:** Stage 6 active. The owner includes the remaining reusable-library extractions before release.

The initial release supports a regional fixed three-voter authority, durable relays and host-local last-known-good readers. Implementation acceptance for earlier stages does not establish sustained capacity, complete restoration or deployment qualification. Release requires the gates below; passing short transport probes does not certify a fleet of durable agents.

| Component | Current evidence | Required exit |
| --- | --- | --- |
| Updated libraries A–E | Native coverage and paired samples captured; seven-pair collection repeat stays within the CPU review threshold | Qualify daemon CPU and footprint, then adopt immutable pins |
| Retained-state resources | Paired library source reviewed, published and green in both eight-job CI runs; Detent Linux/FreeBSD host tests pass | Complete Detent sanitizer qualification, default footprint and final transport dependency closure |
| Shared library extraction | Source audit recorded in `LIBRARY_BOUNDARIES.md` | Shared HTTP client mechanics, bounded MessagePack, persistent map and resource primitives integrated without parallel implementations; focused compatibility and performance evidence |
| Consensus/journal adoption | Coordinated restart directly into finalized v2 agreed | Track C transport complete, coherent pins, old-state recovery, mixed-version refusal and rollback qualification |
| Sustained capacity | Reduced functional runs and short 10k-session evidence | Calibrated 500k-key ingestion, raw/zstd 500/2k/10k session ladder, mixed pressure and soak at the supported operating point |
| Failure and restoration | Failure tooling and restore design available | Fresh three-voter restore under a new epoch; combined failure, receipt, complete-state and resource conservation checks |
| Packaging and product contract | Profiles and deployment documentation exist | Clean installation/build profiles, service lifecycle, upgrade/rollback and documented measured limits |

Finish dependency-only comparisons before mixing extraction changes into their application source. Extract one mechanism at a time into its existing library where one fits; create a small library only when the catalog has no suitable owner. Preserve public interfaces and valid wire/disk bytes, and update the cpp-libraries index with each extraction. Generic daemon plumbing needs a demonstrated shared consumer; otherwise retain it in Detent and record that decision.

The [shared resource blueprint](RESOURCE_EXTRACTION_ARCHITECTURE.md) establishes optional targets in `allocators`. Move allocation ownership, heap accounting and retirement first so extracted clients do not depend on temporary Detent accounting types. Then extract shared HTTP clients, bounded MessagePack and the persistent map. Consolidate Journal/DiskLedger reservations after the canonical retained-state component is qualified. The bounded `msgpack` target must configure without fetching its DOM dependencies; client-only HTTP targets must preserve optional server and TLS dependencies. Each component removes the migrated private implementation and receives one complete review.

Resource extraction preserves the difference between automatic memory refunds and explicit durable disk settlement. Task scheduling, mapped-file residency and transfer helpers are reuse evaluations, not blanket replacements for Detent's protocol and ownership state machines. No release requires inventing a general framework around application-specific machinery.

Use complete-component architecture reviews and final implementation reviews rather than per-commit approvals. Track C may finish transport while Detent completes independent extractions; intermediate v2 revisions remain excluded from adoption. Final integrated qualification follows all selected source and pin changes.

The [Stage 6 blueprint](STAGE6_ARCHITECTURE.md), [adoption blueprint](LIBRARY_ADOPTION_ARCHITECTURE.md), [retained-state blueprint](RETAINED_STATE_ARCHITECTURE.md), [library boundary audit](LIBRARY_BOUNDARIES.md) and [fleet gates](FLEET_QUALIFICATION_ARCHITECTURE.md) define the detailed checks. Independent failure-domain, device power-loss and physical fleet certification require suitable hardware; state those limits explicitly in the release contract.
