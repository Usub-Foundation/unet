# Installation

## Requirements

- C++23 compiler (GCC 13+, Clang 17+, MSVC 19.36+)
- CMake 3.22+
- Git

Optional:

- OpenSSL 1.1.1+ for TLS on POSIX (SChannel is used on Windows).
- Ninja for faster builds.

## Build from source

```
git clone https://github.com/Usub-development/unet.git
cd unet
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

To build the examples & tests:

```
cmake -S . -B build -G Ninja \
      -DUNET_BUILD_EXAMPLES=ON \
      -DUNET_BUILD_TESTS=ON
cmake --build build --parallel
```

## Install

```
cmake --install build
```

Headers go under `${CMAKE_INSTALL_PREFIX}/include`, the static lib under `lib/`, & CMake package files under `lib/cmake/unet/`.

## Consume from your own CMake project

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_app LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(unet REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE usub::unet)
```

Or via FetchContent, for a development pin:

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

## Build options

Defined in the top-level `CMakeLists.txt`:

| Option                 | Default | Effect                                      |
|------------------------|---------|---------------------------------------------|
| `UNET_BUILD_EXAMPLES`  | `OFF`   | Adds the `examples/` subdirectory.          |
| `UNET_BUILD_TESTS`     | `ON`    | Adds the `tests/` subdirectory.             |

Enable at configure time with `-DUNET_BUILD_EXAMPLES=ON` etc.

## Docs site

If you have MkDocs:

```
pip install mkdocs mkdocs-material
mkdocs serve -f docs/mkdocs.yml
```

Then open `http://127.0.0.1:8000/`. `mkdocs build -f docs/mkdocs.yml` writes the static site to `site/`.
