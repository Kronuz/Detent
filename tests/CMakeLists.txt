add_subdirectory(detent)
add_subdirectory(detent_agent)

if(DETENT_BUILD_DISTRIBUTOR)
  add_subdirectory(detentd)
endif()
