# These suites use assert(); keep checks active in Release builds too.
add_compile_options(-UNDEBUG)

add_subdirectory(detent)
add_subdirectory(detent_agent)

if(DETENT_BUILD_DISTRIBUTOR)
  add_subdirectory(detentd)
endif()
