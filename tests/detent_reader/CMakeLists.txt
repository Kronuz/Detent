if(TARGET detent::testing_store)
  add_executable(test_reader_core test_reader_core.cc)
  target_include_directories(test_reader_core PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(test_reader_core PRIVATE detent_reader_hooked detent::testing_store detent::local_publication)
  add_test(NAME test_reader_core COMMAND test_reader_core)

  add_executable(test_reader_files test_reader_files.cc)
  target_link_libraries(test_reader_files PRIVATE detent::reader detent::testing_store)
  add_test(NAME test_reader_files COMMAND test_reader_files)
  set_tests_properties(test_reader_files PROPERTIES TIMEOUT 90)

  # T3 and T6 (reader.md): switching under 16 readers for 60 s, the injected parks and the fork cases.
  add_executable(test_reader_switch test_reader_switch.cc)
  target_include_directories(test_reader_switch PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
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

  # The mapping cap at store sizes too large for the suite (reader-core.md, Bounds); run as a job.
  add_executable(reader_capacity_probe reader_capacity_probe.cc)
  target_include_directories(reader_capacity_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(reader_capacity_probe PRIVATE detent_reader_hooked detent::testing_store
                        detent::local_publication)
endif()

if(TARGET detent::reader_shared)
  # The shared libraries export only their C ABI (docs/design/client-local-source.md D6).
  add_test(NAME test_reader_exports COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:detent_reader_shared> -DLIST=${PROJECT_SOURCE_DIR}/src/detent_reader/exports.list -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
  # The check compares sets: one exported function the list leaves out fails it.
  file(STRINGS "${PROJECT_SOURCE_DIR}/src/detent_reader/exports.list" reader_exports)
  list(REMOVE_ITEM reader_exports detent_close)
  string(JOIN "\n" reader_exports_short ${reader_exports})
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports-without-close.list" "${reader_exports_short}\n")
  add_test(NAME test_reader_exports_unlisted COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:detent_reader_shared> -DLIST=${CMAKE_CURRENT_BINARY_DIR}/exports-without-close.list -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
  set_tests_properties(test_reader_exports_unlisted PROPERTIES WILL_FAIL TRUE)
endif()
if(TARGET detent::testing_store_shared)
  add_test(NAME test_testing_exports COMMAND ${CMAKE_COMMAND}
    -DLIBRARY=$<TARGET_FILE:detent_testing_shared> -DLIST=${PROJECT_SOURCE_DIR}/src/detent_testing/exports.list -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
endif()
