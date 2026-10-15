find_package(Python3 3.12 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME test_system_runner_runtime
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
      -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py")
  set_tests_properties(test_system_runner_runtime PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} TIMEOUT 30)
endif()
