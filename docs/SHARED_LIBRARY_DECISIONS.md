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
  within a second of the first write left the survivors unable to elect). The loop will catch,
  log, and continue. Detent's handlers are unaffected unless they throw, in which case the loop
  now survives instead of dying.
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

- **Key files at mode 0640** are accepted by Xapiand only when the file's group is the process's
  effective group, and wider modes are refused, so Kubernetes secrets mounted with `fsGroup` work
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

## History note

On 2026-10-17 the 42 newest commits, ending at `488522d`, were rewritten before their first
push to strip `Claude-Session` trailers that five of them carried; their trees are unchanged and
no document cites them. If you remember an older SHA, `.scratch/history-rewrite-2026-10-17.txt`
maps each old SHA to its new one.
