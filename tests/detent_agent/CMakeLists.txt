add_test(NAME test_agent_cli COMMAND ${CMAKE_COMMAND}
  -DAGENT=$<TARGET_FILE:detent-agent> -DREADER=$<TARGET_FILE:detent-reader> -P ${CMAKE_CURRENT_SOURCE_DIR}/test_agent_cli.cmake)
set_tests_properties(test_agent_cli PROPERTIES TIMEOUT 60)

add_executable(test_logging test_logging.cc)
target_link_libraries(test_logging PRIVATE detent_logging)
add_test(NAME test_logging COMMAND test_logging)

add_executable(test_reader_capacity test_reader_capacity.cc
  ../../src/detent_agent/config.cc ../../src/detent_agent/reader_capacity.cc)
target_link_libraries(test_reader_capacity PRIVATE detent_cli detent::local_view)
add_test(NAME test_reader_capacity COMMAND test_reader_capacity ${CMAKE_CURRENT_BINARY_DIR}/reader-capacity)
set_tests_properties(test_reader_capacity PROPERTIES TIMEOUT 30)

if(TARGET detent::agent_store)
  add_executable(test_agent_store test_agent_store.cc)
  target_link_libraries(test_agent_store PRIVATE detent::agent_store)
  add_test(NAME test_agent_store COMMAND test_agent_store ${CMAKE_CURRENT_BINARY_DIR}/agent-store)
  set_tests_properties(test_agent_store PROPERTIES TIMEOUT 60)

  add_executable(test_agent_store_crash test_agent_store_crash.cc)
  target_link_libraries(test_agent_store_crash PRIVATE detent::agent_store)
  add_test(NAME test_agent_store_crash COMMAND test_agent_store_crash ${CMAKE_CURRENT_BINARY_DIR}/agent-store-crash)
  set_tests_properties(test_agent_store_crash PROPERTIES TIMEOUT 900)
endif()
if(TARGET detent::local_publication)
  add_executable(test_agent_catch_up test_agent_catch_up.cc)
  target_include_directories(test_agent_catch_up PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(test_agent_catch_up PRIVATE detent::local_publication)
  add_test(NAME test_agent_catch_up COMMAND test_agent_catch_up)
  set_tests_properties(test_agent_catch_up PROPERTIES TIMEOUT 120)
endif()
