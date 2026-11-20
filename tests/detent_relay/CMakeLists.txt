if(TARGET detent::relay_mirror)
  add_executable(test_relay_mirror test_relay_mirror.cc)
  target_link_libraries(test_relay_mirror PRIVATE detent::relay_mirror hashes::hashes)
  add_test(NAME test_relay_mirror COMMAND test_relay_mirror ${CMAKE_CURRENT_BINARY_DIR}/relay-mirror)
  set_tests_properties(test_relay_mirror PROPERTIES TIMEOUT 120)

  # Dense distribution DD3: the relay answers pages from its own builds, built off the mirror thread;
  # its mirror takes records only.
  if(TARGET detent::relay_mirror_service)
    add_executable(test_relay_pages test_relay_pages.cc)
    target_include_directories(test_relay_pages PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_relay")
    target_link_libraries(test_relay_pages PRIVATE detent::relay_mirror_service detent::relay_page_window
      detent::local_fullbuild detent::delivery_delta_transfer detent::authority)
    add_test(NAME test_relay_pages COMMAND test_relay_pages ${CMAKE_CURRENT_BINARY_DIR}/relay-pages)
    set_tests_properties(test_relay_pages PROPERTIES TIMEOUT 180)
  endif()

  add_executable(test_relay_mirror_crash test_relay_mirror_crash.cc)
  target_link_libraries(test_relay_mirror_crash PRIVATE detent::relay_mirror hashes::hashes)
  add_test(NAME test_relay_mirror_crash COMMAND test_relay_mirror_crash ${CMAKE_CURRENT_BINARY_DIR}/relay-mirror-crash)
  set_tests_properties(test_relay_mirror_crash PROPERTIES TIMEOUT 900)
endif()

# D138: history files read in ranges, resumed after a lost answer, and a failing sync's backoff.
if(TARGET detent::relay_mirror_service)
  add_executable(test_http_upstream test_http_upstream.cc)
  target_link_libraries(test_http_upstream PRIVATE detent::relay_mirror_service hashes::hashes)
  add_test(NAME test_http_upstream COMMAND test_http_upstream ${PROJECT_BINARY_DIR}/tests/detent_delivery/tls-fixture)
  set_tests_properties(test_http_upstream PROPERTIES FIXTURES_REQUIRED delivery_tls TIMEOUT 60)
endif()

# The relay's credentials and configuration: a mirror, W13 namespace grants, admission lanes and TLS.
if(TARGET detent::relay_config AND TARGET detent::relay_http)
  add_executable(test_relay_registry test_relay_registry.cc)
  target_link_libraries(test_relay_registry PRIVATE detent::relay_http hashes::hashes)
  add_test(NAME test_relay_registry COMMAND test_relay_registry)
  set_tests_properties(test_relay_registry PROPERTIES TIMEOUT 30)

  add_executable(test_relay_config test_relay_config.cc)
  target_link_libraries(test_relay_config PRIVATE detent::relay_config)
  add_test(NAME test_relay_config COMMAND test_relay_config ${PROJECT_BINARY_DIR}/tests/detent_delivery/tls-fixture)
  set_tests_properties(test_relay_config PROPERTIES FIXTURES_REQUIRED delivery_tls TIMEOUT 30)
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
