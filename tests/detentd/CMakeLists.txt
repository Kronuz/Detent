set(DETENTD_TESTS
  test_diff_cache
  test_distributor_basic
  test_distributor_client_integration
  test_http_connection_aging
  test_multi_node_poll_source
  test_raft_cluster)

foreach(test_name ${DETENTD_TESTS})
  add_executable(${test_name} ${test_name}.cc)
  target_link_libraries(${test_name} PRIVATE detentd_lib)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()

# Not a correctness test in the usual sense -- see the file header. Kept out
# of the default ctest run's critical path via a generous timeout, but still
# gated on its own hard assertions (full convergence, conditional-GET hits).
add_executable(test_load_fanout test_load_fanout.cc)
target_link_libraries(test_load_fanout PRIVATE detentd_lib)
add_test(NAME test_load_fanout COMMAND test_load_fanout)
set_tests_properties(test_load_fanout PROPERTIES TIMEOUT 60)
