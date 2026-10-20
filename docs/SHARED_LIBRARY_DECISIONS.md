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

- **Key files at mode 0640** are accepted by Xapiand only when the file's group is the one the
  process runs as (for a root start, the group it drops to; otherwise its effective group), and wider modes are refused, so Kubernetes secrets mounted with `fsGroup` work
  (SYS-13). This is stricter than Detent, which accepts group read for any group and does not
  check its TLS key files; Detent may adopt the same rule.
- **A shared PKI helper** (Xapiand's `design/pki`, revision 1 in progress) issues certificates and
  prints SPKI pins; Detent may adopt it as is, with no code change.

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

The [validated blueprint](MSGPACK_EXTRACTION_ARCHITECTURE.md) places the flat lexer and fixed scratch in `msgpack::bounded` and reuses the bundled packer through one append-to-write adapter. The existing DOM stays enabled by default; bounded-only configuration returns before DOM dependency population. The bundled packer and Xapiand's exception seam remain unchanged. Detent's private encoding engine is removed in the isolated migration. Source preparation has no qualification claim until native, sanitizer, platform, byte-equivalence and production-path cost gates pass.
