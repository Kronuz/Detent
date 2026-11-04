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

  # T1's syscall and allocation checks, on the release library rather than the hooked one.
  add_executable(test_reader_syscalls test_reader_syscalls.cc)
  target_link_libraries(test_reader_syscalls PRIVATE detent::reader detent::testing_store)
  add_test(NAME test_reader_syscalls COMMAND test_reader_syscalls)
endif()
