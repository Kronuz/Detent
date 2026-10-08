add_library(detent_local_view layout.cc view.cc generation.cc ownership.cc)
target_include_directories(detent_local_view PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_features(detent_local_view PUBLIC cxx_std_20)
add_library(detent::local_view ALIAS detent_local_view)

target_link_libraries(detent_local_view PUBLIC detent::detent detent::resources)
add_library(detent_local_builder builder.cc)
target_link_libraries(detent_local_builder PUBLIC detent::local_view detent::distribution)
target_compile_features(detent_local_builder PUBLIC cxx_std_20)
add_library(detent::local_builder ALIAS detent_local_builder)
# Publication is enabled only with the reviewed standalone completion module.
# Reader and codec consumers do not inherit this dependency.
if(TARGET kronuz::completion)
  add_library(detent_local_publication publication.cc storage_session.cc inventory.cc collector.cc record_reader.cc disk_store.cc)
  target_link_libraries(detent_local_publication PUBLIC detent::local_builder kronuz::completion)
  target_compile_features(detent_local_publication PUBLIC cxx_std_20)
  add_library(detent::local_publication ALIAS detent_local_publication)
endif()
