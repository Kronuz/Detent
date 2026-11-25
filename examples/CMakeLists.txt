add_executable(detent_demo demo.cc)
target_link_libraries(detent_demo PRIVATE detent::reader)
