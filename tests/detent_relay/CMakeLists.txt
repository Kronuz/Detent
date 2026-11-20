if(TARGET detent::relay_mirror)
  add_executable(test_relay_mirror test_relay_mirror.cc)
  target_link_libraries(test_relay_mirror PRIVATE detent::relay_mirror hashes::hashes)
  add_test(NAME test_relay_mirror COMMAND test_relay_mirror ${CMAKE_CURRENT_BINARY_DIR}/relay-mirror)
  set_tests_properties(test_relay_mirror PROPERTIES TIMEOUT 120)

  add_executable(test_relay_mirror_crash test_relay_mirror_crash.cc)
  target_link_libraries(test_relay_mirror_crash PRIVATE detent::relay_mirror hashes::hashes)
  add_test(NAME test_relay_mirror_crash COMMAND test_relay_mirror_crash ${CMAKE_CURRENT_BINARY_DIR}/relay-mirror-crash)
  set_tests_properties(test_relay_mirror_crash PROPERTIES TIMEOUT 900)
endif()

# A1 and A5 over real voters: the relay chain reads S5.2's history through its layout and W6, and
# checks each voter's own layouts export against it.
if(TARGET detent::relay_mirror AND TARGET detent_authority_service)
  add_executable(test_relay_chain test_relay_chain.cc)
  target_include_directories(test_relay_chain PRIVATE ${PROJECT_SOURCE_DIR}/tests/detent_authority)
  target_link_libraries(test_relay_chain PRIVATE detent::relay_mirror detent_authority_service)
  add_test(NAME test_relay_chain COMMAND test_relay_chain ${CMAKE_CURRENT_BINARY_DIR}/relay-chain)
  set_tests_properties(test_relay_chain PROPERTIES TIMEOUT 120)
endif()

# A1-A3 over processes: three detent-authority voters, relay A reading them and relay B reading A, all over
# HTTPS; the probe reads each through the relay's own HttpUpstream.
find_package(Python3 3.12 COMPONENTS Interpreter QUIET)
if(TARGET detent::relay_mirror_service AND TARGET detent-authority AND TARGET detent-relay AND Python3_FOUND AND DETENT_BUILD_AUTHORITY)
  add_executable(test_relay_probe relay_probe.cc)
  target_link_libraries(test_relay_probe PRIVATE detent::relay_mirror_service hashes::hashes)
  add_test(NAME test_relay_service
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_relay_service.py
      $<TARGET_FILE:detent-authority> $<TARGET_FILE:detent-relay> $<TARGET_FILE:test_relay_probe>
      ${PROJECT_BINARY_DIR}/tests/detent_authority/tls-fixture)
  set_tests_properties(test_relay_service PROPERTIES FIXTURES_REQUIRED authority_tls TIMEOUT 240)
endif()

# Dense distribution DD4: the page window and Merkle diff against the occurrence-write oracle. With
# CORPUS DIR SPEC..., A1 against the model's transfers (tests/detent_relay/delta_scenarios.py).
add_executable(test_page_window test_page_window.cc)
target_include_directories(test_page_window PRIVATE "${PROJECT_SOURCE_DIR}/tests/detent_local")
target_link_libraries(test_page_window PRIVATE detent::relay_page_window detent::local_fullbuild hashes::hashes)
add_test(NAME test_page_window COMMAND test_page_window)
set_tests_properties(test_page_window PROPERTIES TIMEOUT 300)
