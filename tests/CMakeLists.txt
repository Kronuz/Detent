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
add_subdirectory(detent_testing)

add_subdirectory(detent_system)
