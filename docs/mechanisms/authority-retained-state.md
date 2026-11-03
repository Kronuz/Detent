> Status: CURRENT (2026-11-03) how the authority node's retained-state envelope is configured, validated and funded, read from `src/detent_authority/node.cc`, `node.hh` and `service.cc` on `phase/retained-state`.

# Retained-state envelope

A node holds recovered and replicated history that its operating limits no longer admit: a journal written under larger limits, a lowered `consensus_limits`, a follower catching up on a full log. The retained-state envelope is the separate, local bound the consensus Worker applies to that history. Detent fixes it independently of the operating limits and refuses to start a node that cannot fund a checkpoint of a full envelope.

## How it works

`NodeOptions::scheduling.retained` is the library's `RetainedStatePolicy`: `command_bytes`, `log_bytes`, `log_entries`, `batch_bytes` and `batch_entries`. Its default initializer copies the `kronuz::consensus::Limits` defaults (1 MiB, 64 MiB, 65,536 entries, 2 MiB, 256 entries), not the library ceilings, so lowering `consensus_limits` does not lower it.

`load_service_options` reads the optional `retained_state` object in `authority.json`. Each member is an unsigned integer that overrides one field; unknown, duplicate, negative, fractional and string members are refused like the other configuration objects.

`validate_node_options` (called by `load_service_options` and by the `Node` constructor) then checks, in order:

1. Each retained field is nonzero and at most the library ceiling (`ceilings`). Message: `authority retained_state.log_bytes 0 is outside 1 to 17179869184`.
2. Each retained field is at least the operating limit it covers (`consensus.command_bytes`, `log_bytes`, `log_entries`, `rpc_bytes`, `rpc_entries`). Message: `authority retained_state.log_bytes 67108863 is below the operating limit 67108864; raise retained_state.log_bytes`.
3. The replacement pool funds `application.image_bytes` + `checkpoint_detail::encoded_bound({retained.log_entries, retained.log_bytes})` + 4096. The sum is checked for 64-bit overflow, and the message names each term, the pool, the requirement and the shortfall:

```
authority runtime cannot fund configured limits: the replacement pool holds 72690163 bytes but the application image (4194304) + the encoded retained suffix (68491764) + the publication allowance (4096) need 72690164, short by 1 byte; raise journal_limits.replacement_bytes, or lower application_limits.image_bytes or retained_state.log_bytes and log_entries
```

Step 2 makes the retained suffix at least the operating suffix, so step 3 covers the old `maximum_size(consensus)` check.

`Node::advance_application` decodes each committed command group under `scheduling.retained.command_bytes` ([historical decoding](authority-historical-decoding.md)). The Worker and Core take the same policy for recovery, follower replacement, checkpoint decoding and output accounting.

## Why this mechanism

The library defaults the policy to its ceilings (16 GiB of log), which no replacement pool can fund. Detent supplies a finite envelope equal to the largest operating profile the configuration can express: `consensus_limits` only lowers `log_entries` and `log_bytes`, and the command, RPC and batch bounds are not configurable. History written by any configuration therefore fits the default envelope, and a node that lowers its operating limits keeps recovering it.

## Invariants

1. A configuration without `retained_state` gives the envelope above: `retained_envelope_defaults` in `tests/detent_authority/test_node.cc`, and the `{}` case in `test_service.cc`.
2. The pool boundary: exactly `image + suffix + 4096` starts a node, one byte less is refused with the exact message (`retained_funding`; `test_service.cc` repeats it through `load_service_options`).
3. Lowering `consensus_limits` leaves the requirement unchanged (`retained_funding` runs on a profile that already lowers the operating log; `test_service.cc` lowers it through JSON).
4. A retained field below its operating limit, zero or above the ceiling is refused with its message (`retained_validation`, all five fields).
5. A journal with more entries than the operating `log_entries` recovers under the default envelope (`lowered-count-application`). An explicit envelope below the stored history refuses recovery and leaves every file at its size (`lowered-envelope-application`).
6. The retained values are local: they are in no batch, image, journal record, checkpoint or peer fingerprint. `semantic_profile` writes the ten application limits only, `DTABAT02` binds those same ten, and `peer_configuration` hashes the application profile, configuration and operating limits. Voters may configure different envelopes.

## Code

`validate_retained_state`, `validate_replacement_funding` and `validate_node_options` in `src/detent_authority/node.cc`; `NodeOptions::scheduling` in `src/detent_authority/node.hh`; the `retained_state` block in `load_service_options`, `src/detent_authority/service.cc`. Design: [retained-state architecture](../RETAINED_STATE_ARCHITECTURE.md).
