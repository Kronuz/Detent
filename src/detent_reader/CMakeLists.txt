# libdetent_reader: the reader core behind one C ABI (docs/mechanisms/reader-core.md). It starts no
# thread and does not use a RetirementDomain.
add_library(detent_reader core.cc)
target_include_directories(detent_reader PUBLIC "${PROJECT_SOURCE_DIR}/include"
                           PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(detent_reader PRIVATE detent::local_view)
target_compile_features(detent_reader PUBLIC cxx_std_20)
add_library(detent::reader ALIAS detent_reader)
# The same core as a shared library for Python and Rust: it exports only the `detent_*` C symbols and
# carries its C++ dependencies inside (docs/design/client-local-source.md D6).
if(DETENT_BUILD_PYTHON OR DETENT_BUILD_RUST OR DETENT_BUILD_TESTS)
  add_library(detent_reader_shared SHARED core.cc)
  set_target_properties(detent_reader_shared PROPERTIES OUTPUT_NAME detent_reader)
  target_include_directories(detent_reader_shared PUBLIC "${PROJECT_SOURCE_DIR}/include"
                             PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_reader_shared PRIVATE detent::local_view)
  target_compile_features(detent_reader_shared PUBLIC cxx_std_20)
  detent_export_only(detent_reader_shared "${CMAKE_CURRENT_SOURCE_DIR}/exports")
  add_library(detent::reader_shared ALIAS detent_reader_shared)
endif()
# Test-only twin with the park hooks and stats (DETENT_READER_TEST_HOOKS); never installed or shipped.
if(DETENT_BUILD_TESTS)
  add_library(detent_reader_hooked STATIC core.cc)
  target_compile_definitions(detent_reader_hooked PUBLIC DETENT_READER_TEST_HOOKS=1)
  target_include_directories(detent_reader_hooked PUBLIC "${PROJECT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}"
                             PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_reader_hooked PRIVATE detent::local_view)
  target_compile_features(detent_reader_hooked PUBLIC cxx_std_20)
endif()
