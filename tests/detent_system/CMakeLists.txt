find_package(Python3 3.12 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME test_system_runner_runtime
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
      -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py")
  set_tests_properties(test_system_runner_runtime PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} TIMEOUT 30)
endif()

if(Python3_FOUND AND TARGET detentd AND DETENT_BUILD_AUTHORITY)
  add_test(NAME test_system_authority_writers
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_writers.sh
      ${Python3_EXECUTABLE} $<TARGET_FILE:detentd>
      ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
  set_tests_properties(test_system_authority_writers PROPERTIES
    FIXTURES_REQUIRED authority_tls TIMEOUT 120)
endif()

if(Python3_FOUND AND TARGET detentd AND DETENT_BUILD_AUTHORITY)
  add_test(NAME test_system_authority_writer_failover
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_writers.sh
      ${Python3_EXECUTABLE} $<TARGET_FILE:detentd>
      ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR} failover)
  set_tests_properties(test_system_authority_writer_failover PROPERTIES
    FIXTURES_REQUIRED authority_tls TIMEOUT 120)
endif()

if(Python3_FOUND AND TARGET detent::local_client)
  add_executable(detent_system_local_oracle local_oracle.cc)
  target_link_libraries(detent_system_local_oracle PRIVATE detent::local_client)
  add_executable(detent_system_mixed_oracle mixed_oracle.cc)
  target_link_libraries(detent_system_mixed_oracle PRIVATE detent::local_client)
endif()
if(Python3_FOUND AND TARGET detentd AND TARGET detent-agent AND TARGET detent_system_local_oracle AND DETENT_BUILD_AUTHORITY AND DETENT_BUILD_MANAGED_AGENT)
  add_test(NAME test_system_managed_authority_failover
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_managed_failover.sh
      ${Python3_EXECUTABLE} $<TARGET_FILE:detentd> $<TARGET_FILE:detent-agent>
      $<TARGET_FILE:detent_system_local_oracle> ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
  set_tests_properties(test_system_managed_authority_failover PROPERTIES
    FIXTURES_REQUIRED authority_tls TIMEOUT 180)
endif()

if(Python3_FOUND AND TARGET detentd AND TARGET detent-agent AND TARGET detent_system_local_oracle AND DETENT_BUILD_AUTHORITY AND DETENT_BUILD_MANAGED_AGENT)
  foreach(mode IN ITEMS partition all-down no-quorum poll trust)
    add_test(NAME test_system_managed_${mode}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_managed_case.sh ${mode}
        ${Python3_EXECUTABLE} $<TARGET_FILE:detentd> $<TARGET_FILE:detent-agent>
        $<TARGET_FILE:detent_system_local_oracle> ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    set_tests_properties(test_system_managed_${mode} PROPERTIES FIXTURES_REQUIRED authority_tls TIMEOUT 180)
  endforeach()
  if(TARGET detent-relay)
    add_test(NAME test_system_managed_relay
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_managed_case.sh relay
        ${Python3_EXECUTABLE} $<TARGET_FILE:detentd> $<TARGET_FILE:detent-agent>
        $<TARGET_FILE:detent_system_local_oracle> ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR} $<TARGET_FILE:detent-relay>)
    set_tests_properties(test_system_managed_relay PROPERTIES FIXTURES_REQUIRED authority_tls TIMEOUT 180)
  endif()
endif()

if(Python3_FOUND AND TARGET detent_fleet_load AND TARGET detentd AND TARGET detent-agent AND TARGET detent-relay AND TARGET detent_system_mixed_oracle AND DETENT_BUILD_AUTHORITY)
  foreach(mode IN ITEMS raw zstd warm mixed partial-launch cancel delayed parked-cancel)
    if(mode STREQUAL "mixed")
      set(fleet_oracle detent_system_mixed_oracle)
    else()
      set(fleet_oracle detent_system_local_oracle)
    endif()
    add_test(NAME test_system_fleet_${mode}
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_fleet.sh ${mode}
        ${Python3_EXECUTABLE} $<TARGET_FILE:detentd> $<TARGET_FILE:detent-agent>
        $<TARGET_FILE:detent-relay> $<TARGET_FILE:${fleet_oracle}>
        $<TARGET_FILE:detent_fleet_load> ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    set_tests_properties(test_system_fleet_${mode} PROPERTIES
      FIXTURES_REQUIRED authority_tls TIMEOUT 240)
  endforeach()
endif()
