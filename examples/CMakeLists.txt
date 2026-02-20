cmake_minimum_required(VERSION 3.22)

add_executable(HTTPServer
    HTTPServer.cpp
)
target_link_libraries(HTTPServer PRIVATE unet usub::uvent)

add_executable(ClientCLI
    ClientCLI.cpp
)
target_link_libraries(ClientCLI PRIVATE unet usub::uvent)

add_executable(UventDemo
    UventDemo.cpp
)
target_link_libraries(UventDemo PRIVATE usub::uvent)

add_subdirectory(upq_integration)