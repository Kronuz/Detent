if(TARGET detent::testing_store)
  add_executable(test_reader_core test_reader_core.cc)
  target_include_directories(test_reader_core PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(test_reader_core PRIVATE detent_reader_hooked detent::testing_store detent::local_publication)
  add_test(NAME test_reader_core COMMAND test_reader_core)

  # T3 and T6 (reader.md): switching under 16 readers for 60 s, the injected parks and the fork cases.
  add_executable(test_reader_switch test_reader_switch.cc)
  target_link_libraries(test_reader_switch PRIVATE detent_reader_hooked detent::testing_store)
  add_test(NAME test_reader_switch COMMAND test_reader_switch)
  set_tests_properties(test_reader_switch PROPERTIES TIMEOUT 900)

  # bindings/tests/scenarios.json, the table Python and Rust also run (client-local-source.md D4).
  add_executable(test_reader_scenarios test_reader_scenarios.cc)
  target_link_libraries(test_reader_scenarios PRIVATE detent::reader detent::testing_store)
  add_test(NAME test_reader_scenarios COMMAND test_reader_scenarios ${PROJECT_SOURCE_DIR}/bindings/tests/scenarios.json)
  set_tests_properties(test_reader_scenarios PROPERTIES TIMEOUT 60)

  # T1's syscall and allocation checks, on the release library rather than the hooked one.
  add_executable(test_reader_syscalls test_reader_syscalls.cc)
  target_link_libraries(test_reader_syscalls PRIVATE detent::reader detent::testing_store)
  add_test(NAME test_reader_syscalls COMMAND test_reader_syscalls)
endif()

if(TARGET detent::reader_shared)
  # The shared libraries export only their C ABI (docs/design/client-local-source.md D6).
  add_test(NAME test_reader_exports COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:detent_reader_shared> -DPREFIX=detent -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
endif()
if(TARGET detent::testing_store_shared)
  add_test(NAME test_testing_exports COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:detent_testing_shared> -DPREFIX=detent_testing -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
endif()
