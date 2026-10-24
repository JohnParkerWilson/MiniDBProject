cmake_minimum_required(VERSION 3.16)
project(localdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(localdb
    src/disk_manager.cpp
    src/schema.cpp
    src/tuple.cpp
)

target_include_directories(localdb PUBLIC include)

if (MSVC)
    target_compile_options(localdb PRIVATE /W4 /permissive-)
else()
    target_compile_options(localdb PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(localdb_tests tests/localdb_tests.cpp)
target_link_libraries(localdb_tests PRIVATE localdb)

enable_testing()
add_test(NAME localdb_tests COMMAND localdb_tests)
