# Installation

## Requirements

- Linux environment (current project target)
- C++23 compiler
- CMake 3.22+
- Git

Optional:

- OpenSSL (required for TLS stream features/tests)

## Build From Source

```bash
git clone https://github.com/Usub-development/unet.git
cd unet
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Install

```bash
cmake --install build
```

By default, headers are installed under your CMake install prefix (for example `/usr/local/include`) and CMake package files are installed under `lib/cmake/unet`.

## Use From Another CMake Project

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_app LANGUAGES CXX)

find_package(unet REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE usub::unet)
```

## FetchContent (Development)

```cmake
include(FetchContent)

FetchContent_Declare(
  unet
  GIT_REPOSITORY https://github.com/Usub-development/unet.git
  GIT_TAG main
)

FetchContent_MakeAvailable(unet)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE usub::unet)
```

## Build Options (Current State)

Declared options in `CMakeLists.txt`:

- `UNET_BUILD_EXAMPLES` (default `OFF`)
- `UNET_BUILD_TESTS` (default `ON`)

Current top-level conditions still check `BUILD_EXAMPLES` for enabling subdirectories, so if you want examples/tests added by this file as-is, use:

```bash
cmake -B build -DBUILD_EXAMPLES=ON
```

## Generate Documentation Site

If MkDocs is installed:

```bash
mkdocs build -f docs/mkdocs.yml
# or
mkdocs serve -f docs/mkdocs.yml
```
