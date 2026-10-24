########################################################################
# detent: the core client library (zero networking dependencies)
########################################################################
add_library(detent
  detent/hashing.cc
  detent/json_schema.cc
  detent/json_patch.cc
  detent/overrides.cc
  detent/client.cc
  detent/read_view.cc
  detent/global.cc
  detent/testing.cc
  detent/test_hook.cc)

target_include_directories(detent PUBLIC
  "${CMAKE_SOURCE_DIR}/include")
# SYSTEM: rapidjson's own headers use std::iterator (deprecated since
# C++17) -- this is their code, not ours, so their deprecation warnings
# shouldn't show up as if they were.
target_include_directories(detent SYSTEM PUBLIC
  "${rapidjson_SOURCE_DIR}/include")

target_link_libraries(detent PUBLIC
  Threads::Threads
  char_classify::char_classify
  strict_stox::strict_stox
  sha256::sha256
  atomic-shared-ptr::atomic-shared-ptr
  random::random)

target_compile_features(detent PUBLIC cxx_std_20)

add_library(detent::detent ALIAS detent)
add_subdirectory(detent_distribution)
if(TARGET http::http)
  add_subdirectory(detent_distribution_http)
endif()
if(DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  if(NOT TARGET http_parser::http_parser)
    FetchContent_Declare(http_parser GIT_REPOSITORY https://github.com/Kronuz/http-parser.git GIT_TAG main)
    FetchContent_MakeAvailable(http_parser)
  endif()
  add_subdirectory(detent_delivery)
endif()
add_subdirectory(detent_local)
add_subdirectory(detent_relay)
if(DETENT_BUILD_MANAGED_AGENT OR DETENT_BUILD_RELAY)
  add_library(detent_delivery_coordinator detent_delivery/coordinator.cc detent_delivery/completion_scheduler.cc)
  target_include_directories(detent_delivery_coordinator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/detent_local")
  target_link_libraries(detent_delivery_coordinator PUBLIC detent::delivery_transport detent::local_publication)
  add_library(detent::delivery_coordinator ALIAS detent_delivery_coordinator)
  add_library(detent_delivery_config detent_delivery/configuration.cc)
  target_link_libraries(detent_delivery_config PUBLIC detent::delivery_coordinator reactor::tls)
  add_library(detent::delivery_config ALIAS detent_delivery_config)
endif()

########################################################################
# detent_http: optional client-side HTTP polling source (POSIX sockets
# only, no Asio) -- plugs into InitOptions::source_poll. Kept as a SEPARATE
# target from detent itself so a consumer who only wants the file source
# never pulls socket/networking code in at all (AGENTS.md's "keep them
# small": don't grow a dependency into a library that currently has none).
# compressors is the one deliberate exception: it lets this client actually
# request the zstd/gzip encodings detentd already serves (see
# http_client.cc's Accept-Encoding) instead of always polling uncompressed
# JSON -- still no Asio/http-framework/cluster weight, just one codec lib.
########################################################################
add_library(detent_http
  detent_http/http_client.cc
  detent_http/http_source.cc)

target_include_directories(detent_http PUBLIC
  "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(detent_http PUBLIC detent::detent compressors::compressors)
target_compile_features(detent_http PUBLIC cxx_std_20)

add_library(detent_http::detent_http ALIAS detent_http)

# Shared CLI-only configuration and platform paths; no networking.
add_library(detent_logging detent_cli/logging.cc)
target_link_libraries(detent_logging PUBLIC logger::logger)
target_compile_definitions(detent_logging PUBLIC LOGGER_DEBUG=1)

add_library(detent_cli detent_cli/config.cc detent_cli/daemon.cc)
target_link_libraries(detent_cli PUBLIC detent::detent)

########################################################################
# detentd: the distribution-layer daemon (Section 10's "any publisher that
# emits valid snapshots" extension seam, made concrete). Pulls the Kronuz
# service stack: reactor (event-driven TCP, thread-per-core) + http
# (the HTTP/1.1 framework, radix routing, conditional-GET/ETag) + cluster
# (UDP multicast bus, for cross-replica gossip). detentd.hh stays private
# (colocated with detentd.cc, not under include/) -- nothing outside this
# directory ever includes it, unlike detent/detent_http's public headers.
########################################################################
if(DETENT_BUILD_DISTRIBUTOR)
  add_library(detentd_lib detentd/detentd.cc detentd/raft_delegate.cc)

  target_include_directories(detentd_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/detentd")

  target_link_libraries(detentd_lib PUBLIC
    detent::detent
    detent_http::detent_http
    http::http
    cluster::cluster
    generation_cache::generation_cache
    reactor::tls
    logger::logger)
  target_compile_definitions(detentd_lib PUBLIC REACTOR_TRACE_HEADER="logger.h")

  target_compile_features(detentd_lib PUBLIC cxx_std_20)

  add_executable(detentd detentd/main.cc)
  target_link_libraries(detentd PRIVATE detentd_lib detent_cli detent_logging http_log::http_log)
endif()

########################################################################
# detent_agent: the host-local daemon + its own CLI in one binary (see
# README's "detent-agent" section). Shares one warm detent::Client across
# every local reader via a shared-memory snapshot (ShmWriter/ShmReader),
# instead of each local process running its own updater thread against
# detentd or going through a socket round trip. Private headers, same
# reasoning as detentd.hh above.
########################################################################
add_library(detent_agent_lib detent_agent/agent.cc detent_agent/shm_writer.cc detent_agent/shm_reader.cc)

target_include_directories(detent_agent_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/detent_agent")

target_link_libraries(detent_agent_lib PUBLIC detent::detent)
target_compile_features(detent_agent_lib PUBLIC cxx_std_20)

add_executable(detent-agent detent_agent/main.cc detent_agent/config.cc detent_agent/reader_capacity.cc)
target_link_libraries(detent-agent PRIVATE detent_agent_lib detent_http::detent_http detent_cli detent_logging)
if(DETENT_BUILD_MANAGED_AGENT)
  add_library(detent_managed_agent detent_agent/managed_host.cc)
  target_link_libraries(detent_managed_agent PUBLIC detent::delivery_config PRIVATE logger::logger)
  target_link_libraries(detent-agent PRIVATE detent_managed_agent)
  target_compile_definitions(detent-agent PRIVATE DETENT_WITH_MANAGED_AGENT=1)
else()
  target_link_libraries(detent-agent PRIVATE detent::local_view)
endif()

include(GNUInstallDirs)
if(DETENT_BUILD_RELAY)
  find_package(OpenSSL 3.0 REQUIRED)
  add_library(detent_relay_host detent_relay/host.cc)
  target_link_libraries(detent_relay_host PUBLIC detent::relay_http detent::relay_config PRIVATE detent_logging detent_cli reactor::tls)
  add_executable(detent-relay detent_relay/main.cc)
  target_link_libraries(detent-relay PRIVATE detent_relay_host detent_cli detent_logging http_parser::http_parser)
  install(TARGETS detent-relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS detent-agent RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(DETENT_BUILD_DISTRIBUTOR)
  install(TARGETS detentd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY ${CMAKE_SOURCE_DIR}/packaging/ DESTINATION ${CMAKE_INSTALL_DATADIR}/detent USE_SOURCE_PERMISSIONS)
install(FILES ${CMAKE_SOURCE_DIR}/docs/SERVICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/detent)

add_executable(detent-reader detent_agent/main.cc detent_agent/config.cc detent_agent/reader_capacity.cc)
set_target_properties(detent-reader PROPERTIES OUTPUT_NAME detent)
target_compile_definitions(detent-reader PRIVATE DETENT_READER=1)
target_link_libraries(detent-reader PRIVATE detent_agent_lib detent_cli)
target_link_libraries(detent-reader PRIVATE detent::local_client)
install(TARGETS detent-reader RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Authority application semantics remain independent of transport and consensus.
add_subdirectory(detent_authority)

if(DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY)
  target_compile_definitions(detentd PRIVATE DETENT_WITH_AUTHORITY=1)
  target_link_libraries(detentd PRIVATE detent_authority_service)
endif()
