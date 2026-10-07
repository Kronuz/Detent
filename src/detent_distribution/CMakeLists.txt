add_library(detent_distribution protocol.cc framing.cc)
target_link_libraries(detent_distribution PUBLIC detent::detent PRIVATE compressors::compressors)
add_library(detent::distribution ALIAS detent_distribution)
