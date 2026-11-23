> Status: CURRENT (2026-11-22) how an authority group's limits rise at a coordinated restart (D141), read from `src/detent_authority/state.cc` and `node.cc`.

# Raising authority capacity at a restart

An operator raises `application_limits.keys`, `state_bytes` or `image_bytes` by stopping every
voter and starting all of them with the larger values. No byte on disk or on the wire changes:
every `DTABAT02` group, `DTAADM01` administrative entry and `DTAIMG03` image already records all
ten limits, and each one is read and applied under the limits it records. Relays and agents keep
running, keep their generations and stores, and see only a pause in publication. The contract is
[online capacity](../ONLINE_CAPACITY_ARCHITECTURE.md) rules 1-7.

## Admission

`admit_limits(recorded, configured)` is the one rule. `keys`, `state_bytes` and `image_bytes` may
be larger in the configuration than in the record; the other seven are equal. Anything else
throws, naming the first field in encoded order:

```text
authority application_limits.keys 3 is below the recorded 4; limits only rise
authority application_limits.results 1 differs from the recorded 256; only keys, state_bytes and image_bytes rise at a restart
```

One table, `limit_fields` in `state.cc`, lists the ten limits in their encoded order and marks
the three that rise. `write_limits`, `read_limits` and `admit_limits` all walk it, so the encoded
order, the decoded order and the admission order cannot drift apart.

`read_limits` reads a record's ten limits, admits them and returns them with the configured
`layout` (which is not part of the record). It runs at three places:

1. `BatchDecoder`: a group's limits become `BatchDecoder::limits()`, and its commands decode
   under them.
2. `decode_administrative` and `recorded_limits(entry, configured)`: an administrative entry's.
3. `ImageDecoder`: an image's. The recovered `State` runs under the configured limits, so a later
   capture records them; the image's rows are bounded by the configured limits, which are at
   least the recorded ones.

## Each entry decides under its own limits

`Node::advance_application` decodes each command of a group with
`CommandDecoder(bytes, decoder->limits(), context)`, so the `ValidatedCommand` carries the
group's limits, and `CommandApplication` takes every replicated decision from
`validated.limits()`. An administrative entry applies through
`AdministrativeApplication(state, index, term, command, recorded_limits(payload, configured))`,
whose layout check uses that profile. A group that failed `Capacity` at the old key ceiling fails
the same way on every replay, on every voter, whatever each was configured with when it replayed.

A new proposal is encoded under the configured limits (`ProposalBuilder`, `propose_administrative`),
so after the raise new entries carry the raised limits. Log order puts every pre-raise entry before
any post-raise one; were an old entry ever applied after a new one, it would still decide the same
way on every voter, because it carries its limits.

A producer's retry of a sequence accepted before the raise returns its original receipt: the
command digest is SHA-256 of the `DTACMD01` encoding, which depends on `request_bytes`,
`mutations`, `definition_bytes` and `overrides` only as validation bounds, none of them raisable.

`State::apply`, `apply_batch`, `administer`, `BatchApplication::advance` and the four-argument
`AdministrativeApplication` take an entry recorded under the state's own `limits()`. Tests use
them; the node's replay does not.

## Peers

The peer fingerprint keeps binding `semantic_profile(application)` (`peers.hh`). Voters with
different limits refuse each other, so a group runs one profile at a time. A rolling restart works
but stops publication until a quorum shares the new limits; reads continue from relays and agents
throughout ([actors](../ACTORS.md)). Snapshots between voters therefore carry an image the
receiver admits.

## Every use of `State::limits_`

Replicated: decides an entry's outcome, so it must use the limits the entry carries. Local: bounds
this process (an allocation, an encoding, a decode), so it uses the configured limits.

| Site | Use | Class |
| --- | --- | --- |
| `State::State` | validates the configured limits | local |
| `State::apply`: `digest(c, limits_)`, `footprint(…, limits_)` | digest and footprint, independent of the three raisable fields | replicated, under `limits()` |
| `State::apply`: `producers`, `namespaces`, `state_bytes`, `keys`, `results` | `Capacity` decisions and the receipt window | replicated, under `limits()` (tests only) |
| `CommandApplication`: `producers`, `namespaces`, `keys`, `state_bytes`, `results` | the same decisions | replicated, under `validated.limits()` |
| `CommandApplication::Impl` constructor | `admit_limits(validated.limits(), s.limits())` | admission |
| `AdministrativeApplication` constructor | `admit_limits(profile_, source.limits_)`; `validate_administrative` under `profile_` | admission, replicated |
| `AdministrativeApplication::step` (`SetLayout`) | `history::file_bytes` and `fetch_bytes` under `profile_` | replicated |
| `ImageEncoder::row`, `next` | the image's byte bound, its recorded limits, its definition encoding | local |
| `ImageDecoder::Impl::row` | `read_limits` admission, then row bounds (`keys`, `state_bytes`, `namespaces`, `producers`, `results`, accumulator) | admission, local |
| `ImageDecoder::feed` | input and row byte bounds | local |
| `history_layout.cc` `Layout::seal` | `encode_definition` bounds (`definition_bytes`, `overrides`: fixed) | local |
| `namespace_history.cc` `out->limits_` | change decoding bounds (fixed fields) | local |
| `layout_export.cc`, `service.cc` `exporter.file(…, sealed()->limits())` | history file decoding bounds | local |

`service.cc` reads only the configured limits (`o.node.application`): credential and namespace
counts, request and mutation bounds, the history fetch lane (`fetch_bytes`), and the profile it
validates at startup. All of them are local.

## The layout in force under raised limits

Block files scale with α · `state_bytes`, so a policy an enabling or `SetLayout` entry admitted
under the old limits could write files above a peer fetch once `state_bytes` rises, and a lagging
voter could not fetch them (`peer_history.hh`). `Node::checked` covers only the configured
`layout`, which seeds an enabling entry. `check_group_policy` (`node.cc`) covers the group's: after
an image recovers and after each administrative entry applies, it refuses when
`history::file_bytes(configured, policy, pending)` exceeds `history::fetch_bytes(configured)`,
naming the field that lifts the fetch:

```text
authority application_limits.image_bytes 33554432 is below the 42074367-byte history files the group's layout policy writes; raise it with state_bytes
```

Without a raise it never fires: an entry deciding under the configured limits already refuses a
policy whose files would outgrow a fetch. Raising `image_bytes` with `state_bytes` keeps every
admitted policy within a fetch.

## Tests

1. `tests/detent_authority/test_state.cc` `restart_raise`: a group that failed `Capacity` at
   keys 2 replays with the same results under keys 4; its retry returns the receipt; a new group
   grows to three keys; an image recorded at keys 2 restores under keys 4 and then records them;
   a group that failed `Capacity` on `state_bytes` 512 replays the same under 4096; lowered and
   changed limits refuse images, groups and administrative entries with the messages above; a `SetLayout` recorded under the old `image_bytes` gets `Conflict` under the raised
   configuration too, and the same entry recorded under the raised limits applies.
2. `tests/detent_authority/test_node.cc` `restart_raise`: a node at keys 2 checkpoints, fails a
   third key, restarts at keys 4, replays the failed group from its log tail, returns the
   original receipt to the retry and grows to four keys; restarted at keys 3 it refuses to
   recover, naming `keys`. The changed-profile case names `keys` and `results` in its fence.
   `restart_raise_layout`: a `SetLayout` that got `Conflict` under the old `image_bytes` stays
   in the log tail, replays after the raise with no pending policy, and applies when proposed
   again; restarted with `state_bytes` raised alone, the node refuses to recover, naming
   `image_bytes`.
3. `tests/detent_system/test_restart_capacity.sh`: three voters at keys 1,000 refuse the
   1,001st, restart at keys 2,000 while a relay and two agents keep running, return the
   original receipt to the retry and grow to 2,000; both agents keep their cursor's cluster and
   incarnation and serve every value to the SDK oracle.
