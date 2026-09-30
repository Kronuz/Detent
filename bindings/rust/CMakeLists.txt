# Section 5's Rust binding, via the `cxx` crate (see Cargo.toml/build.rs).
# Not a CMake-native target: cargo owns this crate's own build graph and
# compiles its small C++ shim itself (via cxx-build); CMake's role here is
# just to make sure libdetent.a (and its own sha256/random static libs)
# already exist before cargo needs them, matching this crate's own build.rs
# expectation (DETENT_BUILD_DIR, defaulting to <repo-root>/build).
find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
  message(FATAL_ERROR "DETENT_BUILD_RUST=ON but no `cargo` found on PATH")
endif()

add_custom_target(detent_rust ALL
  COMMAND ${CMAKE_COMMAND} -E env "DETENT_BUILD_DIR=${CMAKE_BINARY_DIR}" ${CARGO_EXECUTABLE} build
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS detent
  COMMENT "cargo build (bindings/rust)"
  VERBATIM)

add_custom_target(detent_rust_test
  COMMAND ${CMAKE_COMMAND} -E env "DETENT_BUILD_DIR=${CMAKE_BINARY_DIR}" ${CARGO_EXECUTABLE} test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS detent_rust
  COMMENT "cargo test (bindings/rust)"
  VERBATIM)
