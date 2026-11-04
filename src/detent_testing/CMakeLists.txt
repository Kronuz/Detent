# Test-only: publishes real generations for tests (docs/mechanisms/generation-writer.md). Never link it
# into a reader or an installed target.
if(TARGET detent::local_publication)
  add_library(detent_testing generation_writer.cc)
  target_include_directories(detent_testing PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_include_directories(detent_testing PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(detent_testing PUBLIC detent::detent PRIVATE detent::local_publication)
  target_compile_features(detent_testing PUBLIC cxx_std_20)
  add_library(detent::testing_store ALIAS detent_testing)
endif()
