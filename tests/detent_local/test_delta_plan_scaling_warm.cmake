# Adds delta_plan_warm (test_delta_plan_scaling_warm.cc) to a Detent build without editing its CMakeLists:
#   cmake -S . -B BUILD -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE=$PWD/tests/detent_local/test_delta_plan_scaling_warm.cmake
set(DELTA_PLAN_WARM_SOURCE "${CMAKE_CURRENT_LIST_DIR}/test_delta_plan_scaling_warm.cc")
function(delta_plan_warm_target)
  add_executable(delta_plan_warm "${DELTA_PLAN_WARM_SOURCE}")
  target_include_directories(delta_plan_warm PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src/detent_local")
  target_link_libraries(delta_plan_warm PRIVATE detent::detent detent::local_publication)
  target_compile_features(delta_plan_warm PRIVATE cxx_std_20)
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL delta_plan_warm_target)
