# Pass as -DCMAKE_PROJECT_INCLUDE=<this file> to build the M1 identity drivers into any Detent tree (old or new)
# without editing that tree: drivers.cmake runs at the end of the top-level directory, once its targets exist.
if(NOT W5_DRIVERS)
  set(W5_DRIVERS "${CMAKE_CURRENT_LIST_DIR}/drivers.cmake" CACHE INTERNAL "")
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${W5_DRIVERS}")
endif()
