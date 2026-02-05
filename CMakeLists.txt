cmake_minimum_required(VERSION 3.22..3.29) #3.24
project(server VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -fpic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fpic")

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

#set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer" CACHE STRING "Debug flags" FORCE)
#set(CMAKE_C_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer" CACHE STRING "Debug flags" FORCE)
#set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined" CACHE STRING "Linker flags" FORCE)
#set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined" CACHE STRING "Linker flags" FORCE)

# ========================================
# ======= BUILDING LIBRARY =============
# ========================================

# Find uvent
include(FetchContent)

FetchContent_Declare(
    uvent 
    GIT_REPOSITORY https://github.com/Usub-development/uvent.git
    GIT_TAG main
    # FIND_PACKAGE_ARGS
)

FetchContent_MakeAvailable(uvent)


file(GLOB_RECURSE UNET_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/new-include/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/new-include/*.hpp")
file(GLOB_RECURSE UNET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/new-src/*.cpp")

list(FILTER UNET_HEADERS EXCLUDE REGEX "/new-include/unet/http/v2/")
list(FILTER UNET_SOURCES EXCLUDE REGEX "/new-src/http/v2/")

# Build the new implementation as a separate library so it can be tested independently
add_library(unet
    ${UNET_SOURCES}
    ${UNET_HEADERS}
)

add_library(usub::unet ALIAS unet)


# Link dependencies that the new implementation may need (reuse existing fetches)
target_link_libraries(unet PUBLIC usub::uvent)

# --------------------
# INSTALL
# -------------------

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

target_include_directories(unet 
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/new-include
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/new-include>
    $<INSTALL_INTERFACE:include/>
)

configure_package_config_file(
    cmake/unetConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/unetConfig.cmake
    INSTALL_DESTINATION lib/cmake/unet
)

install(TARGETS unet
    EXPORT serverTargets
    #LIBRARY DESTINATION lib
    #ARCHIVE DESTINATION lib
    #RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/server
    INCLUDES DESTINATION include/server
)

foreach(header ${UNET_HEADERS})
    get_filename_component(header_path ${header} DIRECTORY)
    file(RELATIVE_PATH header_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/new-include" ${header_path})
    install(FILES ${header} DESTINATION include/${header_relative_path})
endforeach()

install(EXPORT serverTargets
    FILE unetConfig.cmake
    NAMESPACE usub::
    DESTINATION lib/cmake/unet
)


# ========================================
# ======= BUILDING EXAMPLES =============
# ========================================

option(UNET_BUILD_EXAMPLES "Build unet examples" OFF)

if(BUILD_EXAMPLES)
    # TODO: Add example directories here
    # add_subdirectory(examples)
endif()

# ========================================
# ======= BUILDING TESTS =================
# ========================================

option(UNET_BUILD_TESTS "Build unet tests" ON)

if(BUILD_EXAMPLES)
    # TODO: Add example directories here
    add_subdirectory(new-tests)
endif()