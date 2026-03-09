cmake_minimum_required(VERSION 3.22)

project(new_form_data_tests)

add_executable(test_form_data
    test_form_data.cpp
)

add_executable(test_http1_parser
    test_http1_parser.cpp
)

add_executable(test_http2_parser
    test_http2_parser.cpp
)

add_executable(test_http_server
    test_http_server.cpp
)

find_package(OpenSSL)

add_executable(test_http_openssl_server
    test_http_openssl_server.cpp
)

add_executable(test_openssl_stream_roundtrip
    test_openssl_stream_roundtrip.cpp
)

add_executable(test_http_client_example
    test_http_client_example.cpp
)

add_executable(test_http_client_persistence
    test_http_client_persistence.cpp
)

add_executable(test_http_client_proxy
    test_http_client_proxy.cpp
)

# Include new headers
target_include_directories(test_form_data PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http1_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http2_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_server PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_openssl_server PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_openssl_stream_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_client_example PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_client_persistence PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_client_proxy PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

if(TARGET unet)
    target_link_libraries(test_form_data PRIVATE unet)
    target_link_libraries(test_http1_parser PRIVATE unet)
    target_link_libraries(test_http2_parser PRIVATE unet)
    target_link_libraries(test_http_server PRIVATE unet)
    target_link_libraries(test_http_openssl_server PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(test_openssl_stream_roundtrip PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(test_http_client_example PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(test_http_client_persistence PRIVATE unet)
    target_link_libraries(test_http_client_proxy PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
endif()

# Simple test run target
add_custom_target(run_form_data_tests
    COMMAND test_form_data
    DEPENDS test_form_data
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http1_parser_tests
    COMMAND test_http1_parser
    DEPENDS test_http1_parser
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http2_parser_tests
    COMMAND test_http2_parser
    DEPENDS test_http2_parser
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http_server_tests
    COMMAND test_http_server
    DEPENDS test_http_server
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http_openssl_server_tests
    COMMAND test_http_openssl_server
    DEPENDS test_http_openssl_server
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_openssl_stream_roundtrip_tests
    COMMAND test_openssl_stream_roundtrip
    DEPENDS test_openssl_stream_roundtrip
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http_client_example_test
    COMMAND test_http_client_example
    DEPENDS test_http_client_example
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http_client_persistence_test
    COMMAND test_http_client_persistence
    DEPENDS test_http_client_persistence
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_http_client_proxy_test
    COMMAND test_http_client_proxy
    DEPENDS test_http_client_proxy
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
