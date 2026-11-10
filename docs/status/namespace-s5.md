# Namespace S5: positions per namespace on the authority

S5.1 of [namespace delivery](../design/namespace-delivery.md) (D3, D5, W1, W3, W7). The authority keeps a position, an incarnation and a live size per namespace, and a directory of namespaces, all assigned at apply on today's one consensus group. S5.2 (seals, blocks, the first table) and S6 (cursors and frames on the wire) build on this contract, `namespace-positions`.

## The contract

| Piece | Where | What it is |
| --- | --- | --- |
| `Namespace` | `src/detent_authority/state.hh` | Per name: `position`, `incarnation`, `directory`, `bytes` (live size), `live` |
| `NamespaceCursor` | `state.hh` | {cluster, incarnation, position}, design W1 |
| `State::namespace_cursor`, `State::answer` | `state.cc` | A cursor's answer: `Current`, `Behind` (changes after it), `Ahead` (this replica must catch up), `FullCopy` (another cluster or incarnation, W7), `Absent` (never created, or dropped) |
| `State::for_each_directory_entry(after, f)` | `state.hh` | The directory stream: every namespace whose latest directory change is above `after`, in directory order, with its entry |
| `State::directory_position` | `state.hh` | The directory's own position |
| `AdministrativeCommand` | `state.hh` | `EnablePositions{nonce}` and `DropNamespace{name, incarnation}`, payload `DTAADM01` |
| `Node::propose_administrative` | `node.hh` | Proposes one administrative entry |

## How it works

**Before the enabling entry** a namespace's `position` is the group revision of its last change, as before S5, and records carry group revisions. Live sizes are kept from the start, so enabling needs no scan.

**The enabling entry** is one administrative entry with a nonzero nonce, the seed of every incarnation. Applied at one index on every voter, it walks the namespaces in name order in bounded steps (one per `CommandWorkBudget::updates`), giving each the next directory position and the incarnation `derive_incarnation(context, seed, directory, name)`, the first 16 bytes of a SHA-256 over those inputs. Each namespace keeps its stored revision as its starting position, which is at least every record revision in it, so the reader's checks hold. A second enabling entry is `NoChange`.

**After it**, a transaction that applies and changes a key in namespace N gives N position + 1; its records carry that position and the result's `namespace_revision` is it. The group revision still counts every change, so `position <= revision` always. Refusals and `NoChange` get no position. A write to an unknown name creates it at the next directory position with position 1.

**A drop** names the namespace and the incarnation it drops, so a retried drop never removes a re-creation (`Conflict`); a drop of an absent or already-dropped incarnation is `NoChange`, and one before the enabling entry is `Conflict`. It is one change at the next position: the namespace's key range `[N:, N;)` leaves the record map through a persistent split and join (`PersistentMap::erase_range`, O(log N) new nodes), its size leaves the state's total, and its entry becomes a tombstone at the next directory position that keeps the position and the dropped incarnation. The removed nodes and definitions retire through the existing bounded `collect()` once no root holds them (tombstone, then reclaim). A write after a drop re-creates the name under a new incarnation and continues from the tombstone's position: positions are never reused under one name, so a precondition taken against the old incarnation (`expected_namespace`, `absent_namespace_revision`) is a `Conflict` even before preconditions carry the incarnation (W4, S6).

**The directory** maps each directory position to a name, one entry per name: a creation, re-creation or drop moves the name's entry to the head. A consumer at directory position D reads the entries above D and gets exactly the names created, re-created or dropped since, at their latest state.

**Durability** needs no new mechanism: the entries are in the journal, and the image (`DTAIMG02`) carries the seed, the directory head and per namespace its position, directory position and tombstone flag. Decode derives each incarnation, rebuilds the directory map, and recomputes live sizes from the records, whose namespaces are contiguous in key order; it refuses a tombstone with records, a record above its namespace's position, a duplicate or out-of-range directory position, and sizes that do not sum to the state's total.

## Delivery until S6

Today's delta wire gives every record of a transaction the group revision. The change history captures a transaction only when its namespace's position equals the group revision (always, before the enabling entry); otherwise it records a gap and that namespace is delivered by snapshot, whose records and namespace revisions are positions below the group cursor. A drop resets the history at publication. Nothing proposes the enabling entry or a drop in S5.1: S5.2 proposes the enabling entry with the first base, and S6 moves cursors and frames to positions.

## Evidence

| Test | Shows |
| --- | --- |
| `test_authority_namespace_positions` | Positions start at stored revisions and move only with their namespace; NoChange and refusals get none; preconditions compare positions; the image round trip; the stepped and synchronous paths agree; the directory stream; drop, tombstone, reclaim, re-creation, the full-copy answer for an old cursor, a retried drop; `erase_range` against an ordered map |
| `test_authority_namespace_cluster` | Three voters with identical entries, records and images after one installs a snapshot taken past the enabling entry and the drop, and one restarts mid-run |
| `test_authority_namespace_power_cut` | A power cut at every storage primitive of a run across the enabling entry, position updates, checkpoints and a drop; recovery keeps every acknowledged step and the producer's retries reach the uninterrupted state |

## Left open

1. The agent side of a drop (generations and `.lease` files of a namespace that leaves the directory) belongs to the agent store, S7: the authority's drop is race-free by construction (one log index, immutable roots for readers, positions never reused), and nothing on the agent reads a directory yet.
2. Tombstones count toward `Limits::namespaces` and are kept: they hold the last position of a dropped name. Once preconditions carry the incarnation (S6), they can be cut like history.
3. Live size is the authority's canonical footprint (key, encoded definition, 16), the unit of `state_bytes`; S5.2's cut compares block bytes against it.
