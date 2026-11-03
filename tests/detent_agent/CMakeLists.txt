if(TARGET detent-agent)
  add_test(NAME test_agent_cli COMMAND ${CMAKE_COMMAND}
    -DAGENT=$<TARGET_FILE:detent-agent> -DREADER=$<TARGET_FILE:detent-reader> -P ${CMAKE_CURRENT_SOURCE_DIR}/test_agent_cli.cmake)
  set_tests_properties(test_agent_cli PROPERTIES TIMEOUT 60)
endif()

add_executable(test_logging test_logging.cc)
target_link_libraries(test_logging PRIVATE detent_logging)
add_test(NAME test_logging COMMAND test_logging)

add_executable(test_reader_capacity test_reader_capacity.cc
  ../../src/detent_agent/config.cc ../../src/detent_agent/reader_capacity.cc)
target_link_libraries(test_reader_capacity PRIVATE detent_cli detent::local_view)
add_test(NAME test_reader_capacity COMMAND test_reader_capacity ${CMAKE_CURRENT_BINARY_DIR}/reader-capacity)
set_tests_properties(test_reader_capacity PROPERTIES TIMEOUT 30)
