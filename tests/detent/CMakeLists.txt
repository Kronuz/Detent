set(DETENT_TESTS
  test_model_and_schema
  test_hash_golden
  test_eval_semantics
  test_overrides
  test_file_source
  test_testing_guard
  test_concurrency_stress
  test_json_patch
  test_json_strings)

list(APPEND DETENT_TESTS test_read_view)

foreach(test_name ${DETENT_TESTS})
  add_executable(${test_name} ${test_name}.cc)
  target_link_libraries(${test_name} PRIVATE detent::detent)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
