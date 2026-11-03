# These suites use assert(); keep checks active in Release builds too.
add_compile_options(-UNDEBUG)
include_directories("${PROJECT_SOURCE_DIR}/tests")

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
