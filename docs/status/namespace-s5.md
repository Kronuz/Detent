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

**Durability** needs no new mechanism: the entries are in the journal, and the image (`DTAIMG03` since S5.2) carries the seed, the directory head and per namespace its position, directory position, tombstone flag and incarnation. Decode checks each live incarnation against its derivation (a tombstone keeps the dropped one), rebuilds the directory map, and recomputes live sizes from the records, whose namespaces are contiguous in key order; it refuses a tombstone with records, a record above its namespace's position, a duplicate or out-of-range directory position, and sizes that do not sum to the state's total.

## Delivery until S6

Today's delta wire gives every record of a transaction the group revision. The ring (`ChangeHistory`) captures every applied transaction with its namespace position and incarnation (S5.2), but the wire serves only entries whose position equals the group revision (`CachedChange::wire`, always before the enabling entry); any other entry, and a drop, raise the wire's floor, so that namespace is delivered by snapshot, whose records and namespace revisions are positions below the group cursor. Nothing proposes the enabling entry or a drop in S5.1 or S5.2: the S6 cutover proposes the enabling entry, and S6 moves cursors and frames to positions.

## S5.2: seals, blocks and the layout on disk

S5.2 (D6, T4) makes every voter persist the same compacted history per namespace: seal files, blocks, bases and the `layout` table, with the checkpoint taken at a seal whose history is durable.

| Piece | Where | What it is |
| --- | --- | --- |
| `LayoutPolicy`, `Limits::layout` | `state.hh` | Seal thresholds, roll-up period P, carry, ratio, α = α_num / α_den, disk cap, and the log thresholds the seal on log progress divides by 8; D90 fixes α = 1/8 and P = 64 |
| `AdministrativeKind::SetLayout` | `state.hh` | The "set limits" entry: its policy takes effect at the next seal, a period change only at a seal whose ordinal the old period divides |
| `State::close_entry`, `State::seal`, `State::sealed_changes` | `state.cc` | Apply's seal: counters, ordinal, and the accumulator of each key's last change since the previous seal |
| `history_format.hh` | `src/detent_authority/` | `DTABLK01` blocks, `DTABAS01` bases, `DTASEAL1` seal files and the `DTALAY01` table, canonical and SHA-256 checked |
| `history::Layout` | `history_layout.hh` | Roll-up, carry, squash by ratio and L, cut by cost or cap, the table at period seals, and the three answers |
| `history::History` | `history.hh` | The directory under the commit protocol: seal, collect, recover, install, repair from a peer |
| `ChangeHistory::floor`, `entries`, `covered` | `change_history.hh` | The ring by namespace position, evicting lazily |

**Seals.** Apply closes each log entry with `close_entry`: a seal falls when the transactions or canonical bytes since the last seal reach the policy's thresholds, or the entries or payload bytes reach an eighth of the log's. The enabling entry is seal 0. Each seal's state is kept (`Node::sealed`); the image carries the counters and the accumulator, so every voter seals at the same indexes after a restart or an install.

**Files.** A seal file names its predecessor and holds, per namespace it changed, its directory entry, its live size and its level-0 section (each key's last change, a tombstone for a delete), plus the roll-up outputs it caused and their digests. A namespace rolls up when (ordinal + directory) % P is 0 or its level-0 reaches L = ⌊live · α / 4⌋; a small touched output is carried in the seal file instead (at most `carry_count` times). Blocks squash while the older is under ratio times the younger; the oldest blocks are cut into the base while the younger ones' bytes reach α · live (cost) or all blocks exceed cap · α · live (cap); the newest block is never cut. The table lists every namespace's base, blocks and level-0 sections and is written at period seals.

**Commit.** Every file is written under `.tmp-<name>`, synced, renamed and the directory synced: the seal file first, then its outputs as one sync group, then the table at a period seal, then the collector unlinks what the table does not reach (keeping every seal file above it and its outputs). A history failure stalls history and checkpoints; apply goes on, and consensus's log bound stops writes.

**Checkpoints.** A checkpoint captures the latest seal's state, and only once history is durable through it, so recovery always finds the table and the seal files that reach the checkpoint.

**Recovery and install.** Recovery reads the table, verifies every file it reaches, chains the seal files above it to the checkpoint's seal and recomputes each, rebuilding a missing output; a table past the checkpoint is adopted and the seals it holds fold nothing. A file that fails its digest is fetched by name from a peer (`history::Fetch`). An install fetches the source's table and files and the seal files above it, verified, before the snapshot is acknowledged.

**The ring.** It captures every applied transaction with its namespace position and incarnation, serves a namespace from `floor(name, incarnation, position)`, and evicts at its bound only entries at or below the newest durable seal's revision; otherwise it refuses the new entry, a gap that costs ring hits on that voter, never a block. The service attaches it before recovery, so replay refills it above the checkpoint.


| Test | Shows |
| --- | --- |
| `test_authority_namespace_positions` | Positions start at stored revisions and move only with their namespace; NoChange and refusals get none; preconditions compare positions; the image round trip; the stepped and synchronous paths agree; the directory stream; drop, tombstone, reclaim, re-creation, the full-copy answer for an old cursor, a retried drop; `erase_range` against an ordered map |
| `test_authority_namespace_cluster` | Three voters with identical entries, records and images after one installs a snapshot taken past the enabling entry and the drop, and one restarts mid-run |
| `test_authority_namespace_power_cut` | A power cut at every storage primitive of a run across the enabling entry, position updates, checkpoints, a drop and a period change, with history on the same disk; recovery with no peer keeps every acknowledged step, the producer's retries reach the uninterrupted state, and the layout reaches every namespace at its position (no checkpoint outran its history) |
| `test_authority_history_layout` | S5.2: formats, the seal trigger, a period change, and churns (drops and re-creations, α 1/8, 1 and D90's P = 64) whose layout matches an independent model at every seal and whose answers reach the head store from every position, with bytes sent per tier; each seal file lists exactly the outputs it writes; replay from the first table reaches every table |
| `test_authority_history_cluster` | S5.2: three voters write byte-identical seal files, blocks, bases and tables with a ring capture failure on every voter at one position, a policy change, a voter restarted mid-period and one installed mid-period; the installed voter answers the same bytes; after every voter restarts, each R from the cut delivers the head store, block answers are unchanged, and the ring and the durable layout tile every position |
| `test_authority_history_power_cut` | S5.2: a power cut at every primitive of the history's seal commits, output groups, tables (the first included), collector and installs, with the period changed mid-run; recovery from the checkpoint a node could persist reaches the uninterrupted directory byte for byte, also from below the table; a rotted seal file is fetched from a peer and refused without one |
| `test_authority_change_history` | S5.2: the ring evicts only at its bound and only entries the durable layout covers |

## Left open

1. The agent side of a drop (generations and `.lease` files of a namespace that leaves the directory) belongs to the agent store, S7: the authority's drop is race-free by construction (one log index, immutable roots for readers, positions never reused), and nothing on the agent reads a directory yet.
2. Tombstones count toward `Limits::namespaces` and are kept: they hold the last position of a dropped name. Once preconditions carry the incarnation (S6), they can be cut like history.
3. Live size is the authority's canonical footprint (key, encoded definition, 16), the unit of `state_bytes`; S5.2's cut compares block bytes against it.
4. Nothing proposes the enabling entry in production yet: the S6 cutover does, with `Limits::layout` as its policy.
5. Production peer fetch for repair and install needs a peer endpoint for history files (`history::Fetch` is the seam; the service passes none). S8's relay endpoint is the natural one.
6. History IO runs synchronously in apply's seal; moving it to the worker's IO operations is a scheduling change, not a format one.
7. The ring keeps the group-revision wire until S6: entries whose position differs from the group revision are kept for namespace answers but never served on today's wire.
