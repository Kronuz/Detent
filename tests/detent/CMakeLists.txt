set(DETENT_TESTS
  test_model_and_schema
  test_hash_golden
  test_eval_semantics
  test_overrides
  test_file_source
  test_testing_guard
  test_concurrency_stress
  test_json_patch
  test_json_strings
  test_model_alloc_failure
  test_client_publication_failure
  test_flat_map
  test_duplicate_precedence)

list(APPEND DETENT_TESTS test_read_view)

foreach(test_name ${DETENT_TESTS})
  add_executable(${test_name} ${test_name}.cc)
  target_link_libraries(${test_name} PRIVATE detent::detent)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  add_executable(test_config_acl_policy test_config_acl_policy.cc)
  target_include_directories(test_config_acl_policy PRIVATE "${PROJECT_SOURCE_DIR}/include")
  add_test(NAME test_config_acl_policy COMMAND test_config_acl_policy)
endif()
