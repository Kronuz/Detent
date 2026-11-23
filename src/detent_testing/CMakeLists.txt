# Publishes real generations for tests (docs/mechanisms/generation-writer.md). Never link it into a
# reader or a shipped binary.
if(TARGET detent::local_publication)
  add_library(detent_testing generation_writer.cc)
  target_include_directories(detent_testing PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_include_directories(detent_testing PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_testing PUBLIC detent::detent PRIVATE detent::local_publication)
  target_compile_features(detent_testing PUBLIC cxx_std_20)
  add_library(detent::testing_store ALIAS detent_testing)
  # Shared twin for application, Python and Rust tests; exports only `detent_testing_*`.
  add_library(detent_testing_shared SHARED generation_writer.cc)
  set_target_properties(detent_testing_shared PROPERTIES OUTPUT_NAME detent_testing EXPORT_NAME testing)
  target_include_directories(detent_testing_shared PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
                             "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                             PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_testing_shared PRIVATE detent::detent detent::local_publication)
  target_compile_features(detent_testing_shared PUBLIC cxx_std_20)
  detent_export_only(detent_testing_shared "${CMAKE_CURRENT_SOURCE_DIR}/exports.list")
  add_library(detent::testing ALIAS detent_testing_shared)
  # The opt-in development component (D149): both shared libraries, their C and C++ headers and the
  # `Detent` package; `cmake --install BUILD --component development`. A plain install skips it.
  if(TARGET detent_reader_shared)
    include(CMakePackageConfigHelpers)
    install(TARGETS detent_reader_shared detent_testing_shared EXPORT DetentDevelopment
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development EXCLUDE_FROM_ALL
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT development EXCLUDE_FROM_ALL)
    install(FILES "${PROJECT_SOURCE_DIR}/include/detent/reader.h" "${PROJECT_SOURCE_DIR}/include/detent/reader.hh"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/detent COMPONENT development EXCLUDE_FROM_ALL)
    install(FILES "${PROJECT_SOURCE_DIR}/include/detent_testing/generation_writer.h"
            "${PROJECT_SOURCE_DIR}/include/detent_testing/store.hh"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/detent_testing COMPONENT development EXCLUDE_FROM_ALL)
    install(EXPORT DetentDevelopment NAMESPACE detent:: FILE DetentConfig.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Detent COMPONENT development EXCLUDE_FROM_ALL)
  endif()
endif()
