add_library(detent_distribution_http reply_pump.cc session_request.cc)
target_link_libraries(detent_distribution_http PUBLIC detent::distribution http::http)
add_library(detent::distribution_http ALIAS detent_distribution_http)
