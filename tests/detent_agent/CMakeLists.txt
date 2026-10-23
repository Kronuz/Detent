add_executable(test_agent_basic test_agent_basic.cc)
target_link_libraries(test_agent_basic PRIVATE detent_agent_lib)
add_test(NAME test_agent_basic COMMAND test_agent_basic)
set_tests_properties(test_agent_basic PROPERTIES TIMEOUT 30)

add_executable(test_snapshot_canonical test_snapshot_canonical.cc)
target_link_libraries(test_snapshot_canonical PRIVATE detent_agent_lib)
add_test(NAME test_snapshot_canonical COMMAND test_snapshot_canonical)
set_tests_properties(test_snapshot_canonical PROPERTIES TIMEOUT 30)

add_executable(test_shm_concurrency_stress test_shm_concurrency_stress.cc)
target_link_libraries(test_shm_concurrency_stress PRIVATE detent_agent_lib)
add_test(NAME test_shm_concurrency_stress COMMAND test_shm_concurrency_stress)
set_tests_properties(test_shm_concurrency_stress PROPERTIES TIMEOUT 60)

add_test(NAME test_agent_cli COMMAND ${CMAKE_COMMAND}
  -DAGENT=$<TARGET_FILE:detent-agent> -DREADER=$<TARGET_FILE:detent-reader> -P ${CMAKE_CURRENT_SOURCE_DIR}/test_agent_cli.cmake)
set_tests_properties(test_agent_cli PROPERTIES TIMEOUT 60)

if(DETENT_BUILD_DISTRIBUTOR)
  find_program(DETENT_TEST_CURL curl)
  if(DETENT_TEST_CURL)
    add_test(NAME test_agent_http COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/test_agent_http.sh
      $<TARGET_FILE:detent-agent> $<TARGET_FILE:detentd> ${CMAKE_CURRENT_BINARY_DIR}/agent-http ${DETENT_TEST_CURL})
    set_tests_properties(test_agent_http PROPERTIES TIMEOUT 30)
  endif()
endif()

add_test(NAME test_system_agent COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/test_system_agent.sh
  $<TARGET_FILE:detent-agent> ${CMAKE_CURRENT_BINARY_DIR}/system-agent)
set_tests_properties(test_system_agent PROPERTIES TIMEOUT 15)

add_executable(test_logging test_logging.cc)
target_link_libraries(test_logging PRIVATE detent_logging)
add_test(NAME test_logging COMMAND test_logging)

add_executable(test_reader_capacity test_reader_capacity.cc
  ../../src/detent_agent/config.cc ../../src/detent_agent/reader_capacity.cc)
target_link_libraries(test_reader_capacity PRIVATE detent_cli detent::local_view)
add_test(NAME test_reader_capacity COMMAND test_reader_capacity ${CMAKE_CURRENT_BINARY_DIR}/reader-capacity)
set_tests_properties(test_reader_capacity PROPERTIES TIMEOUT 30)
