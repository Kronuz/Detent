# These suites use assert(); keep checks active in Release builds too.
add_compile_options(-UNDEBUG)
include_directories("${PROJECT_SOURCE_DIR}/tests")

# A test that includes support/allocation_probe.hh defines every global
# allocation function. ThreadSanitizer's static runtime is linked whole
# and defines the same set, so under it the test's definitions come first.
function(detent_use_allocation_probe target)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
     "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type}} ${CMAKE_EXE_LINKER_FLAGS}" MATCHES "fsanitize=thread")
    target_link_options(${target} PRIVATE LINKER:--allow-multiple-definition)
  endif()
endfunction()

add_subdirectory(detent)
add_subdirectory(detent_agent)
if(TARGET detent::owner_loop)
  add_subdirectory(detent_owner)
endif()

if(DETENT_BUILD_DISTRIBUTOR)
  add_subdirectory(detentd)
endif()

add_subdirectory(detent_authority)
add_subdirectory(detent_distribution)
if(DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  add_subdirectory(detent_delivery)
endif()
add_subdirectory(detent_local)
add_subdirectory(detent_http)

add_subdirectory(detent_system)
