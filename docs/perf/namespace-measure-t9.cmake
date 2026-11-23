# Adds the T9 harness (namespace-measure-t9.cc) to a Detent build without editing its CMakeLists:
#   cmake -S . -B BUILD -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE=$PWD/docs/perf/namespace-measure-t9.cmake
set(NAMESPACE_MEASURE_T9_SOURCE "${CMAKE_CURRENT_LIST_DIR}/namespace-measure-t9.cc")
function(namespace_measure_t9_target)
  add_executable(namespace_measure_t9 "${NAMESPACE_MEASURE_T9_SOURCE}")
  target_include_directories(namespace_measure_t9 PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(namespace_measure_t9 PRIVATE detent::detent detent::local_publication)
  target_compile_features(namespace_measure_t9 PRIVATE cxx_std_20)
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL namespace_measure_t9_target)
