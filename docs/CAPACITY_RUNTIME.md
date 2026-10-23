# Runtime capacity profiles

**Status:** Startup capacity configuration passes native qualification; large-system qualification remains open. No new supported system ceiling is declared.

Authority and managed delivery configuration can set capacity without recompiling. Defaults remain unchanged. The [qualification plan](CAPACITY_QUALIFICATION.md) targets a fresh group with capacity for 5M knobs, then grows its actual contents through the cardinality matrix. A configuration accepted at startup is not evidence that the hardware sustains that workload.

## Authority

Merge the following settings into each voter's existing private authority configuration. All voters must have identical `application_limits`; those limits participate in the replicated semantic profile.

```json
{
  "application_limits": {
    "keys": 5000000,
    "state_bytes": 805306368,
    "image_bytes": 1073741824
  },
  "journal_limits": {"hard_bytes": 4294967296, "replacement_bytes": 2147483648},
  "delivery_limits": {
    "wire_bytes": 536870912,
    "records": 5000000
  }
}
```

`application_limits` forwards the existing `Limits` fields: `keys`, `namespaces`, `producers`, `mutations`, `request_bytes`, `state_bytes`, `image_bytes`, `definition_bytes`, `overrides` and `results`. The key count is bounded by the existing 32-bit record format, and image sizes must fit signed file offsets. Logical state plus namespace/producer receipt metadata must fit the application image budget. Request and mutation bounds still apply independently; increasing the total key count does not increase a transaction's admissible size.

`delivery_limits` additionally accepts `records`, `namespaces`, `string_bytes`, `overrides` and `definition_bytes`, alongside its existing `heap_bytes` and `wire_bytes`. These are per-representation admission bounds; namespace subscriptions may carry much less than the complete authority state.

Larger application images can require more journal funding. Optional `journal_limits` forwards `hard_bytes`, `hard_entries`, `control_bytes`, `control_entries`, `replacement_bytes` and `replacement_entries` into the existing library admission pools. A replacement must fund the application image, checkpoint framing/log suffix and overhead. Checks use subtraction to avoid overflow. Journal admission still validates the protected pools against the hard budget; configuring a larger logical allowance does not reserve physical disk space or certify available storage.

Existing groups retain their semantic-profile checks. Changing their `application_limits` remains unsupported until the versioned online capacity-transition component is implemented. Do not bypass equality checks, delete state or reinterpret old batches under new limits. Updating the executable with unchanged settings preserves existing profiles and bytes.

## Managed relay and agent

Merge this envelope into the existing private managed source configuration used by either daemon:

```json
{
  "image_format": "paged-v2",
  "heap_bytes": 3221225472,
  "mapping_bytes": 3221225472,
  "disk_bytes": 17179869184,
  "image_limits": {
    "records": 5000000,
    "wire_bytes": 536870912,
    "decoded_bytes": 2147483648,
    "image_bytes": 1610612736,
    "pages": 12000000,
    "segments": 16384
  }
}
```

`image_limits` also accepts `namespaces`, `wire_bytes`, `string_bytes`, `overrides` and `definition_bytes`. It forwards to the existing decoder, view, paged-builder and recovery types. `decoded_bytes` must fit the configured heap budget; image bytes must fit the mapping budget. Record counts, pages, segment slots and payload bytes are independent maxima, not a promise that all maxima fit simultaneously. Actual allocations remain charged to the shared heap ledger, and actual packs remain checked against their byte bound.

Paged builders, candidate validation, recovery, compaction and collection use the supplied page and segment bounds. Internal root equality checks validate format framing without silently reapplying the smaller default admission profile. Segment metadata reservation uses checked vector sizes and the existing ledger; payload bytes are enforced when entries are admitted.

The paged SDK accepts the corresponding `ViewLimits`, including page and segment counts. Those new fields change the public C++ struct layout; rebuild consumers with this update. CLI reader configuration passes focused native and real process qualification in the [reader blueprint](READER_CAPACITY_ARCHITECTURE.md); the actual CLI at 1M and automatic profile discovery remain open. The fixed 4 MiB legacy shared-memory path is unchanged and is not used to qualify high-cardinality managed delivery.

## Qualification harness

`run_fleet.py --capacity-config tests/detent_system/presets/capacity-5000000.json` supplies the same explicit profile to the authority, relay source, agent and independent SDK oracle. The scenario retains the actual record count. Without an explicit profile, scenario and mixed-writer validation retain their 500K defaults.

Resource sampling starts before seeding, continues through bootstrap and workload verification, and records per-process RSS, virtual size and CPU. The Python driver is reported separately and is included in the workload memory guard. RSS maxima from that stream are sampled lower bounds; virtual size includes the process address space, not just data mappings. Native SDK oracles separately report kernel peak RSS before teardown and their open/verification times. The driver, service and mapped-reader measurements must remain distinct.


## Qualification progress, October 23

The final startup-capacity candidate `0c1e5ad` passes the complete 217-test product suite in 734.00 seconds, including the seven focused capacity regressions. The harness passes 54 Python tests, Ruff and Pyrefly with zero errors. This covers the separate driver-memory addition; it does not qualify the large-system operating envelope.

The three-voter, two-relay, two-agent 1K pilot completes independent SDK verification, one-key and 128-key durable updates, the short fleet workload and clean process teardown. This is a functional pilot, not qualification of the 5M target or sustained performance. Its source is `afe0837`; the raw evidence remains in `.scratch/capacity-trials/1000-r2/`. It predates separate Python-driver RSS sampling.

Final review finds no remaining production blocker in the four corrections: relay encoding inherits configured admission limits; segment discovery preserves the admitted root bounds; response envelopes use checked arithmetic under the existing 4 GiB fragment-directory bound; relay aggregate heap funding rejects overflow before accumulation. Complete large-image traversal, recovery and the 1M → 5M → 10M trials remain open.
