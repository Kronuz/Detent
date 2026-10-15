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
