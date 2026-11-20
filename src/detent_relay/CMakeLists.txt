# The layout encoding the mirror stores and every upstream answers; the authority links it to export.
if(TARGET hashes::hashes)
  add_library(detent_relay_layout STATIC layout.cc)
  target_include_directories(detent_relay_layout PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(detent_relay_layout PUBLIC detent::detent PRIVATE hashes::hashes)
  target_compile_features(detent_relay_layout PUBLIC cxx_std_20)
  add_library(detent::relay_layout ALIAS detent_relay_layout)
endif()
# The relay's mirror of blocks, base and sections, and its W5/W6 reading and answers (namespace
# delivery D7, W8). It decodes the authority's history files and links no consensus: a reader, never a member.
if(TARGET completion::completion AND TARGET hashes::hashes)
  add_library(detent_relay_mirror STATIC mirror.cc namespace_relay.cc)
  target_include_directories(detent_relay_mirror PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(detent_relay_mirror PUBLIC detent::detent detent::distribution detent::relay_layout
    completion::completion PRIVATE detent::authority hashes::hashes)
  target_compile_features(detent_relay_mirror PUBLIC cxx_std_20)
  add_library(detent::relay_mirror ALIAS detent_relay_mirror)
endif()
# The mirror as the relay serves it (W8): one thread owning NamespaceRelay, read from its upstream
# over HTTPS with a credential, never as a consensus member.
if(TARGET detent::relay_mirror AND TARGET detent::delivery_transport)
  find_package(OpenSSL 3.0 REQUIRED)
  add_library(detent_relay_mirror_service STATIC mirror_service.cc http_upstream.cc)
  target_link_libraries(detent_relay_mirror_service PUBLIC detent::relay_mirror detent::delivery_transport
    PRIVATE http_parser::http_parser OpenSSL::Crypto)
  add_library(detent::relay_mirror_service ALIAS detent_relay_mirror_service)
endif()
add_library(detent_relay_core EXCLUDE_FROM_ALL bundle.cc sessions.cc registry.cc)
target_link_libraries(detent_relay_core PUBLIC detent::distribution detent::local_export)
add_library(detent::relay ALIAS detent_relay_core)
if(TARGET detent::distribution_http AND TARGET detent::relay_mirror_service)
  find_package(OpenSSL 3.0 REQUIRED)
  add_library(detent_relay_http EXCLUDE_FROM_ALL service.cc)
  target_link_libraries(detent_relay_http PUBLIC detent::relay detent::distribution_http detent::relay_mirror_service
    OpenSSL::Crypto)
  add_library(detent::relay_http ALIAS detent_relay_http)
endif()
if(DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  add_library(detent_relay_config EXCLUDE_FROM_ALL configuration.cc)
  target_link_libraries(detent_relay_config PUBLIC detent::relay detent::delivery_config detent::relay_mirror_service)
  add_library(detent::relay_config ALIAS detent_relay_config)
endif()
# Dense distribution D7.3: the relay's page window and the Merkle diff that names a delta transfer.
add_library(detent_relay_page_window STATIC page_window.cc)
target_include_directories(detent_relay_page_window PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(detent_relay_page_window PUBLIC detent::local_codec detent::distribution)
add_library(detent::relay_page_window ALIAS detent_relay_page_window)
