add_library(detent_relay_core EXCLUDE_FROM_ALL bundle.cc sessions.cc)
target_link_libraries(detent_relay_core PUBLIC detent::distribution detent::local_export)
add_library(detent::relay ALIAS detent_relay_core)
