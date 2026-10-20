# Pending library upgrades

**Date:** 2026-10-15

Detent pins its direct and transitive dependencies in `cmake/dependencies.json`. Qualification uses immutable archives of those revisions. The sibling repositories may advance independently; their current working directories are not reproducible build inputs.

## Active adoption pass, October 17

The user requested adoption of the latest libraries, before/after benchmarks and an audit for duplicated mechanisms. The [validated adoption blueprint](LIBRARY_ADOPTION_ARCHITECTURE.md) governs this pass. Published default-branch heads differ for 27 of 28 Kronuz pins in the [new inventory](../benchmarks/library-adoption-candidates-20261017.json); older candidate rows below retain their historical qualification only. Freeze the transport application baseline before changing pins, capture preliminary old-library measurements, then compare old/new builds from identical application sources in alternating pairs.

The [refreshed published inventory](../benchmarks/library-adoption-candidates-20261017-r2.json) includes completion `5251cf9` with the per-device macOS flush correction, threadpool `628679a` with defined restart behavior, and compressors `ed590c8` with subset tests and all-disabled rejection. Journal `3dccf50` and consensus `fc66c0e` now pin the coherent dependency set. These supersede the earlier local-only warnings; Detent integration qualification remains pending. Keep pin adoption separate from helper consolidation and new storage mechanisms.

Heavy work now follows the shared `quiet` protocol. Run `quiet check` before compilation, tests or measurements: strict holds permit only reading/editing; light holds allow nice-19 builds/tests with at most four jobs. Wrap measurements with `quiet run`. The interrupted preliminary durable sample is invalid and must be repeated; its suspended processes have been stopped. No dependency pin has changed on main.

Source preparation lives in separate detached checkouts under `.scratch/library-adoption-20261017/`: `candidate` contains published pin updates, `reuse` contains independent helper consolidations, and `comparison-base` retains old pins. Remove the earlier staged timing override: both comparison arms now derive synthetic election delays from `Timing` and use each library revision's defaults. No old implementation or compatibility mode is retained for Detent.

Group F has a source-derived recovery-resource gate. Latest consensus reads retained state against library ceilings, while parts of incremental encoding still use operating limits. Fix that inconsistency through one shared library policy/validator across decode, replay, encode and Core recovery, rather than parallel legacy functionality. Keep configured checkpoint reservation sizing unchanged. Validate allocation bounds before growth and preserve historical group-limit semantics. Prefer library defaults; document a custom bound only after proving why the defaults are insufficient. This remains a source finding pending runtime qualification.

## Baseline measurements

The [old-library baseline](../benchmarks/library-adoption-baseline-20261017.json) contains one valid sample for each of 37 workloads, under strict `quiet` holds. It covers reads, hashing, codecs, deltas, authority/local-state operations at 100k/500k, durable 16/64-producer runs, and the existing maximum-definition scenario. The harness initially selected an invalid 100k/64 heavy case, which exceeds proposal and retained-log limits; that failure remains recorded, and the supported 128/1 heavy case passes. These are preliminary samples, not a nonregression claim. Paired comparisons alternate arm order and skip byte-identical binaries with identical dynamic linkage.

## First dependency group qualification

The frozen group A build passes eight initial Core/semantic checks and ten transport checks. [Preliminary footprint evidence](../benchmarks/library-adoption-progress-20261017.json) records every comparison binary. `detentd` grows by 73,776 bytes (1.59%) and the relay by 73,984 bytes (1.51%); these exceed the review threshold. In `detentd`, the executable text section grows by 30,992 bytes and the link-edit segment by 40,960 bytes. The largest symbol extent increases include HTTP progress-write/deadline templates and TLS failure reporting; these estimates do not isolate each feature's cost. Runtime comparison is pending, so this group is not accepted or pinned.

The A–E native suite exposed a pre-existing trust fixture failure, reproduced with old libraries: the fixture closed persistent HTTP/1.1 responses without declaring `Connection: close`. A transport retry could then reach the secondary endpoint before the intended profile rejection. The fixture now declares its connection behavior; its assertions remain unchanged. All ten negative trust cases pass with both old and A–E binaries. This changes test behavior only; it is not an introduced library regression or a production trust fix.

### Completed A–E comparison, October 18

The [paired results](../benchmarks/library-adoption-paired-ae-20261017.json) contain 54 successful samples: three alternating pairs for each of nine workloads with changed executables. Another 28 workloads have byte-identical executables and identical dynamic linkage and were skipped. The full native run passes 213 of 214 tests in 1,209.39 seconds; the sole trust-fixture failure above passes in a corrected two-test rerun. This is not a single 214/214 invocation.

The 500k paged-collection case exceeds the CPU review gate: median process CPU rises from 9.98 to 11.30 seconds (13.23%). Both arms have unchanged segment, I/O, CPU-turn and sampled-heap counts. Individual CPU samples are 11.32/9.92/9.98 seconds before and 11.31/11.30/9.93 after, so both arms span the observed range. The focused repeat below does not reproduce this result; it remains evidence requiring interpretation rather than proof of an introduced regression. The other eight changed workloads have median CPU differences between -0.75% and +0.24%.

These microbenchmarks do not discharge the HTTP daemon CPU gate. Fleet process-CPU instrumentation passes Ruff, strict type checking, seven focused unit tests and reduced raw/zstd end-to-end checks with complete state verification. The initial manual checks used an expired one-day TLS fixture and failed before measurement; fresh isolated certificates pass. Those functional checks overlap other host work and establish no performance result. The controlled comparison and adoption review below are complete. No A–E pin is adopted on benchmark evidence alone.

The [complete A–E footprint record](../benchmarks/library-adoption-progress-20261017.json) embeds its effective dependency revisions and binary hashes. `detentd` grows by 95,472 bytes (2.06%), the relay by 90,736 bytes (1.85%), and the agent by 14,008 bytes (0.43%). The first two exceed the footprint review threshold; the exception is recorded below. An initial October 18 retry exits before measurement because existing builds remain active. A subsequent strict quiet run completes all 14 samples. The [seven-pair repeat](../benchmarks/library-adoption-collection-repeat-20261018.json) measures median CPU of 10.52 seconds before and 10.46 after (-0.57%), median wall time -0.62%, and median RSS +0.84%. Median paired CPU difference is 0.0%; individual pairs range from -2.16% to +2.68%. The original +13.23% median result is not reproduced, and its cause remains unresolved. Both frozen executable hashes are unchanged; resource-component changes are outside these arms.

### Frozen daemon CPU comparison, October 18

The [daemon comparison](../benchmarks/library-adoption-daemon-cpu-20261018.json) completes three alternating before/after pairs for each of raw and zstd, using the frozen executable hashes. Every run verifies all 256 records across 16 transport probes and two durable readers. Four writers schedule 32 workloads per second during a 60-second window; the bounded generator refuses work while its producer is occupied, so admitted/acknowledged counts are recorded separately from scheduling. CPU is normalized by acknowledged logical commands.

Median CPU per command changes by +0.35% for raw and +2.58% for zstd. Paired differences range from -10.53% to +10.13% for raw and +2.11% to +10.99% for zstd. Both median observations are below the 5% review threshold; the spread prevents a statistical-equivalence or causal performance claim. Source editing and worktree creation continue under the quiet policy, while compilation, tests and competing benchmarks are excluded. This small workload does not qualify sustained fleet capacity. The artifact preserves process CPU counters, accepted/refused scheduling counts, verification results and source/binary provenance. The footprint exception is recorded below.

### A–E adoption review

The completed component review accepts the recorded executable-size increases as an explicit exception for the compatibility and correctness changes. This is a file-size cost, with no claimed RSS, power or dollar benefit. The daemon medians stay below the 5% investigation threshold, while the small sample and pair variation remain limitations. Proceed with compatible A–E pin adoption after the independent cpp-btree layout correction passes downstream sanitizer qualification. Retained-state resources, new extraction overhead and finalized transport/F remain separate gates and cannot reuse these measurements as their qualification.

### Compatible pins adopted, October 19

A separate clean build at `fbd15d5` fetches all dependency pins without local source overrides. Default Release (`-O3 -DNDEBUG`) builds the authority, distributor, managed agent and relay against OpenSSL 4.0.3; all four command help checks pass. Tests are disabled in this profile. Its [build evidence](../benchmarks/library-adoption-closure-20261019.json) supplements the assertion-enabled functional suite below and establishes no performance result.

The [closure record](../benchmarks/library-adoption-closure-20261019.json) identifies the adopted A–E revisions, including the independently qualified cpp-btree `dd984d9` correction. A clean application build passes 201/201 tests in 957.12 seconds. The relay invocation passes 15/15 tests in 273.19 seconds: 13 newly enabled cases and two repeated prerequisite fixtures, for 214 distinct tests across both invocations. Assertions remain active. This is not a single 214-test invocation or a sustained-capacity measurement. The application source matches the qualified checkout; input snapshots preserve the declared public revisions.

Consensus `b7073bb` and Journal `57e6250` remain unchanged in production. The published resource extraction and finalized transport/F have separate adoption gates. Clean product builds without local source overrides remain part of release qualification.

## Retained-state consistency findings

The published consensus encoder rejects historical payloads after lowering operating limits, although checkpoint decoding and Core recovery accept them. Consensus commit `c2cbf2e` shares the existing Recovery entry validator with the incremental encoder, preserves record budgets, and gives configuration payloads their membership encoding bound. A regression fails on published `fc66c0e`; the corrected checkpoint, Core, membership, Worker, compatibility, decoder-corpus and terminal suites pass (7/7, 88.91 seconds). Source review reports no P1/P2 findings for this scope. This correction is published in the qualified library closure below; Detent production pins remain unchanged.

Consensus commit `ef30b4f` fixes delivery byte-budget underflow after a historical first entry exceeds a lowered RPC limit. Its regression fails before the guard; three focused suites pass afterward, and the 200-seed, 10,000-step simulation passes. Exact-boundary and zero-byte following entries are covered. The correction is published in the qualified library closure below; Detent production pins remain unchanged.

The [validated retained-state blueprint](RETAINED_STATE_ARCHITECTURE.md) defines the coherent remaining component. Journal reservation growth is published and qualified below; it remains outside the frozen A–E comparison.

The shared retained-state policy now covers checkpoint funding, Store recovery batches, Worker output accounting, live admission, follower replacement and transport. It preserves retirement credit for one bounded replacement batch. Detent integration qualification and coordinated Group F adoption remain open.

The October 18 checkpoint-funding regression exposes another historical-limit boundary: shutdown can spin indefinitely when retirement budgets use the lowered command limit and refuse a larger retained payload on every turn. A finite-drain regression fails both funded and pressure/canceled cases (25 checks, two failures). The untouched published revision also fails a minimal reproducer, retaining all three entries after 1,000 turns. Consensus `3c60f47` drains them in three turns through one shared oversized-first allowance and saturating byte accounting. Its isolated Core, retained-log, retirement and compatibility suites pass (4/4, 6.16 seconds). The broader component's retained-state/log suites pass (2/2), and all 25 focused Worker checks pass. Platform qualification passes in the published closure below; Group F adoption remains pending. The shared catalog records the contract in `63d9288`.

### Published resource component, October 18

Consensus `61149a2e43a4871676163198bde896fae3ac4ef6` and Journal `cb66f3d4e644ee5f3c3e86cd61a8c83d2b36be62` are pushed to their public main branches. [Consensus CI](https://github.com/Kronuz/consensus/actions/runs/39168898196) and [Journal CI](https://github.com/Kronuz/journal/actions/runs/39168323988) each pass all eight jobs, including Linux sanitizers, macOS and FreeBSD. The consensus test allocation probe uses the sanitizer allocation hook under TSan to avoid conflicting global allocation operators; its calibrated retained-state suite passes 286 checks.

The isolated Detent integration uses these public revisions and the immutable A–E closure. Linux passes all three host tests in 13.03 seconds; FreeBSD passes in 14.79 seconds with libc++19's explicit `-fexperimental-library` option. The tests replay history above each reduced command, RPC-byte, log-byte and entry-count limit, then checkpoint and reopen with receipts preserved. macOS sanitizer qualification remains open. These results do not change production dependency pins or discharge default-footprint and final transport qualification.

## Consensus peer protocol

### Shared transport agreement, October 18

Detent agrees to the [shared peer transport revision 1](SHARED_LIBRARY_DECISIONS.md#6-the-peer-transport-revision-1-strata-designpeer-transport-2026-10-17): use one coordinated restart directly from the qualified version-1 transport into finalized version 2. Do not adopt `d7192f7` or revisions in `0307308..fc66c0e`. Those revisions remain historical inspection and regression inputs only. Group F waits for Track C's transport work and its coherent dependency closure; the October 17 inventory is not an approved Group F target.

Do not freeze unfinished consensus wire bytes. Qualify N/N-1 negotiation when the shared implementation lands; its future availability does not make the current version-1 transition rolling-compatible. Keep fixed voters and the named-file backend. Record Detent's transport-suite results at each additive step in [LIBRARY_BOUNDARIES.md](LIBRARY_BOUNDARIES.md), including the tested revision and dependency closure.

Detent joins the journal backend decision after Track C T6 measures both backends at 1,000 groups. Keep existing backend use until those results exist. The shared recovery-bound work must reconcile with [the retained-state resource blueprint](RETAINED_STATE_ARCHITECTURE.md) before either implementation lands: a checkpoint length cap alone does not cover replay allocation, retained-log accounting or checkpoint funding. Use one shared policy and implementation.

SPKI pins, service-group key permissions and reactor exception logging need source verification and focused qualification before adoption. Existing certificate-pin rotation documentation needs a restart-order audit against the PKI finding. These are open integration items, not security fixes established by this handoff.

The qualified consensus revision `b7073bbe4d94e66dce526f193b0dd81a57cdb661` uses DTC1 peer control frames. The historical inspection candidate `d7192f728b98fa42866e5829818bb19b11ae89a9` uses DTC2 and is excluded from adoption by the agreement above. Replacing the dependency alone does not support a rolling upgrade with old voters. Detent uses the coordinated restart; no application-specific dual codec is planned. Mixed-version rejection and recovery tests remain release gates.

The candidate also adds dynamic membership and shared turn-log support. Their availability in the library does not imply Detent exposes or qualifies them. Existing fixed-voter disk compatibility fixtures cover a different boundary from peer wire compatibility.

## Storage and other additions

Completion adds `File::sync_data`, `Directory::sync_files` and read-only file mappings. Journal and consensus add a shared turn-log backend. These APIs need workload and durability qualification before Detent adopts them. In particular, a fix to the new shared manifest decoder is not evidence that Detent's existing named-file recovery path has the same defect.

Other candidate changes include bounded zstd decompression, thread-pool join behavior and owner-scoped task groups. Each adoption must preserve the public behavior Detent relies on and include the relevant focused and end-to-end tests. Performance claims require measurements against the recorded qualified revision.

An independent Release build of `detentd`, `detent-agent` and `detent-relay` passes against immutable snapshots of the candidate sibling revisions. A three-voter writer smoke also passes with 1,000 keys and 16 producers, including final state and receipt reconciliation. This establishes basic build and writer compatibility for a uniform new-version cluster. It does not qualify mixed-version peers, a shared backend migration or the full production gate. No candidate revision or new backend is part of the qualified baseline.

## Reserved adoption passes

This file is the shared handoff between Detent integration work and library maintenance. Update the candidate revisions and findings here before starting an overlapping library change. Keep Detent's measured fleet baseline frozen; qualify upgrades in a separate build, then change pins in small commits.

| Pass | Reserved investigation time | Scope | Required exit gate |
| --- | --- | --- | --- |
| 1 | Next focused 60–90 minute pass after the measurement/pacing harness | Compatible leaf/runtime updates, starting with compressors and threadpool; adopt one dependency or tightly coupled set at a time | Public API/disk/wire diff, focused regression and sanitizer checks, daemon smoke, binary footprint and relevant before/after workload |
| 2 | Following 2–4 hour pass | Completion upgrade, including the cross-device fix below; retain Detent's existing storage backend | Local publication/recovery/cancellation and authority disk suites on macOS/Linux/FreeBSD, native versus fallback I/O accounting, no durability or resource regression |
| 3 | Separate 2 hour architecture/compatibility pass before implementation | Finalized version-2 journal/consensus closure and coordinated restart from DTC1 | Qualify old-state recovery, mixed-version refusal behavior and rollback boundary; exclude intermediate DTC2 candidates |
| 4 | Separately budgeted after measurements | Optional shared turn logs, batched device barriers, mappings and task groups | A demonstrated bottleneck, representative before/after data, lifetime/admission design and crash/power-loss model gates |

These are reserved investigation budgets, not completion promises. A failed gate produces a concrete finding here and leaves the qualified pin unchanged. A long-running benchmark may continue on the old immutable build while compatibility work uses another build; measurements used for capacity claims must record or exclude that contention.

## Proposals from the library maintainer, October 17

A heads-up while this pass is held: the library-side work below lands in the Kronuz libraries, additively. No Detent source, pin or baseline changed. Each item is a candidate for a later pass, adopted and measured like any other.

| Proposal | Library change | What Detent would adopt | Notes |
| --- | --- | --- | --- |
| Colored request logs | None: `Kronuz/http-log` exists | Wrap the publisher, relay and status handlers in `http_log::AccessLog`, as Xapiand does (`manager.cc:187`) | Measure its CPU on the request path before enabling it by default |
| Severity-tinted messages | `logger`: opt-in `LogConfig::tint`, coloring each message in its severity's palette color | Set `tint` | Off by default; output is unchanged unless set |
| A color per voter, peer and bundle | `term-color`: `name_color(name)`, Xapiand's per-node color (FNV-1a hue, golden-ratio spread) | Color node, voter and bundle names in log lines | Same name, same color on every host |
| Startup banner | `term-color`: `gradient_banner(art, top, bottom, info)` | A wordmark with version and build lines at startup | Optional |
| TLS servers without a private CA | `reactor::tls`: the `Server` role may omit the CA, and a system-roots option for clients and `--status` | Publisher or relay listeners with public certificates | Additive: contexts that pass a CA behave as today |
| A strict TLS option | `reactor::tls::Options::strict` (planned with Xapiand's HTTPS): TLS 1.2 restricted to ECDHE with AEAD ciphers, no session tickets in TLS 1.2 or 1.3, and no TLS 1.2 session-ID resumption | Setting `strict` on the peer, publisher, and relay contexts, after Detent's TLS tests | Off by default; contexts without it are unchanged. OpenSSL's default TLS 1.2 list includes four static-RSA suites without forward secrecy |
| Consistent pins | Every Kronuz library pins its own dependencies since October 17, and a consistency pass moves lagging pins to each dependency's current commit | Closed library-side on October 17: journal `3dccf50` pins completion `5251cf9`, and consensus `fc66c0e` pins journal `3dccf50` and reactor `e19afd9`, each 8/8 and 22/22 tests natively and under ASan/UBSan. Every Kronuz library's consumers now pin one commit of it | Pass 3 can take these heads as its coherent set; Detent's own pins are unchanged until then |
| Generated certificates (later) | `reactor::pki`: first-start CA, signed node certificates, single-use enrollment tokens bound to the CA fingerprint | An optional mode beside bring-your-own CA; pinned voters can pin issued leaves | Xapiand qualifies it first; reviewed as security code before any adoption |

## Coordination ledger

| Library / candidate | Owner | Finding and action | State |
| --- | --- | --- | --- |
| completion `86ca1a26c419238e851359a5199d9c8a04061d8e` | Library fix completed; Detent integration pending | macOS `sync_files` previously staged every file but full-flushed only the final descriptor's device. It now flushes one representative per distinct device. Public APIs and Linux/FreeBSD behavior are preserved. | Committed locally, not pushed by this session. macOS/Linux/FreeBSD 12/12 each, ASan/UBSan 12/12, focused TSan 1/1. Cross-device regression uses a syscall model; physical power loss remains untested. Detent pin unchanged. |
| compressors `6f6d7c05b74c3f790e23e4b994d945f12a0ea7aa` | Detent integration | Adds bounded zstd decoding and backend build switches. Check production fragment bounds and malformed/truncated streams before adopting; measure raw/zstd footprint and workload effects. | Pass 1 candidate; isolated daemon build and three-voter smoke pass as part of the candidate set. |
| threadpool `fdc0a1ef216aaf5357c92a6d09d6b60af65e66db` | Detent integration | Changes join settlement and adds TaskGroup. Verify authentic original completion and shutdown races; adopting the revision does not require replacing existing ownership mechanisms with TaskGroup. | Pass 1 candidate; isolated daemon build and three-voter smoke pass as part of the candidate set. |
| scheduler `3ec5d7d` / random | Detent integration, coordinated with library maintainer | Scheduler now links `random::random` instead of compiling vendored definitions. Detent already makes `random_lib` available before reactor/http/cluster, which can populate scheduler. Preserve that order and verify one shared target when adopting the pin. | New Pass 1 candidate reported in the maintainer handoff; not part of the captured compatibility build. Source ordering checked; no compilation or duplicate-symbol reproduction during the benchmark hold. |
| journal `a678e44e7fffd6b745bd89ac9589fdc15796be6e` / consensus `d7192f728b98fa42866e5829818bb19b11ae89a9` | Joint architecture/integration | New journal headers require completion's `File::sync_data`. Consensus changes peer frames to DTC2. Upgrade this dependency stack coherently; do not interpret old disk fixtures as wire compatibility. | Pass 3 required; no pin bump or rolling-upgrade claim. |

The candidate build captured 27 immutable first-party library revisions. Its successful three-daemon build and 1,000-key/16-producer smoke establish basic compatibility for that combined candidate set, not an independently qualified pin bump for each row. The completion durability fix landed afterward and needs its own Detent integration run.

For each adoption, record old/new commit IDs, the source/API/format audit, exact test commands and results, measured effects, known limitations and the Detent commit that changes the pin. Coordinate file ownership with an active library maintainer before editing; never reset their work or point qualification builds at a changing sibling checkout. Publishing library commits and changing an application's dependency pin are separate actions.

## Application-side performance findings

The preliminary 500-session/128-exchange run passed checkpoint and full SDK verification, but spent 283.23 seconds around a 10.83-second measured write interval. Probe CPU was 9.54 seconds over the full process lifetime and 2.13 seconds during measurement. The qualification driver retained already-verified probes in the polling loop and queued operations could use the pre-checkpoint goal. Both paths are corrected in `55d6978`. The [paired rerun](../benchmarks/fleet-checkpoint-comparison.json) preserves the same fleet settings and reduces checkpoint plus SDK verification from 272.01 to 32.60 seconds. Full native lifetime changes from 283.23 to 73.58 seconds, CPU from 9.54 to 8.24 seconds, and peak RSS from 60.61 to 67.02 MB. Closed-loop write totals and background contention differ, so this is a checkpoint scheduling observation rather than an ingestion-throughput comparison. This is a benchmark scheduling issue, not evidence of a library or production-daemon performance defect.


The library maintainer's [C4 handoff](LIBRARY_BOUNDARIES.md) records later consensus pipeline, follower batching and control-priority work (`6af73ae`, `7e23145`, `24d5399`, `906383c`). The 27-library compatibility snapshot above predates those commits. Re-capture a coherent revision set before adoption, and wait for the documented DTC2 frame/tag boundary to settle; a passing build of `d7192f7` does not qualify the later C4 revisions.

The subsequent handoff adds snapshot windows (`fcc18e6`) and append payload caps (`c752657`). Defaults preserve the prior one-chunk snapshot window and RPC-sized append cap, but snapshot inactivity timeout behavior changes. Include slow-progress snapshot installation and configured window memory bounds in Pass 3 qualification. The maintainer also records leaf fixes in boolean-parser, time-point, cartesian, md5 and ctrie; inventory actual Detent consumers before adding them to Pass 1. Their library test results do not establish Detent integration results.

## Source findings awaiting the shared benchmark window

The user reopened compilation and load runs on October 16. The following source findings still need focused reproductions and fixes; they do not change the qualification baseline.

| Finding | Evidence | Next action |
| --- | --- | --- |
| Legacy HTTP response allocations have no explicit wire or decoded size ceiling | `src/detent_http/http_client.cc`: header accumulation, `read_all_to_eof`, `reserve(Content-Length)` and gzip/zstd decoding all grow strings without a byte cap. This is the classic blocking client; the managed binary fragment decoder already admits decoded bytes and workspace separately. | Detent integration owns configurable header/wire/decoded bounds with compatible defaults, then oversized Content-Length, EOF-body and compression-expansion regressions. Evaluate the candidate bounded zstd helper during Pass 1; a pin bump alone does not bound existing callers or gzip. |
| Compressor subset configurations skip the current test executable | Candidate `6f6d7c0` registers tests only when all three backends are enabled. Disabling all three also leaves a static target with no sources. | Library maintainer: define the all-disabled configuration contract, then qualify each supported backend subset. Confirm generation behavior after the compilation hold before claiming a reproduced failure. Library side fixed, October 17: compressors `ed590c8` tests every built subset and refuses all-off at configure. |
| Thread wrapper restart semantics need explicit qualification | Candidate `fdc0a1e` changes join settlement, but `Thread::run()` still gates on `_running`; it does not recreate the one-shot promise/future after completion. README calls `run()` idempotent while running and does not specify post-join restart. | Library maintainer: reproduce run → join → run and concurrent completion/start boundaries under sanitizers, then define and implement restart or one-shot behavior without silently breaking consumers. No new runtime failure is claimed from this source inspection. Library side fixed, October 17: threadpool `628679a` defines run() after a run ends (joins it, rethrows, fresh promise) and adds running(); the old code crashes on run -> join -> run. |
| HTTP offload refusal bypasses application protocol errors | Pinned `http_asio.h` emits plaintext HTTP 503 without `Content-Type` when its offload gate is full. Delivery clients require bounded JSON controls. This is a source-confirmed defect matching the earlier checkpoint error; the diagnostic rerun did not reproduce that error. | Relay integration sizes each reactor gate to the existing global connection cap. Sequential request handling holds at most one offload permit per connection, making that redundant refusal unreachable while preserving application admission bounds. A reusable library overload-response hook remains a separate improvement. |

## Durability modes agreement, October 19

Detent agrees to the wire-v3 contract in strata's [durability-modes revision 50245a1](https://github.com/Kronuz/strata/blob/50245a1084df3a4744cc855876a9e24f27ffdbe6/docs/design/durability-modes.md#for-detent), before that design's step 4 lands. The exact bytes, optional interfaces and conditions are recorded in [shared decision 7](SHARED_LIBRARY_DECISIONS.md#7-the-durability-modes-strata-designdurability-modes-2026-10-19). There is no objection to its stated growth or behavior for Detent.

The existing coordinated restart goes directly from qualified DTC1 to finalized v2. Version 3 then uses the N/N-1 range without a second flag day. Preserve version-independent host-profile compatibility, negotiated-version frame sizing and credit accounting. Do not confuse wire negotiation with activation of new disk formats or start another adjacent-version upgrade while the previous one remains incomplete.

Keep all durable authority groups `synced` on the named-file, per-group journal. Do not opt into turn-log forgetfulness, memory acknowledgements or recovery elections. Preserve absent-`acks` membership bytes, batch/checkpoint formats, barrier-before-reply ordering and equal commit cursors. The per-group backend must reject `written`; Detent's current configuration has no selector and rejects unknown fields. `src/detent_authority/node.cc` constructs the existing consensus Worker, and its configuration in `service.cc` names no turn-log or acknowledgement opt-in. The classic UDP distributor is a separate legacy path, outside this consensus wire upgrade.

`Unsupported` must retain storage and stop recovery safely, even through existing `Corruption` catches. New v3 flags must not opt a synced-only receiver into recovery behavior. New APIs remain optional and require full C++ rebuilds and their own fault, format and platform checks. This agreement changes no production dependency pin or runtime behavior.

Every additive library step must run the enabled-product Detent suite with its exact commit in a scratch manifest. Record the dependency closure, commands, results and limitations in [LIBRARY_BOUNDARIES.md](LIBRARY_BOUNDARIES.md#durability-modes-additive-commit-gate-october-19); a later combined head is insufficient. The existing journal-backend decision still waits for T6's per-group/shared comparison at 1,000 groups.

## Library changes past the production pins, October 19

This source review records the handoff without moving production pins. The proposed durability-mode APIs remain design-only unless an immutable implementation commit says otherwise. Each subsequent library step follows the [scratch consumer-suite gate](LIBRARY_BOUNDARIES.md#durability-modes-additive-commit-gate-october-19).

| Library | Production pin → reviewed head | Verdict and evidence |
| --- | --- | --- |
| completion | `5251cf9` → `7734544` | Accept as a qualification candidate. Seven strata commits add directory ownership, filesystem/trust reporting and opt-in root handling; head also includes the separately qualified `21809c7` resource alias change. The handoff reports 214/214 Detent tests with the seven strata changes replayed onto the old pin. That is reported replay evidence, not a fresh Detent run against the complete `7734544` closure or a production adoption. |
| reactor | `e19afd9` → `1c37def` | Accept the receive-loop correction, subject to the Detent checks below. Seven commits contain CR1–CR4, tests and review. A throwing UDP handler is reported through `L_EXC` and dropped; a throwing hook is also contained. A receive error still exits the loop; explicit `stop()` stops its io_context and joins the thread. |
| threadpool | `628679a` → `394aaea` | Accept as a dependency-only candidate. The diff changes research/docs and top-level CMake's default-OFF `THREADPOOL_BUILD_RESEARCH`; linked pool code is unchanged. Detent does not enable research. `feature/scheduler` and its futex wake, per-group DRR and work classes are a separate future adoption, with their own lifecycle and cost qualification. |
| hashes | `7ab0c23`; SipHash-2-4 proposed | Accept the additive primitive and qualify its eventual immutable commit. No SipHash implementation is present at the reviewed head. Detent has no new call site or pin change; rollout buckets, persisted digests and token identities retain their existing algorithms and bytes. |

### Completion behavior and ownership

`acquire_directory_owner()` flocks the directory descriptor without creating a lock file. It attempts the filesystem's lock operation and reports unsupported locking; it no longer refuses merely from a filesystem-type classification. `directory_filesystem()` and `directory_trust()` report facts through the IO base. `PosixIOOptions::follow_root_symlink` and `allow_shared_directory` remain opt-ins with strict path/permission defaults. Current Detent uses neither option nor filesystem classification, and its actual refusal set must stay unchanged by a later policy preset.

There is one behavior change for an existing method: on macOS, `sync_directory()` without an acquired file owner performs directory `fsync` followed by `F_FULLFSYNC` through the directory descriptor, replacing its former `logic_error`. Unsupported barriers still fail closed. With `owner.lock` held, the full device barrier still uses that file. Detent's storage sessions and named-file journal retain file ownership across publication, so this is not an intended barrier change for those paths; qualify ownerless and owned paths separately.

Completion now depends on `allocators::resources`. Its `AllocationContext` is the canonical alias from `21809c7`; use one immutable allocators revision throughout the closure and rebuild every C++ consumer. The [resource extraction evidence](../benchmarks/resource-extraction-qualification-20261019.json) covers that earlier component, separately from the handoff's replay. Do not substitute the directory lock for Detent's file lock: they do not exclude each other. The [CL-2 response](CONSOLIDATION_RESPONSE.md#cl-2-one-ownership-protocol) requires a coordinated dual-lock migration.

### Reactor behavior in Detent

The affected consumer is the classic distributor's `cluster::Bus` callback in `src/detentd/detentd.cc`, through `on_bus_message()` and `DetentRaftDelegate`. Shutdown explicitly calls `bus_->stop()` in the distributor destructor. No deliberate throw-as-stop contract is present. The durable authority's consensus transport is a separate TCP/TLS path.

A persistence or allocation exception can still escape `on_raft_apply()`: the snapshot code persists before changing live state, and Raft advances `last_applied_` only after the delegate returns. These application failures need qualification under the continuing-loop behavior. Require a bad/throwing datagram followed by a valid one, a throwing diagnostic hook, explicit shutdown, and persistence failure followed by recovery, without silently accepting an unapplied command. No existing source check establishes those runtime results. Keep the shared diagnostic hook visible using Detent's logging adapter; do not add another logging engine or rely on the default hook to report an error.

### Adoption conditions

Keep transitive pins coherent, including reactor users in cluster/http/consensus and completion users in journal/consensus. The finalized-v2 restriction still applies to consensus; a pin-skew fix does not authorize an intermediate wire adoption. Record each scratch run separately, then move ready production pins in a reviewed component commit. No new benchmark or runtime result is claimed by this source review.
