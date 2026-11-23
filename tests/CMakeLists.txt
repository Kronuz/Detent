# These suites use assert(); keep checks active in Release builds too.
add_compile_options(-UNDEBUG)
include_directories("${PROJECT_SOURCE_DIR}/tests")

# Under ThreadSanitizer on Linux the runtime owns operator new, so a test that injects
# allocation failures (support/allocation_probe.hh) is not built there; the
# plain and ASan/UBSan jobs run it.
string(TOUPPER "${CMAKE_BUILD_TYPE}" detent_build_type)
if(NOT APPLE AND
   "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${detent_build_type}}" MATCHES "fsanitize=[a-z,]*thread")
  set(DETENT_TSAN ON)
else()
  set(DETENT_TSAN OFF)
endif()

add_subdirectory(detent)
add_subdirectory(detent_agent)
if(TARGET detent::owner_loop)
  add_subdirectory(detent_owner)
endif()

add_subdirectory(detent_authority)
add_subdirectory(detent_distribution)
if(DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  add_subdirectory(detent_delivery)
endif()
add_subdirectory(detent_local)
add_subdirectory(detent_relay)
add_subdirectory(detent_testing)
add_subdirectory(detent_reader)

add_subdirectory(detent_system)

# The opt-in development component installs and serves an application test (D149, legacy-h A19).
if(TARGET detent_reader_shared AND TARGET detent_testing_shared)
  add_test(NAME test_installed_testing_component
           COMMAND "${CMAKE_COMMAND}" "-DBUILD=${PROJECT_BINARY_DIR}"
                   "-DSOURCE=${PROJECT_SOURCE_DIR}/tests/cmake_consumer/installed"
                   "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/installed-component" "-DGENERATOR=${CMAKE_GENERATOR}"
                   "-DCXX=${CMAKE_CXX_COMPILER}" "-DSYSROOT=${CMAKE_OSX_SYSROOT}" -P "${PROJECT_SOURCE_DIR}/tests/cmake_consumer/installed/check.cmake")
  set_tests_properties(test_installed_testing_component PROPERTIES TIMEOUT 300)
endif()
