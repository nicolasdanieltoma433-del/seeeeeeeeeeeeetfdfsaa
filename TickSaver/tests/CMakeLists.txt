# Tests de la lógica pura (src/core). Se compilan con el compilador del host,
# sin Geode:  cmake -S tests -B build-tests && cmake --build build-tests
cmake_minimum_required(VERSION 3.21)
project(TickSaverCoreTests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/*.cpp)
add_executable(core_tests core_tests.cpp ${CORE_SOURCES})

if (MSVC)
    target_compile_options(core_tests PRIVATE /W4)
else()
    target_compile_options(core_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
