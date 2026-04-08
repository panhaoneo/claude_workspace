cmake_minimum_required(VERSION 3.14)
project(shm_mmap CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─────────────────────────────────────────────────────────────────────────────
# 编译选项
# ─────────────────────────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# GoogleTest（优先系统包，回退到 FetchContent）
# ─────────────────────────────────────────────────────────────────────────────
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# 主库
# ─────────────────────────────────────────────────────────────────────────────
add_library(shm_mmap_lib STATIC
    src/mmap_file.cpp
    src/tick_file_pool.cpp
    src/mmap_manager.cpp
)
target_include_directories(shm_mmap_lib PUBLIC include)
target_link_libraries(shm_mmap_lib PUBLIC pthread)

# ─────────────────────────────────────────────────────────────────────────────
# 测试
# ─────────────────────────────────────────────────────────────────────────────
enable_testing()

function(add_shm_test name)
    add_executable(${name} test/${name}.cpp)
    target_link_libraries(${name} PRIVATE shm_mmap_lib GTest::gtest GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

add_shm_test(test_mmap_file)
add_shm_test(test_tick_pool)
add_shm_test(test_snapshot)
add_shm_test(test_tick)
add_shm_test(test_mmap_manager)
