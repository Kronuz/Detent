# M1 identity drivers: the same sources are built in the old tree and in the new one, against each tree's own
# pinned libraries. Included by include.cmake once the tree has defined its targets.
set(W5_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(w5_driver name)
  add_executable(${name} "${W5_DIR}/${name}.cc")
  target_include_directories(${name} PRIVATE "${W5_DIR}")
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_link_libraries(${name} PRIVATE ${ARGN})
endfunction()

w5_driver(w5_compressors compressors::compressors)
w5_driver(w5_http_parser http_parser::http_parser)
w5_driver(w5_stox strict_stox::strict_stox)
w5_driver(w5_enum http_parser::http_parser hashes::hashes)
w5_driver(w5_random random::random)
w5_driver(w5_detent detent::detent)
if(TARGET logger::logger)
  w5_driver(logger_transcript logger::logger)
endif()
if(TARGET detent::local_builder AND TARGET detent::distribution)
  w5_driver(w5_store detent::local_builder detent::distribution)
  target_include_directories(w5_store PRIVATE "${CMAKE_SOURCE_DIR}/benchmarks" "${CMAKE_SOURCE_DIR}/src/detent_local")
endif()
