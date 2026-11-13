add_library(detent_authority state.cc prepared.cc read_reply.cc export_scope.cc export_capture.cc
  history_format.cc history_layout.cc)
target_include_directories(detent_authority PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(detent_authority PUBLIC detent::detent detent::resources)
add_library(detent::authority ALIAS detent_authority)
add_library(detent_authority_export export_encoder.cc change_history.cc namespace_history.cc frames_export.cc namespace_sessions.cc delta_export.cc delivery_export.cc
  subscription.cc)
target_link_libraries(detent_authority_export PUBLIC detent::authority detent::distribution)
add_library(detent::authority_export ALIAS detent_authority_export)


if(DETENT_BUILD_AUTHORITY)
  add_library(detent_authority_node node.cc history.cc)
  target_link_libraries(detent_authority_node PUBLIC detent::authority consensus::consensus)
  add_library(detent::authority_node ALIAS detent_authority_node)
endif()

if(DETENT_BUILD_AUTHORITY)
  add_library(detent_authority_service service.cc)
  target_link_libraries(detent_authority_service PUBLIC detent::authority_node detent::authority_export detent::distribution_http)
endif()

if(DETENT_BUILD_AUTHORITY)
  add_library(detent_authority_transport INTERFACE)
  target_include_directories(detent_authority_transport INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(detent_authority_transport INTERFACE detent::authority_node consensus::transport http::http task::asio completion::asio)
  target_link_libraries(detent_authority_service PUBLIC detent_authority_transport)
  add_library(detent_authority_peer_history peer_history.cc)
  target_link_libraries(detent_authority_peer_history PUBLIC detent_authority_transport)
  target_link_libraries(detent_authority_service PUBLIC detent_authority_peer_history)
endif()
