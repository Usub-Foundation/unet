cmake_minimum_required(VERSION 3.22)

project(new_form_data_tests)

add_executable(test_form_data
    test_form_data.cpp
)

add_executable(test_x_www_form_urlencoded
    test_x_www_form_urlencoded.cpp
)

add_executable(test_headers_compat
    test_headers_compat.cpp
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

add_executable(test_http_client_persistence
    test_http_client_persistence.cpp
)

add_executable(test_http_client_proxy
    test_http_client_proxy.cpp
)

add_executable(test_ws_parser
    test_ws_parser.cpp
)

add_executable(test_ws_server
    test_ws_server.cpp
)

if(WIN32)
    add_executable(test_http_schannel_server
        test_http_schannel_server.cpp
    )
endif()

find_package(OpenSSL QUIET)

if(OpenSSL_FOUND)
    add_executable(test_http_openssl_server
        test_http_openssl_server.cpp
    )

    add_executable(test_openssl_stream_roundtrip
        test_openssl_stream_roundtrip.cpp
    )

    add_executable(test_http_client_example
        test_http_client_example.cpp
    )
endif()

# Include new headers
target_include_directories(test_form_data PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_x_www_form_urlencoded PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_headers_compat PRIVATE
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

target_include_directories(test_http_client_persistence PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_http_client_proxy PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_ws_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

target_include_directories(test_ws_server PRIVATE
    ${CMAKE_SOURCE_DIR}/new-include
)

if(WIN32)
    target_include_directories(test_http_schannel_server PRIVATE
        ${CMAKE_SOURCE_DIR}/new-include
    )
endif()

if(OpenSSL_FOUND)
    target_include_directories(test_http_openssl_server PRIVATE
        ${CMAKE_SOURCE_DIR}/new-include
    )

    target_include_directories(test_openssl_stream_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/new-include
    )

    target_include_directories(test_http_client_example PRIVATE
        ${CMAKE_SOURCE_DIR}/new-include
    )
endif()

if(TARGET unet)
    target_link_libraries(test_form_data PRIVATE unet)
    target_link_libraries(test_x_www_form_urlencoded PRIVATE unet)
    target_link_libraries(test_headers_compat PRIVATE unet)
    target_link_libraries(test_http1_parser PRIVATE unet)
    target_link_libraries(test_http2_parser PRIVATE unet)
    target_link_libraries(test_http_server PRIVATE unet)
    target_link_libraries(test_http_client_persistence PRIVATE unet)
    target_link_libraries(test_http_client_proxy PRIVATE unet)
    target_link_libraries(test_ws_parser PRIVATE unet)
    target_link_libraries(test_ws_server PRIVATE unet)

    if(WIN32)
        target_link_libraries(test_http_schannel_server PRIVATE unet Secur32 Crypt32)
    endif()

    if(OpenSSL_FOUND)
        target_link_libraries(test_http_openssl_server PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
        target_link_libraries(test_openssl_stream_roundtrip PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
        target_link_libraries(test_http_client_example PRIVATE unet OpenSSL::Crypto OpenSSL::SSL)
    endif()
endif()

# Simple test run target
add_custom_target(run_form_data_tests
    COMMAND test_form_data
    DEPENDS test_form_data
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_x_www_form_urlencoded_tests
    COMMAND test_x_www_form_urlencoded
    DEPENDS test_x_www_form_urlencoded
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_headers_compat_tests
    COMMAND test_headers_compat
    DEPENDS test_headers_compat
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

add_custom_target(run_ws_parser_tests
    COMMAND test_ws_parser
    DEPENDS test_ws_parser
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(run_ws_server_test
    COMMAND test_ws_server
    DEPENDS test_ws_server
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(OpenSSL_FOUND)
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
endif()

if(WIN32)
    add_custom_target(run_http_schannel_server_tests
        COMMAND test_http_schannel_server
        DEPENDS test_http_schannel_server
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
