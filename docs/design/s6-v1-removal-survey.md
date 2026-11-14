# S6 version 1 removal survey

Snapshot: `Detent:a900571a` (`main`), 2026-11-13. This is a source survey, not an implementation or a format approval. Only this report changes. The inventories below record matching lines at this snapshot; names such as `Cursor` also include explicitly listed unrelated nested cursors.

## Recommendation and boundary

Choose one table entry and reader per followed namespace (namespace-delivery D8). D88 explicitly limits the scope-named entry to the cutover. The existing store table already holds `NamespaceCursor` and multiple entries; retaining a synthetic scope entry would require inventing an aggregate incarnation/position for independent namespace streams. W1 provides no such aggregate cursor. That is protocol and recovery machinery with no existing contract, rather than a cheaper spelling of the same state.

The old wire is **not** an HTTP `/v1` route: both the version 1 JSON session body and the version 2 body use `/v2/distribution/session/{open,next,close}`. The old one-shot route is `POST /v2/distribution`. Keep `/v1/health` and the authority producer/recovery formats: their version numbers are independent. `session_frames_version = 1` is likewise the version of the W6 frame envelope, not the legacy session body.

## Migration seams

| Area | Current evidence | Required change |
|---|---|---|
| Common cursor | `include/detent/cursor.hh:8,19`; `src/detent_authority/state.hh:102` | Delete the 88-byte-field old cursor (16+16+16+32+8); move the authority's duplicate W1 definition to the common 40-byte-field type (16+16+8). These are field sums, not measured object sizes. Keep `DirectoryCursor`. |
| Authority | `src/detent_authority/service.cc:939,1009,1199,2308,2473`; `export_scope.{hh,cc}`, `export_capture.{hh,cc}`, `delivery_export.{hh,cc}`, `subscription.{hh,cc}` | Remove exact-name grants and old exports/subscriptions only after all consumers use the history-backed v2 branch. Preserve history exports and v2 grants. |
| Client | `src/detent_delivery/client.cc:104,109,123`; `namespace_session.cc:364,375,383` | Delete v1 request/response and one-shot receive paths; use W5 Following and namespace sessions. Open's session JSON and W6 frames remain distinct decoders. |
| Coordinator/agent | `include/detent_delivery/coordinator.hh:17,21,69`; `src/detent_delivery/coordinator.cc:983,986,1215,1219`; `src/detent_agent/managed_host.cc:164,185,193` | Replace one selected store/received frontier with per-namespace state; one session feeds them. Host currently creates exactly one coordinator and one store. Readiness must define all configured namespaces and dropped namespaces. |
| Local store | `src/detent_local/disk_store.cc:273,372`; `src/detent_local/derived_ids.cc:58,76`; `include/detent_local/store_table.hh:19,30` | Remove synthetic scope-name and incarnation derivation; pass actual namespace name/incarnation/position through builders, recovery and readers. Keep atomic table publication and per-generation leases. |
| Relay | `src/detent_relay/service.cc:208,433`; `sessions.cc:113`; `namespace_relay.cc` | Move old downstream sessions and upstream delivery together, preserving per-namespace continuity, directory progression, and drop signaling. |
| Memory/atomicity | `include/detent_delivery/namespace_session.hh:49`; `include/detent_distribution/protocol.hh:44`; `src/detent_distribution/session_frames.cc` | Admit W5/W6 allocations to the existing ledger and stage every frame until the entire HTTP/fragments response validates. A corrupt second fragment must publish nothing. |

## Cost of the table-entry decision

| Aspect | Scope-named entry (D88 retained) | One entry per namespace (D8) |
|---|---|---|
| Session frontiers | Still needs N W1 cursors to form Following | N W1 cursors are the stored frontiers |
| Table bytes | One synthetic name (currently 64 hex characters), but cannot represent N independent frontiers | `96 + sum(137 + len(name))` encoded bytes; `store_table.cc:11,12,59,79`. For N=4096 and 16-character names: 626,784 bytes. Existing maximum bound is `96 + N*392`. |
| Generation identity | Requires a new aggregate cursor/ID rule; old group revision cannot stand in for independent positions | Existing `derive_generation(profile, incarnation, position, base, ordinal)` already accepts W1 inputs (`derived_ids.cc:41`) |
| Publication | One merged generation implies rewriting/partitioning records and defining cross-namespace full-copy semantics | Stage changed namespace generations, then one `AgentStore::publish` cycle with dropped names (`agent_store.cc:427-496`); unchanged entries remain |
| Readers | Current one coordinator/reader largely retained, but lookup must map N namespaces into aggregate state | Reader and readiness fanout; table selection and reader lifetime must hold one coherent table generation |
| On-disk effects | New aggregate metadata required; D88's synthetic incarnation cannot equal an authority namespace incarnation | Table format already supports N W1 entries. Paged roots and legacy image descriptors still encode the old cursor and require a format revision/rebuild. No table format bump needed solely to add entries. |
| Risk | Hides a second identity mechanism and new recovery semantics | More per-namespace runtime objects and admission pressure; measure active/idle namespace overhead before freezing limits |

`paged_layout.cc:344-348,367-371` writes/reads old cursor fields at fixed offsets: revision 56, cluster 64, group 80, restore epoch 96, scope 112. Replacing the type alone corrupts interpretation. Audit the root checksum/profile and every descriptor encoder, update format version and corruption fixtures, and explicitly refuse old images (D43 rejects compatibility). Derived generation/segment names also change when the authority's real incarnation replaces the synthetic one, so stale unreachable files are collectible, never silently adopted.

A source count is not a patch-size estimate. The inventory quantifies the lexical blast radius below; the coordinator's state machine, disk descriptors and staged sink are semantic rewrites, so the previous 3–5K-line estimate in D93 should remain provisional until the design review fixes the table choice. D8's table encoder already supports both cardinalities; neither choice earns a new table implementation.

## Recommended commit order

1. Define the single W1 cursor in the common header; migrate authority references and establish format/refusal fixtures in the quest branch. Keep the whole branch unlanded until every consumer moves.
2. Convert distribution snapshots/deltas/frames, IDs and paged roots; change format versions and explicit rejection tests together. Remove old synthetic identity helpers.
3. Add ledger-backed W5/W6 decoding and a staged sink; kill the early-apply mutant with a corrupt second fragment test before wiring publication.
4. Move coordinator and agent to one session plus per-namespace state; publish changed/dropped entries in one table cycle. Cover bootstrap, directory-follow-all, repair, incarnation reset, restart, and readers held across publication.
5. Move the complete relay chain; prove live authority → relay → relay → agent equivalence and namespace-drop delivery.
6. Delete old wire, `AuthorizedScope`, poll configuration, old cursor and unused tests/bench paths; refresh configuration/docs examples and the stale tier comment in `include/detent_distribution/history.hh`. Run grep, suites, crash/refusal checks and system cases at the final branch head before landing as one quest.

## Reproduce the inventory

Run from the pinned tree:

```sh
git -C Detent rev-parse HEAD
rg -n '\bCursor\b|AuthorizedScope' Detent/{src,include,tests,benchmarks,examples,tools,scripts}
rg -n 'DeliveryMode|poll_interval|"poll"|--poll' Detent/{src,include,tests,benchmarks,examples,tools,scripts}
rg -n 'SessionResponsePurpose|version|/v2/distribution' Detent/{src,include,tests,benchmarks,examples,tools,scripts}
```

The last search intentionally includes v2; inspect each version discriminator rather than removing every line containing `1`. Inventory line numbers below are grouped by file. Nested `persistent_map::Cursor`, `ExportCapture::RecordCursor` and parser-state `Cursor` are traversal machinery and must survive; the common type and its consumers are the migration set. Historical mutant patches and docs are not executable consumers.

## Old cursor inventory

305 matching lines in 113 files.

| File | Lines |
|---|---|
| `benchmarks/delta_decode_bench.cc` | 16, 28 |
| `benchmarks/local_corpus.hh` | 19, 28 |
| `benchmarks/paged_recovery_bench.cc` | 38, 42 |
| `include/detent/cursor.hh` | 8, 12, 13 |
| `include/detent/read_view.hh` | 36, 56, 64 |
| `include/detent_delivery/client.hh` | 30, 31, 48 |
| `include/detent_delivery/coordinator.hh` | 25, 69 |
| `include/detent_delivery/response.hh` | 10, 32 |
| `include/detent_distribution/delta_encoder.hh` | 24, 25 |
| `include/detent_distribution/encoded_update.hh` | 12, 14, 15, 29 |
| `include/detent_distribution/protocol.hh` | 74, 108, 115, 116, 168, 169, 201, 209, 211 |
| `include/detent_distribution/publication_window.hh` | 20, 24, 27, 33, 55, 73 |
| `include/detent_distribution/session_table.hh` | 49, 148, 149, 207, 247, 250, 277, 294 |
| `include/detent_distribution/snapshot_encoder.hh` | 20 |
| `include/detent_distribution/update_cache.hh` | 20, 30, 32, 33, 34, 38, 41, 42, 43, 50, 58 |
| `include/detent_distribution_http/reply_pump.hh` | 26 |
| `include/detent_distribution_http/session_request.hh` | 25, 27 |
| `include/detent_local/client_source.hh` | 9, 34 |
| `include/detent_local/derived_ids.hh` | 26, 28, 30, 40 |
| `include/detent_local/disk_store.hh` | 65, 82 |
| `include/detent_local/generation.hh` | 29, 88, 90, 94, 97, 102 |
| `include/detent_local/identity_reset.hh` | 9 |
| `include/detent_local/layout.hh` | 18 |
| `include/detent_local/paged_layout.hh` | 49 |
| `include/detent_local/reader.hh` | 11, 23 |
| `include/detent_local/view.hh` | 42, 72, 82 |
| `include/detent_relay/bundle.hh` | 8, 16, 17, 27, 31, 32, 54, 55, 56, 70 |
| `include/detent_relay/registry.hh` | 6, 21 |
| `include/detent_relay/sessions.hh` | 9, 40, 44 |
| `src/detent/read_view.cc` | 34 |
| `src/detent_agent/main.cc` | 59, 420 |
| `src/detent_agent/managed_host.cc` | 35 |
| `src/detent_authority/delivery_export.cc` | 12, 23, 44, 180 |
| `src/detent_authority/delivery_export.hh` | 12, 21 |
| `src/detent_authority/delta_export.cc` | 17, 29, 46, 47, 219, 230 |
| `src/detent_authority/delta_export.hh` | 13 |
| `src/detent_authority/export_capture.cc` | 22 |
| `src/detent_authority/export_capture.hh` | 19, 33, 37, 55 |
| `src/detent_authority/export_encoder.cc` | 8 |
| `src/detent_authority/persistent_map.hh` | 158, 178, 182, 202, 203 |
| `src/detent_authority/service.cc` | 531, 2292 |
| `src/detent_authority/service.hh` | 104 |
| `src/detent_authority/state.cc` | 2105 |
| `src/detent_authority/state.hh` | 373, 449, 450, 451, 452 |
| `src/detent_authority/subscription.cc` | 21, 22, 51, 60, 74, 80 |
| `src/detent_authority/subscription.hh` | 21, 22, 27, 32, 34, 36 |
| `src/detent_delivery/client.cc` | 37, 53, 303, 307 |
| `src/detent_delivery/configuration.cc` | 256 |
| `src/detent_delivery/coordinator.cc` | 850 |
| `src/detent_delivery/response.cc` | 44, 45, 72, 317 |
| `src/detent_delivery/response_probe_internal.hh` | 9 |
| `src/detent_distribution/delta_encoder.cc` | 29 |
| `src/detent_distribution/delta_source.cc` | 21, 22, 164 |
| `src/detent_distribution/encoded_update.cc` | 6, 34 |
| `src/detent_distribution/protocol.cc` | 243, 257, 440, 447, 553, 581, 600, 829, 838 |
| `src/detent_distribution/session_frames.cc` | 393, 616, 618 |
| `src/detent_distribution/snapshot_source.cc` | 14 |
| `src/detent_distribution/update_cache.cc` | 19, 50, 74, 84, 87, 90, 94, 116 |
| `src/detent_distribution_http/reply_pump.cc` | 62 |
| `src/detent_distribution_http/session_request.cc` | 79 |
| `src/detent_local/builder.cc` | 20 |
| `src/detent_local/client_source.cc` | 58, 158 |
| `src/detent_local/derived_ids.cc` | 58, 68, 76, 86 |
| `src/detent_local/disk_store.cc` | 221, 332 |
| `src/detent_local/generation.cc` | 130, 170, 216, 220, 234, 567 |
| `src/detent_local/leased_paged_root.hh` | 83, 85 |
| `src/detent_local/paged_overlay_assembler.cc` | 71, 178 |
| `src/detent_local/paged_overlay_assembler.hh` | 10 |
| `src/detent_local/paged_plan.cc` | 40, 45 |
| `src/detent_local/paged_plan.hh` | 69, 70 |
| `src/detent_local/paged_root_prefix.cc` | 40, 93 |
| `src/detent_local/paged_root_prefix.hh` | 6 |
| `src/detent_local/paged_snapshot_builder.cc` | 51, 80, 194, 269 |
| `src/detent_local/paged_snapshot_source.hh` | 10 |
| `src/detent_local/prepared_reader.cc` | 95 |
| `src/detent_local/prepared_reader.hh` | 18 |
| `src/detent_local/reader.cc` | 9 |
| `src/detent_local/recovery_context.hh` | 27, 30 |
| `src/detent_local/snapshot_source.cc` | 15 |
| `src/detent_local/view.cc` | 275, 279, 324 |
| `src/detent_relay/bundle.cc` | 9, 16, 177, 182, 202 |
| `src/detent_relay/registry.cc` | 10, 97 |
| `src/detent_relay/service.cc` | 49 |
| `src/detent_relay/sessions.cc` | 45, 92 |
| `src/detent_testing/generation_writer.cc` | 28, 103, 180 |
| `tests/detent_agent/test_agent_catch_up.cc` | 32, 37 |
| `tests/detent_authority/test_delivery_export.cc` | 86 |
| `tests/detent_authority/test_delta_export.cc` | 13, 20, 118 |
| `tests/detent_authority/test_distribution_service.cc` | 133 |
| `tests/detent_authority/test_export_encoder.cc` | 29 |
| `tests/detent_authority/test_subscription.cc` | 38, 179 |
| `tests/detent_delivery/probe_fixture.hh` | 18, 19, 33 |
| `tests/detent_delivery/scripted_upstream.hh` | 45, 46, 84, 374 |
| `tests/detent_delivery/test_bootstrap_retention.cc` | 12, 132 |
| `tests/detent_delivery/test_client.cc` | 20, 21 |
| `tests/detent_delivery/test_coordinator.cc` | 108, 109 |
| `tests/detent_delivery/test_relay_registry.cc` | 21 |
| `tests/detent_delivery/test_reply_pump.cc` | 59 |
| `tests/detent_delivery/test_response.cc` | 15, 16, 24 |
| `tests/detent_delivery/test_session_table.cc` | 26, 111, 167 |
| `tests/detent_distribution/storage_failure_fixture.hh` | 83, 85 |
| `tests/detent_distribution/test_delta.cc` | 12, 13, 21, 91, 189 |
| `tests/detent_distribution/test_encoded_update.cc` | 14, 15, 23 |
| `tests/detent_distribution/test_snapshot.cc` | 11, 12, 80, 144 |
| `tests/detent_local/applied_delta_fixture.hh` | 3 |
| `tests/detent_local/paged_progress_fixture.hh` | 3, 12 |
| `tests/detent_local/snapshot_fixture.hh` | 14, 15, 83 |
| `tests/detent_local/test_dense_fullbuild.cc` | 35, 67 |
| `tests/detent_local/test_identity_reset.cc` | 27, 81 |
| `tests/detent_system/first_read_export.cc` | 105, 135 |
| `tests/detent_system/local_oracle.cc` | 33 |
| `tests/detent_system/mixed_oracle.cc` | 73 |
| `tools/w5/w5_store.cc` | 22, 23, 32 |

## AuthorizedScope inventory

64 matching lines in 19 files.

| File | Lines |
|---|---|
| `src/detent_authority/delivery_export.cc` | 6, 22, 43 |
| `src/detent_authority/delivery_export.hh` | 11 |
| `src/detent_authority/delta_export.cc` | 16, 28, 218, 229 |
| `src/detent_authority/delta_export.hh` | 12 |
| `src/detent_authority/export_capture.cc` | 5, 17, 80 |
| `src/detent_authority/export_capture.hh` | 17, 20, 29, 31, 54 |
| `src/detent_authority/export_scope.cc` | 6, 7, 29, 40, 41 |
| `src/detent_authority/export_scope.hh` | 12, 18, 20, 26, 31 |
| `src/detent_authority/service.cc` | 530, 939 |
| `src/detent_authority/service.hh` | 93 |
| `src/detent_authority/state.hh` | 219, 293 |
| `src/detent_authority/subscription.cc` | 6, 21 |
| `src/detent_authority/subscription.hh` | 13, 21 |
| `tests/detent_authority/test_delivery_export.cc` | 83 |
| `tests/detent_authority/test_delta_export.cc` | 117 |
| `tests/detent_authority/test_export_capture.cc` | 38, 62, 93 |
| `tests/detent_authority/test_export_encoder.cc` | 122, 268 |
| `tests/detent_authority/test_export_scope.cc` | 27, 37, 43, 49, 51, 53, 56, 58, 59, 62, 65, 69, 71, 76, 80, 81, 82, 87, 94 |
| `tests/detent_authority/test_subscription.cc` | 36, 224 |

## Poll mode inventory

69 matching lines in 23 files.

| File | Lines |
|---|---|
| `benchmarks/detent_update_bench.cc` | 5 |
| `include/detent/client.hh` | 99, 181, 183 |
| `include/detent_delivery/coordinator.hh` | 17, 21, 26 |
| `include/detent_local/client_source.hh` | 10 |
| `src/detent/client.cc` | 77, 78, 128, 133, 234, 274, 293, 322 |
| `src/detent_delivery/configuration.cc` | 121, 136, 138, 139, 207, 208, 211, 212 |
| `src/detent_delivery/coordinator.cc` | 771, 775, 779, 929, 983, 986, 1044, 1215, 1219, 1235, 1239, 1241, 1279, 1280, 1284 |
| `src/detent_local/client_source.cc` | 94, 95, 106, 114 |
| `src/detent_relay/configuration.cc` | 118, 120 |
| `tests/detent/test_client_publication_failure.cc` | 49 |
| `tests/detent/test_file_source.cc` | 60, 81, 143 |
| `tests/detent_authority/test_distribution_ownership.cc` | 175 |
| `tests/detent_delivery/scripted_upstream.hh` | 414, 415 |
| `tests/detent_delivery/test_coordinator.cc` | 660, 661, 672 |
| `tests/detent_delivery/test_coordinator_teardown.cc` | 73, 74 |
| `tests/detent_delivery/test_managed_config.cc` | 188, 192 |
| `tests/detent_delivery/test_managed_process.sh` | 62 |
| `tests/detent_delivery/test_reconnect_cancel.cc` | 63, 64 |
| `tests/detent_delivery/test_relay_config.cc` | 67, 177 |
| `tests/detent_local/test_client_source.cc` | 147 |
| `tests/detent_system/managed.py` | 136, 237 |
| `tests/detent_system/run_trust_negatives.py` | 171 |
| `tests/detent_system/run_writers.py` | 278 |

## Version 1 and one-shot wire inventory

237 matching lines in 68 files.

| File | Lines |
|---|---|
| `benchmarks/container-alternative/lifecycle_bench.cc` | 142, 150 |
| `benchmarks/detent_authority_bench.cc` | 40 |
| `benchmarks/detent_read_bench.cc` | 29 |
| `benchmarks/detent_scale_bench.cc` | 38 |
| `benchmarks/detent_update_bench.cc` | 30, 31 |
| `benchmarks/distribution_codec_bench.cc` | 319 |
| `benchmarks/library-adoption-baseline-20261017.json` | 9 |
| `benchmarks/library-adoption-daemon-cpu-20261018.json` | 61, 927, 1792, 2657, 3522, 4387, 5252, 6118, 6983, 7848, 8713, 9578 |
| `benchmarks/logging_compare.py` | 230, 231 |
| `benchmarks/msgpack-extraction-preliminary-20261019.json` | 231 |
| `benchmarks/paged_collection_bench.cc` | 18 |
| `benchmarks/resource-extraction-qualification-20261019.json` | 166 |
| `benchmarks/system-capacity-20261023.json` | 2, 42, 569, 1095, 1622 |
| `benchmarks/system-capacity-5m-20261024.json` | 2 |
| `benchmarks/terminal-retirement-measurements.json` | 2 |
| `examples/demo.cc` | 14 |
| `include/detent/json_patch.hh` | 30 |
| `include/detent_delivery/namespace_session.hh` | 19, 31 |
| `include/detent_delivery/response.hh` | 14, 16 |
| `include/detent_distribution/session_frames.hh` | 23 |
| `include/detent_distribution_http/session_request.hh` | 24 |
| `include/detent_testing/generation_writer.hh` | 24 |
| `src/detent/json_patch.cc` | 192 |
| `src/detent/json_schema.cc` | 365 |
| `src/detent_authority/service.cc` | 1199, 2312 |
| `src/detent_delivery/client.cc` | 34, 71, 100, 105, 108, 123, 126, 127, 158, 278, 445, 543, 583, 658 |
| `src/detent_delivery/namespace_session.cc` | 52, 55, 58, 90, 96, 113, 115, 116, 180, 183, 193, 253, 362, 363, 372, 374, 380, 382 |
| `src/detent_delivery/response.cc` | 84, 86, 170, 173, 174, 193, 383 |
| `src/detent_distribution_http/session_request.cc` | 179 |
| `src/detent_relay/service.cc` | 208 |
| `tests/cmake_consumer/main.cc` | 4 |
| `tests/detent/one_knob_cells.hh` | 53, 66 |
| `tests/detent/test_client_publication_failure.cc` | 39, 40, 41, 43 |
| `tests/detent/test_duplicate_precedence.cc` | 36, 43, 82, 102, 131 |
| `tests/detent/test_file_source.cc` | 48, 92, 97, 104, 119, 133, 148 |
| `tests/detent/test_json_patch.cc` | 17, 18, 32, 54, 60, 66, 73, 106, 148, 150, 162, 175, 203, 214 |
| `tests/detent/test_json_strings.cc` | 10, 11, 12, 13, 14, 20, 28, 32 |
| `tests/detent/test_model_alloc_failure.cc` | 208 |
| `tests/detent/test_model_and_schema.cc` | 17, 43, 50, 85, 91, 93, 95, 97, 99, 101, 103, 113, 119, 125, 139, 150, 197, 210, 217 |
| `tests/detent/test_overrides.cc` | 47 |
| `tests/detent_authority/test_distribution_ownership.cc` | 270, 296, 304, 448, 455, 505, 525 |
| `tests/detent_authority/test_distribution_service.cc` | 187, 200, 209, 220, 223 |
| `tests/detent_authority/test_frames_service.cc` | 8 |
| `tests/detent_authority/test_service.cc` | 259, 636, 637, 640 |
| `tests/detent_authority/test_subscription_admission.cc` | 30, 32 |
| `tests/detent_delivery/relay_network_fixture.hh` | 82 |
| `tests/detent_delivery/relay_service_fixture.hh` | 110, 122, 137 |
| `tests/detent_delivery/scripted_upstream.hh` | 300 |
| `tests/detent_delivery/test_client.cc` | 104, 175 |
| `tests/detent_delivery/test_coordinator.cc` | 835, 1063 |
| `tests/detent_delivery/test_managed_process.sh` | 111, 155 |
| `tests/detent_delivery/test_namespace_session.cc` | 394, 397, 400, 405, 414 |
| `tests/detent_delivery/test_probe_client.cc` | 80 |
| `tests/detent_delivery/test_probe_response.cc` | 52 |
| `tests/detent_delivery/test_response.cc` | 255, 278 |
| `tests/detent_delivery/test_session_request.cc` | 28, 41, 49, 53, 54, 56, 57, 58, 59, 60, 61, 78, 96 |
| `tests/detent_distribution/test_session_frames.cc` | 130 |
| `tests/detent_local/test_client_source.cc` | 80 |
| `tests/detent_system/mixed.py` | 368 |
| `tests/detent_system/run_trust_negatives.py` | 71, 90 |
| `tests/detent_system/test_mixed.py` | 63 |
| `tests/detent_testing/restart_timing.cc` | 29, 64 |
| `tests/detent_testing/test_change_file.cc` | 54 |
| `tests/detent_testing/test_generation_writer.cc` | 18 |
| `tests/detent_testing/test_pack_scrub.cc` | 211 |
| `tests/detent_testing/test_trusted_restart.cc` | 66, 538 |
| `tools/w5/http_replay.py` | 42, 103, 135, 141, 268 |
| `tools/w5/w5_detent.cc` | 19, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89 |

## Legacy identity and table-shape helpers

`tests/detent_system/managed.py:34` requires exactly one table entry before reading its position; migrate this oracle when selecting D8. Scope hashes and adapters are consumers even without an explicit `Cursor` spelling. The following inventory also includes generic grant-scope hashes whose authorization role must survive the cursor removal.

| File | Lines |
|---|---|
| `include/detent/scope.hh` | 13, 18 |
| `include/detent_distribution/grants.hh` | 24 |
| `include/detent_local/derived_ids.hh` | 26, 28 |
| `src/detent_authority/export_scope.cc` | 36 |
| `src/detent_authority/service.cc` | 622, 625, 725, 1013 |
| `src/detent_authority/state.cc` | 911 |
| `src/detent_authority/state.hh` | 259 |
| `src/detent_delivery/client.cc` | 471 |
| `src/detent_delivery/configuration.cc` | 171 |
| `src/detent_delivery/namespace_session.cc` | 75 |
| `src/detent_distribution/grants.cc` | 7, 17, 19, 30 |
| `src/detent_distribution/session_frames.cc` | 318, 324, 573, 603 |
| `src/detent_local/derived_ids.cc` | 58, 76, 90 |
| `src/detent_local/disk_store.cc` | 273, 274, 372, 373 |
| `src/detent_local/generation.cc` | 144 |
| `src/detent_local/paged_warm_validator.cc` | 102 |
| `src/detent_relay/configuration.cc` | 155 |
| `src/detent_relay/host.cc` | 202 |
| `tests/detent_authority/test_distribution_ownership.cc` | 258, 501 |
| `tests/detent_authority/test_distribution_service.cc` | 132 |
| `tests/detent_authority/test_namespace_positions.cc` | 228, 262 |
| `tests/detent_authority/test_subscription.cc` | 20 |
| `tests/detent_delivery/test_coordinator.cc` | 1221, 1222 |
| `tests/detent_delivery/test_managed_config.cc` | 68 |
| `tests/detent_delivery/test_managed_process.sh` | 103, 107 |
| `tests/detent_delivery/test_relay_config.cc` | 70, 156 |
| `tests/detent_delivery/test_relay_registry.cc` | 25, 26, 28 |
| `tests/detent_local/trusted_records.hh` | 72, 73 |
| `tests/detent_system/first_read_export.cc` | 103 |
| `tests/detent_system/managed.py` | 18, 26, 34, 101, 282 |
| `tests/detent_system/run_trust_negatives.py` | 12, 188 |
