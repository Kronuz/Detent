add_executable(test_delivery_response test_response.cc)
target_link_libraries(test_delivery_response PRIVATE detent::delivery_http)
add_test(NAME test_delivery_response COMMAND test_delivery_response)
find_program(DETENT_DELIVERY_TEST_OPENSSL openssl REQUIRED)
add_test(NAME delivery_tls_certificates COMMAND ${CMAKE_COMMAND}
  -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tls-fixture -DOPENSSL=${DETENT_DELIVERY_TEST_OPENSSL}
  -P ${CMAKE_SOURCE_DIR}/tests/detent_authority/tls_certificates.cmake)
set_tests_properties(delivery_tls_certificates PROPERTIES FIXTURES_SETUP delivery_tls TIMEOUT 30)
add_executable(test_delivery_client test_client.cc)
target_link_libraries(test_delivery_client PRIVATE detent::delivery_transport)
add_test(NAME test_delivery_client COMMAND test_delivery_client ${CMAKE_CURRENT_BINARY_DIR}/tls-fixture)
set_tests_properties(test_delivery_client PROPERTIES FIXTURES_REQUIRED delivery_tls TIMEOUT 30)
