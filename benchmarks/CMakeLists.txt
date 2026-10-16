# Standalone, argv-configurable benchmark executables -- no test framework,
# no CTest registration (these print numbers for a human, they don't assert
# a pass/fail SLO). See each .cc file's header comment for what it measures
# and how to run it.

add_executable(detent_read_bench detent_read_bench.cc)
target_link_libraries(detent_read_bench PRIVATE detent::detent)

add_executable(detent_hash_bench detent_hash_bench.cc)
target_link_libraries(detent_hash_bench PRIVATE detent::detent)

add_executable(detent_update_bench detent_update_bench.cc)
target_link_libraries(detent_update_bench PRIVATE detent::detent)

add_executable(detent_agent_shm_bench detent_agent_shm_bench.cc)
target_link_libraries(detent_agent_shm_bench PRIVATE detent_agent_lib detent::detent)

if(DETENT_BUILD_DISTRIBUTOR)
  add_executable(detentd_fanout_bench detentd_fanout_bench.cc)
  target_link_libraries(detentd_fanout_bench PRIVATE detentd_lib detent_http::detent_http)
endif()

add_executable(detent_scale_bench detent_scale_bench.cc)
target_link_libraries(detent_scale_bench PRIVATE detent_agent_lib)

add_executable(detent_authority_bench detent_authority_bench.cc)
target_link_libraries(detent_authority_bench PRIVATE detent::authority)

add_executable(detent_command_work_bench command_work_bench.cc)
target_link_libraries(detent_command_work_bench PRIVATE detent::authority)

if(DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY)
  add_executable(detent_authority_durable_bench detent_authority_durable_bench.cc)
  target_link_libraries(detent_authority_durable_bench PRIVATE detent::authority_node)
endif()

add_executable(detent_credential_scan_bench credential_scan_bench.cc)
target_link_libraries(detent_credential_scan_bench PRIVATE sha256)

if(DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY)
  add_executable(detent_authority_read_response_bench read_response_bench.cc)
  target_link_libraries(detent_authority_read_response_bench PRIVATE detent_authority_service)
endif()

if(DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY)
  add_executable(detent_authority_terminal_retirement_bench terminal_retirement_bench.cc)
  target_link_libraries(detent_authority_terminal_retirement_bench PRIVATE detent::authority_node reactor::reactor)  # Asio
endif()

add_executable(detent_distribution_codec_bench distribution_codec_bench.cc)
add_executable(detent_delta_decode_bench delta_decode_bench.cc)
target_link_libraries(detent_delta_decode_bench PRIVATE detent::distribution)
target_link_libraries(detent_distribution_codec_bench PRIVATE detent::detent compressors::compressors)
option(DETENT_BENCHMARK_PROTOBUF "Enable installed Protobuf Lite for codec comparison only" OFF)
if(DETENT_BENCHMARK_PROTOBUF)
  find_package(Protobuf CONFIG REQUIRED)
  protobuf_generate(TARGET detent_distribution_codec_bench PROTOS distribution_codec_candidate.proto)
  target_include_directories(detent_distribution_codec_bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(detent_distribution_codec_bench PRIVATE protobuf::libprotobuf-lite)
else()
  target_compile_definitions(detent_distribution_codec_bench PRIVATE DETENT_CODEC_NO_PROTOBUF=1)
endif()
add_executable(detent_hash_allocation_bench hash_allocation_bench.cc)
target_link_libraries(detent_hash_allocation_bench PRIVATE detent::detent)
add_executable(detent_local_image_bench local_image_bench.cc)
target_link_libraries(detent_local_image_bench PRIVATE detent::local_builder)
add_executable(detent_paged_snapshot_bench paged_snapshot_bench.cc)
target_include_directories(detent_paged_snapshot_bench PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(detent_paged_snapshot_bench PRIVATE detent::local_builder)
if(TARGET detent::local_publication)
  add_executable(detent_paged_recovery_bench paged_recovery_bench.cc)
  target_include_directories(detent_paged_recovery_bench PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_paged_recovery_bench PRIVATE detent::local_publication)
  add_executable(detent_paged_collection_bench paged_collection_bench.cc)
  target_include_directories(detent_paged_collection_bench PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_paged_collection_bench PRIVATE detent::local_publication)
endif()

if(TARGET detent::delivery_config)
  add_executable(detent_fleet_load detent_fleet_load.cc fleet_load.cc)
  target_link_libraries(detent_fleet_load PRIVATE detent::delivery_config)
endif()
