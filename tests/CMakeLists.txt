cmake_minimum_required(VERSION 3.22)

if(NOT TARGET unet)
    message(FATAL_ERROR "tests/ require the unet target")
endif()

enable_testing()

set(UNET_TEST_SOURCES
    test_request_parser.cpp
    test_response_serializer.cpp
    test_router.cpp
    test_body_reader.cpp
)

foreach(src IN LISTS UNET_TEST_SOURCES)
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE unet usub::uvent)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
