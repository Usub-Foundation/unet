cmake_minimum_required(VERSION 3.27)
project(upqWebserverTest)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(
        upq
        GIT_REPOSITORY https://github.com/Usub-development/upq.git
        GIT_TAG main
)

FetchContent_MakeAvailable(upq)

add_executable(upqWebserverTest main.cpp)

set(UPQ_LINK_TARGET "")
if(TARGET usub::upq)
    set(UPQ_LINK_TARGET usub::upq)
elseif(TARGET upq::upq)
    set(UPQ_LINK_TARGET upq::upq)
elseif(TARGET upq)
    set(UPQ_LINK_TARGET upq)
else()
    message(FATAL_ERROR
        "upq was fetched but no known target was exported. "
        "Expected one of: usub::upq, upq::upq, upq")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    usub::uvent
    usub::unet
    ${UPQ_LINK_TARGET}
)
