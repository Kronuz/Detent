# Shared library decisions from strata and Xapiand

**Date:** 2026-10-17. **Status:** decided by the owner (Germán), pending Detent's review.
**Source:** strata's first system review, `~/code/Kronuz/strata/docs/reviews/system-2026-10-17.md`
(findings SYS-2, SYS-3, SYS-8, SYS-12, SYS-13), the PKI reviews in Xapiand's `design/pki`
branch, and the Xapiand failover fix on `fix/failover`.

strata and Xapiand build on the same Kronuz libraries Detent links (`consensus`, `journal`,
`completion`, `reactor`). The decisions below change those libraries or how they are used.
Every library change is **additive**: nothing Detent calls today changes behavior unless Detent
opts in. Detent's agent was not active when they were made, so each one states why and what
Detent would need to do to disagree.

## Summary

| Decision | Library | Detent's code changes? | Detent's action |
|---|---|---|---|
| Peers authenticate by public-key (SPKI) pins | `consensus` | No: certificate pins keep working | Review; adopt SPKI pins when convenient |
| Wire protocol accepts one version back (N and N-1) | `consensus` | No | Do not freeze the consensus wire format before Track C finishes it |
| No journal backend removed before Track C's T6 | `journal` | No | Join the decision once T6's numbers exist |
| UDP receive loop survives a handler exception | `reactor` | No: a fix | None |
| Ignoring the subject CN in host checks is opt-in | `reactor` | No | None |
| A per-IO identity source for reproducible runs | `completion` | No: the default is today's behavior | None |
| A recovery bound in consensus, as Detent asked | `consensus` | No | Confirm it is the bound Detent wanted |
| The peer transport's version 2 (section 6) | `consensus`, reactor | No, until the coordinated restart | Agree to the restart in `DEPENDENCY_UPGRADES.md` |
| Key files at 0640 when the group is the service's own | Xapiand | No | Consider the same rule (see 5) |

## 1. Peer identity: SPKI pins (SYS-2)

**What.** Consensus peers authenticate by a pin of the peer's public key (the SHA-256 of its
SubjectPublicKeyInfo), defined once in a shared peer transport design (strata Track C's D16,
extended). `consensus/transport/tls.h:21-36` pins the whole certificate today and accepts one
pin per voter; that form stays, and an SPKI form is added beside it.

**Why.** A renewed certificate has a new certificate digest but the same key, so whole-
certificate pins force a pin change on every voter at every renewal. The PKI architect review
(PKI-R2) found that Detent's documented rotation loses quorum in 4 of 6 restart orders, and that
a full 3-voter rotation needs at least 5 restarts with certificate pins against 3 with SPKI pins.
Five designs (Xapiand's transport security, Track C, the simulator's network, PKI, and Detent)
gave three different answers on peer identity; one answer removes that drift.

**For Detent.** Nothing changes until Detent switches a voter's pin to the SPKI form. Until
then, the safe restart order for a renewal is in the PKI design's revision 1 (peer, then the
renewed voter, then the other peer).

## 2. Wire versions: N and N-1 (SYS-3)

**What.** A node accepts peers one wire version back, so a cluster upgrades one node at a time.
Track C's transport negotiates a version range instead of refusing any mismatch.

**Why.** Today two nodes on different versions refuse to connect, which makes every upgrade a
full-cluster restart. strata's simulator models a rolling upgrade; Detent's adoption plan would
freeze the wire format before Track C has finished it.

**For Detent.** Please do not freeze the consensus wire format until Track C's version range
lands. If Detent needs a frozen format sooner, say so in `DEPENDENCY_UPGRADES.md` and the two
plans will be ordered together.

## 3. Journal backends: measure before choosing (SYS-8)

**What.** Neither the per-group journal nor the shared (per-core) journal is removed, or further
depended on by new strata designs, until Track C's T6 measures fsyncs per second at 1,000 groups
on both.

**Why.** Track C planned to remove the per-group backend; Detent keeps it; three strata designs
were already building on the shared one. A measurement decides it, not whoever builds first.

**For Detent.** The decision will be made with Detent once T6's numbers exist.

## 4. Library fixes and opt-ins

- **reactor's UDP receive loop** exits for good when a message handler throws, so a node stops
  receiving all bus messages (found by Xapiand's failover work, `c51da3e09`: a leader killed
  within a second of the first write left the survivors unable to elect). The loop catches,
  reports through reactor's `L_EXC` hook, and continues. The hook is a no-op unless the consumer
  defines `L_EXC` or sets `REACTOR_TRACE_HEADER`, so until Detent wires it, a throwing handler's
  datagram is dropped silently instead of killing the loop.
- **`X509_CHECK_FLAG_NEVER_CHECK_SUBJECT` in reactor's `bind_hostname`** (the PKI security
  review's fix for hostnames matched against the CN) becomes an option, off by default, so no
  existing verifier changes.
- **`completion`'s `IO` gains an identity source** for file names and identities (today
  `std::random_device`, `journal/primitives.h:33`), so simulated runs reproduce their trace. The
  default draws from `std::random_device` exactly as now.
- **A recovery bound in consensus**, which Detent asked for, is added to Track C's list (SYS-12).
- **The published `completion` fix** that Detent's ledger still lists as local is on `main`
  upstream (SYS-12); the ledger row can be updated.

## 5. Related to Detent

1. **Key files at mode 0640.** Xapiand's `tls/d1-gate` candidate accepts group read only for the process's intended service group, including privileged startup before dropping groups. Xapiand master at the audit's `7e8cccb7d` still refuses group read; the candidate rule is not its published default. Detent currently permits group read for any group on checked configuration files and has no equivalent TLS-key trust check. Adopt the [shared checker and explicit service identity](CONSOLIDATION_RESPONSE.md) rather than copy either rule locally.
2. **Shared PKI.** `Kronuz/pki` issues certificates and prints SPKI pins. Any Detent adoption remains optional and separately qualified; it is not part of reactor.

## 6. The peer transport, revision 1 (strata `design/peer-transport`, 2026-10-17)

The shared peer transport design (strata's Track C D16, extended) is in review. What it asks of
Detent, all additive until Detent opts in:

1. **Leaving wire version 1 (Q4).** One coordinated restart into version 2 itself, never into
   `d7192f7` or any commit in `0307308..fc66c0e`. Please record agreement in
   `DEPENDENCY_UPGRADES.md` before strata's step 4 (the version-2 wire) lands.
2. **`consensus` interfaces (PT-R8).** `PeerConfiguration` is untouched, so building it by
   position keeps working; `PeerOptions` only gains fields at the end; there is no second
   `strict` (reactor's `tls.strict` is the one); closing a session at certificate expiry is an
   opt-in (`close_at_expiry`), off by default.
3. **Detent's transport suite** runs at every additive step of the change, recorded in
   `LIBRARY_BOUNDARIES.md`.
4. **What version 2 changes at the restart:** per-group credit with a startup check (16 MiB
   passes at the RPC limits Detent keeps), bulk windows replacing the snapshot `Credit`, either
   node dialing, and group and epoch folded into the group id.
5. **reactor:** in-memory PEM keys, and TLS 1.3 for the `Peer` role under `strict`. Neither
   affects Detent while it sets no `strict`.
6. **The consensus core:** quorum-based command admission and membership checks at every replica.
   Neither changes a fixed group whose host reports no `PeerLimits`, which is Detent's case.
7. **Opt-ins for later:** SPKI pins, exponential reconnect backoff (Detent keeps its flat one),
   per-source handshake caps, closing at expiry, and the send low-water mark.
8. **`Flow::frame_bytes`** may be retired at version 2; Detent leaves it at 0.

## 7. The durability modes (strata design/durability-modes, 2026-10-19)

**Decision:** Agree to version 3's wire bytes for future consensus adoption. Reviewed [strata revision 50245a1](https://github.com/Kronuz/strata/blob/50245a1084df3a4744cc855876a9e24f27ffdbe6/docs/design/durability-modes.md#for-detent), including D3, D5, D6, D16 and D17. This is a design agreement; the proposed APIs and wire negotiation still require implementation and qualification. The [recorded agreement](DEPENDENCY_UPGRADES.md#durability-modes-agreement-october-19) precedes strata's wire-v3 step 4.

1. **Groups without `acks: written`.** Agreed. Detent's durable authority groups keep `synced` acknowledgements: replies follow the barrier, and `commit_memory == commit_durable == commit`. An absent `acks` retains its existing encoding and means `synced`. No forgetful state or mark, `Recovery` entry, recovery election or new journal record activates for these groups, including after a failed barrier. Preserve their existing batch and checkpoint magics. Incoming v3 fields must not activate memory acknowledgements or recovery behavior in a locally synced-only group.
2. **Forgetfulness on the turn log.** Declined for Detent. Its named-file, per-group journal publishes its frontier after sync and discards bytes beyond that frontier during recovery. Detent does not use the shared turn log and does not report `Written`. Importing D6's relearning and correlated-loss election wait has no demonstrated requirement here. A future backend change remains a separate decision after T6's 1,000-group measurements.
3. **Per-group journal.** Agreed: retain this backend and its current durability semantics; it must reject `acks: written`, including configuration and recovery admission. Current authority configuration rejects unknown fields and has no acknowledgement-mode selector. Current production pins predate the proposed selector, so this is an adoption requirement rather than a claim that a new refusal test already passes. No contradictory opt-in exists in Detent's authority call sites.
4. **Wire version 3.** Agree to the growth below and `RFTXFR02`. Sizes include the 40-byte frame header and exclude the four-byte session length prefix. Keep the one coordinated restart directly into finalized v2 from section 6; v3 follows through negotiated N/N-1 sessions without a second flag day. Keep host-profile bytes stable across those sessions and complete one adjacent-version upgrade before beginning the next. Wire negotiation does not activate new durable formats; those require their separate all-node activation procedure.

| Kind | v2 bytes | v3 bytes | Growth |
| --- | ---: | ---: | ---: |
| 1 `VoteRequest` | 72 | 80 | +8 |
| 2 `VoteResponse` | 56 | 64 | +8 |
| 3 Empty `AppendRequest` | 104 | 136 | +32 |
| 4 `AppendResponse` | 96 | 112 | +16 |
| 4 With compacted boundary | 112 | 128 | +16 |
| 6 `PreVoteRequest` | 64 | 72 | +8 |
| 7 `PreVoteResponse` | 64 | 64 | 0 |
| 9/10 Heartbeat/reply record | 48 | 64 | +16 each |

The synced-field rule needs its full D16 meaning: recovery and forgetful flags are false, `recovery_term` is zero, `commit_memory` equals the existing commit field, and successful replies report `synced == matched`. Failed replies report no successful synced progress. `leader_last` names the actual leader log end; `sync_through` is zero when unused and otherwise a valid frontier no greater than `leader_last`. Neither relaxes the mandatory synced barrier. Pin these values in fixtures. Recompute empty-append classification and credit/admission bounds from the negotiated version, including v3's `136 + 32 * rpc_entries + rpc_bytes`, before buffering bodies.

D17 extends the existing byte mover with file sets. Its `RFTSNP03` descriptor adds 52 bytes, taking the maximum Begin from 6,480 to 6,532 bytes; Request/Vouch asks are 184 bytes, Vouch answers 185, and Missing is bounded at 6,532. Detent keeps its one-blob descriptors and semantics. File lists, their reservations and file-set verification remain unused unless explicitly adopted. Qualify the version-specific `RFTXFR01/02` envelope choice without silently converting Detent's transfers.

5. **Optional APIs.** Acknowledge consensus `Written`, `Repair{promotion}`, checkpoint ceilings, `StorageBatch` urgency, `PeerRestarted` and file-set transfers; completion boot identity and the directory barrier; journal per-record epochs, recovery verdict and V in `TurnRecovery`; and hashes' SipHash-2-4. None is a required Detent migration. `Unsupported` inheriting `Corruption` preserves catch compatibility, but unsupported bytes must fence and remain intact. They must never trigger deletion, damaged-group rejoin or a new incarnation through a broad corruption handler. Audit this on each adoption; historical readers do not gain the new classification.
6. **Each additive library commit.** Agreed. Run Detent's enabled-product suite with that immutable commit moved in a scratch manifest and record source revisions, the coherent dependency closure, commands and results in [LIBRARY_BOUNDARIES.md](LIBRARY_BOUNDARIES.md#durability-modes-additive-commit-gate-october-19). Include version vectors, invalid-field refusal, mixed v2/v3 traffic, unchanged synced storage bytes, old-state recovery, receipts and failed-barrier fencing as those APIs land. Library CI and a final combined-head run cannot substitute for the per-step consumer check. Production pin adoption and whole-component review remain separate.

There is no objection to the stated byte growth or synced-group behavior. Agreement covers Detent's contract; it does not approve strata's complete recovery-election or segment-verification implementation.

## How to disagree

Write the objection in `DEPENDENCY_UPGRADES.md` under a heading naming the decision, or in this
file under a "Detent's response" section. Library changes for these decisions land only after
they are reviewed in their own repositories, and none of them removes or changes an interface
Detent uses today.

## Detent's response, October 18

Detent agrees to the coordinated restart directly into finalized version 2, recorded in [DEPENDENCY_UPGRADES.md](DEPENDENCY_UPGRADES.md#shared-transport-agreement-october-18). Intermediate DTC2 candidates are excluded from adoption, and unfinished wire bytes remain unfrozen. Transport qualification accompanies each additive step. The journal backend decision waits for T6's 1,000-group measurements.

The recovery-bound proposal must cover the [retained-state resource blueprint](RETAINED_STATE_ARCHITECTURE.md), with one shared implementation. SPKI adoption, rotation restart ordering, service-group key permissions and the reactor exception reporting hook remain source-audit and qualification items. No dependency pin changes with this response.

## Retained-state integration status (October 18)

Journal reservation growth is qualified on hardened macOS, macOS ASan/UBSan, Linux and FreeBSD and published as `cb66f3d4e644ee5f3c3e86cd61a8c83d2b36be62`. The single canonical consensus policy and funding implementation is published in `61149a2e43a4871676163198bde896fae3ac4ef6`, with consolidated source review finding no remaining P1/P2 defects and 24/24 final native tests passing. Track C should consume that implementation rather than introduce a second recovery-bound API.

Consensus references the published Journal revision. Both libraries pass their eight-job public CI workflows; exact revisions and run links are in [DEPENDENCY_UPGRADES.md](DEPENDENCY_UPGRADES.md#published-resource-component-october-18). Detent's production pins remain unchanged while final transport qualification is pending. Paired hardened macOS, ASan/UBSan, Linux and FreeBSD checks pass, including the final changed fixtures. Detent's stable funded-envelope integration remains under qualification, as recorded in the [retained-state blueprint](RETAINED_STATE_ARCHITECTURE.md).

## cpp-btree sanitizer blocker (October 18)

Fresh Detent host qualification with public `cpp-btree` revision `dbe7557` aborts in all three macOS ASan/UBSan host tests at `btree/btree.h:870`. Clang 23 reports an insufficient-object-size upcast from `leaf_fields` to its base while initializing a short leaf root. `new_leaf_root_node` allocates only the header plus the requested number of values, then passes that storage as a full `leaf_fields`. The raw failure is preserved in `.scratch/library-adoption-20261017/host-resource-asan-sdk15-tests.log`.

The correction is published as cpp-btree [`dd984d9`](https://github.com/Kronuz/cpp-btree/commit/dd984d92540674d0794ce338712691ae9afa3d10). It represents the actual allocated header and explicitly constructs trailing storage, preserving compact root sizes and matching allocation/deallocation extents. Native and sanitized layout and mixed-mode suites pass on macOS, Linux and FreeBSD. The downstream Detent host sanitizer gate passes 3/3 in 46.12 seconds with header SHA-256 `b8c68cb395aa8783a159029f9cc0d6c8de41b7f71d954dc5d0c610d756d19885`; UBSan remains enabled. Five uncontended mutation pairs record median CPU +0.43%, with noisy pair differences from -2.16% to +13.37%; actual node footprint and allocation counts match in all 14 cases. The library has no CI workflow. This result does not close the separate immutable-key finding below.

The build also requires coherent SDK discovery on this Intel Mac: Homebrew Clang uses SDK 15 implicit headers while default zlib discovery selects the SDK 26 alias. Selecting SDK 15 for both `CMAKE_OSX_SYSROOT` and the zlib include/library paths builds successfully. An explicit sysroot alone leaves the cached zlib paths unchanged and still fails. This toolchain configuration issue is separate from the runtime sanitizer finding.

## cpp-btree immutable-key audit (October 18)

The compact-layout review separately finds `btree_map_params::swap` modifying `std::pair<const Key, T>::first` through `const_cast` (`btree/btree.h:339` in the layout candidate). Correcting short-root storage does not make that mutation valid. This existing mechanism needs an independent library fix that preserves iterator/reference behavior and move-only mapped values. Reinterpreting a mutable pair as a const-key pair is not an accepted substitute. If preserving the public reference API requires key copies during relocation, measure that cost and record the API/performance decision before adoption. No sanitizer pass should be described as closing this finding.

The immutable-key correction also covers exception-safe mutation. Existing split paths change counts before fallible key copies, insertion increments size before construction, and root promotion can publish before later allocation succeeds. Copy construction needs cleanup for an incrementally constructed tree. Detent's nested definition maps currently lack move constructors, so their apparent moves can copy entire override trees.

The accepted component plan compares two valid candidates before choosing a representation. An indirect map slot owns an actual `pair<const Key, T>` allocated through the supplied allocator; structural changes transfer ownership without moving existing entries. An inline candidate requires all fallible staging for the complete mutation before a proven nonthrowing commit, with explicit supported-type constraints. Both preserve real const-key references. Measure long keys, nested Detent definitions, allocation failure and construction failure, in addition to lookup and mutation costs. The extra allocation/indirection cost and any inline source-compatibility restriction remain unmeasured; no representation decision is made yet.

## Legacy HTTP response bounds (October 19)

The legacy poll/publisher client accumulates wire bytes without a ceiling. Its gzip decoder can expand one compressed input block into an unbounded temporary string, and its zstd call accepts any declared output size. The [HTTP extraction](HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md) includes independently bounded encoded and decoded bodies. Reuse `decompress_zstd(bytes, max_size)` and extend the existing `DeflateDecompressData` implementation for bounded gzip output. A separate gzip implementation in HTTP is excluded.

The compressor candidate is isolated from the published `ed590c8` baseline. It emits at most the existing 16 KiB block size, caps cumulative output before constructing a returned block, validates checksums and end-of-stream, and handles concatenated gzip members under one bound ([RFC 1952, section 2.2](https://www.rfc-editor.org/rfc/rfc1952#section-2.2)). Invalid trailing data and truncation become errors. The optional constructor/reset bound preserves existing source calls but changes the class layout and constructor ABI, requiring a coordinated C++ rebuild. Qualification and measurements remain open; no new compressor revision is adopted yet.

## HTTP allocation ownership (October 19)

HTTP's private `StreamAllocator` and `ResourceDelete` duplicate the resource foundation's ownership mechanics. The candidate now forwards to `allocators::OwnedAllocator` and an additive `OwnedUnique`/`make_owned_unique` helper. The helper supports over-aligned and incomplete types and rolls back failed construction; four native allocator tests pass. The resource foundation's published `b7daa52` implementation remains unchanged in its qualification worktree.

The empty HTTP preset also has a real resource-matching defect: it queries the current process PMR default again when freeing an existing allocation. Switching that default between allocation and destruction can free through a different resource. The forwarding preset captures the default at owner construction, preserving the server's default-resource choice while matching its deallocator. An explicitly supplied shared resource remains owned; a borrowed process default must still outlive its allocations. The regression switches defaults during construction and across surviving weak controls. With the same fixture and compiler, the published HTTP headers fail (exit 1) and the candidate passes (exit 0). All 13 combined native server/client suites pass; refreshed sanitizer and platform qualification remain open. Existing `AllocationContext{}` keeps its process-lifetime `new_delete_resource()` default.

## History note

On 2026-10-17 the 42 newest commits, ending at `488522d`, were rewritten before their first
push to strip `Claude-Session` trailers that five of them carried; their trees are unchanged and
no document cites them. If you remember an older SHA, `.scratch/history-rewrite-2026-10-17.txt`
maps each old SHA to its new one.

## Election readiness deadlines (October 19)

Consensus commit `9f7d43e1` raises its default election interval from 100–200 ms to 1–2 seconds. Two authority distribution fixtures still stop checking after 50 sleeps of 20 ms. The service fixture fails at the same recovered-leader assertion both before and after HTTP migration; the ownership fixture passes or fails depending on election timing. The isolated HTTP run also encounters that ownership assertion in its large-scope case.

Use steady-clock readiness deadlines of five seconds for these small fixtures, preserving the existing 120-second allowance for large recovered-state cases. Keep the library's production defaults. The changed service, ownership and large-scope cases pass with their TLS setup (4/4, 10.84 seconds), then pass three repetitions each (33.48 seconds). The broader suite continues against the original binaries; its election failures are covered by this focused rerun. This finding establishes a fixture deadline mismatch preceding HTTP migration.

## Later completion directory policies (October 19)

Completion `7734544` adds directory-handle ownership, filesystem classification and opt-in handling of shared or symlinked roots after the resource-foundation revision `21809c7`. Existing `acquire_owner()` still locks `owner.lock`; the new `acquire_directory_owner()` is an additive mechanism with different persistence behavior. Detent's storage sessions already use `acquire_owner()`. Replacing that lock would lose exclusion against existing file-lock owners and requires a coordinated compatibility decision, rather than a mechanical substitution.

Carry the newer completion revision into final dependency qualification with a full C++ rebuild. Retain its strict default directory trust policy. File leases protecting mapped generations and the CLI PID lock serve different lifetimes and are not substitutes for a storage-owner lock. The isolated HTTP comparisons retain their frozen completion revision to keep the transport comparison controlled.

## Optional bounded MessagePack (October 19)

The [validated blueprint](MSGPACK_EXTRACTION_ARCHITECTURE.md) places the flat lexer and fixed scratch in `msgpack::bounded` and reuses the bundled packer through one append-to-write adapter. The existing DOM stays enabled by default; bounded-only configuration returns before DOM dependency population. The bundled packer and Xapiand's exception seam remain unchanged. Detent's private encoding engine is removed in the isolated migration. The isolated candidate passes two standalone native and two sanitizer tests, four default DOM/coexistence tests and five Detent distribution tests. Production fixture captures match all 14 logical outputs before and after migration, including maximum 200-byte snapshot keys. The [recorded qualification](../benchmarks/msgpack-extraction-preliminary-20261019.json) remains preliminary: other callers, platforms, clean public pins and production-path cost gates are open.

## Consolidation audit response, October 19

Detent's [item-by-item response](CONSOLIDATION_RESPONSE.md) accepts CL-1, CL-2, CL-3, CL-4, CL-5, CL-7, CL-9, CL-11, CL-17 and CL-18 with explicit compatibility conditions. CL-2 is a correctness defect: the two legacy lock protocols do not exclude each other. Detent should move mutable storage ownership to the canonical `both` protocol after qualified bootstrap and contention tests. Strata's directory-only exception is accepted for its supported builds: it has no released pre-lock owner, and D2 excludes pre-lock development builds. The [follow-up](CONSOLIDATION_RESPONSE.md#accepted-supported-build-boundary-october-20) records that boundary and the create-time bridge required if older released file-lock owners are supported later. Taking both protects against either legacy mode; two old processes using different modes remain unsafe. No production lock call changes with this response.

CRC-32C compatibility names must forward to one engine immediately. Shared directory presets preserve Detent's actual refusal set, and counted-admission consolidation preserves the existing return types and distinct TLS lifetimes through thin facades. CL-17/18 are already implemented in isolated adoption components; their remaining integration gates remain open. No second extraction or competing accounting mechanism is authorized by this response.
