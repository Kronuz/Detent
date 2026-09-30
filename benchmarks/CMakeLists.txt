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
