add_executable(detent_demo demo.cc)
target_link_libraries(detent_demo PRIVATE detent::detent)

# A reference publisher for the FILE source specifically (writes/rewrites a
# local snapshot file atomically). The distributor (distributor/) is the
# HTTP-based equivalent for the network distribution path.
add_executable(detent_publish publish_snapshot.cc)
target_link_libraries(detent_publish PRIVATE detent::detent)
