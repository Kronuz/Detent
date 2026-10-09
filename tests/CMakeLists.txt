# These suites use assert(); keep checks active in Release builds too.
add_compile_options(-UNDEBUG)

add_subdirectory(detent)
add_subdirectory(detent_agent)

if(DETENT_BUILD_DISTRIBUTOR)
  add_subdirectory(detentd)
endif()

add_subdirectory(detent_authority)
add_subdirectory(detent_distribution)
if(DETENT_BUILD_MANAGED_AGENT)
  add_subdirectory(detent_delivery)
endif()
add_subdirectory(detent_local)
add_subdirectory(detent_http)
