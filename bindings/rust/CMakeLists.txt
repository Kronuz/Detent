# Rust binding over libdetent_reader's C ABI (docs/design/client-local-source.md D1-D3, D6).
# Not a CMake-native target: cargo owns the crate. CMake makes sure the shared libraries it links exist,
# and cargo compiles no C++.
find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
  message(FATAL_ERROR "DETENT_BUILD_RUST=ON but no `cargo` found on PATH")
endif()

add_custom_target(detent_rust ALL
  COMMAND ${CMAKE_COMMAND} -E env "DETENT_BUILD_DIR=${CMAKE_BINARY_DIR}" ${CARGO_EXECUTABLE} build
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS detent_reader_shared
  COMMENT "cargo build (bindings/rust)"
  VERBATIM)

# The tests publish real generations through the shared test writer, in a store under cargo's target
# directory (directory trust refuses a group- or world-writable ancestor, so /tmp is out).
if(DETENT_BUILD_TESTS)
  add_dependencies(detent_rust detent_testing_shared)
  add_test(NAME detent_rust_test
    COMMAND ${CMAKE_COMMAND} -E env "DETENT_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "DETENT_SCENARIOS=${PROJECT_SOURCE_DIR}/bindings/tests/scenarios.json"
            ${CARGO_EXECUTABLE} test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(detent_rust_test PROPERTIES TIMEOUT 900)
endif()
