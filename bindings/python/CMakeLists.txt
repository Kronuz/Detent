# Python binding: a C extension over libdetent_reader's C ABI (docs/design/client-local-source.md D1-D3).
# Opt in with -DDETENT_BUILD_PYTHON=ON; it needs Python3 development headers (Python.h).
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

Python3_add_library(detent_python MODULE WITH_SOABI detent_python.cc)
set_target_properties(detent_python PROPERTIES OUTPUT_NAME "detent")
target_include_directories(detent_python PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(detent_python PRIVATE detent_reader_shared)

# The tests publish real generations through the shared test writer, in a store under the build tree:
# directory trust refuses a group- or world-writable ancestor, so /tmp is out.
if(DETENT_BUILD_TESTS)
  add_test(NAME detent_python_test
    COMMAND ${CMAKE_COMMAND} -E env
            "PYTHONPATH=$<TARGET_FILE_DIR:detent_python>"
            "DETENT_TESTING_LIB=$<TARGET_FILE:detent_testing_shared>"
            "DETENT_TEST_ROOT=${CMAKE_BINARY_DIR}/.scratch"
            "DETENT_SCENARIOS=${PROJECT_SOURCE_DIR}/bindings/tests/scenarios.json"
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_detent.py)
  set_tests_properties(detent_python_test PROPERTIES TIMEOUT 120)
endif()
