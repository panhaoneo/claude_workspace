cmake_minimum_required(VERSION 3.10)
project(efvi-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(EFVI_MOCK_MODE   "Build without real OpenOnload (for CI / unit tests)" OFF)
option(EFVI_BUILD_TESTS "Build unit tests"                                    ON)
option(EFVI_BUILD_EXAMPLES "Build examples"                                   OFF)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------

add_library(efvi-cpp
    src/vi_impl.cpp
    src/vi_set_impl.cpp
    src/checksum.cpp
)

target_include_directories(efvi-cpp
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(EFVI_MOCK_MODE)
    target_compile_definitions(efvi-cpp PUBLIC EFVI_MOCK_MODE)
    target_sources(efvi-cpp PRIVATE src/mock/mock_efvi.cpp)
else()
    if(NOT DEFINED ONLOAD_SRC_DIR)
        message(FATAL_ERROR
            "ONLOAD_SRC_DIR must point to the OpenOnload source tree "
            "when building without mock mode.\n"
            "Example: cmake -DONLOAD_SRC_DIR=/path/to/openonload ..")
    endif()
    target_include_directories(efvi-cpp
        PRIVATE ${ONLOAD_SRC_DIR}/src/include
                ${ONLOAD_SRC_DIR}/src/include/onload
    )
endif()

target_compile_options(efvi-cpp PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

if(EFVI_BUILD_TESTS)
    enable_testing()

    # Try system GTest first; fall back to FetchContent for CI environments
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
        add_library(GTest::GTest ALIAS gtest)
        add_library(GTest::Main  ALIAS gtest_main)
    endif()

    add_executable(efvi_tests
        tests/test_init.cpp
        tests/test_tx.cpp
        tests/test_rx.cpp
        tests/test_filter.cpp
        tests/test_stats.cpp
        tests/test_multiqueue.cpp
        tests/test_log.cpp
    )

    target_link_libraries(efvi_tests
        PRIVATE efvi-cpp GTest::GTest GTest::Main
    )

    # Tests always run in mock mode
    target_compile_definitions(efvi_tests PRIVATE EFVI_MOCK_MODE)

    # Give tests access to internal mock headers
    target_include_directories(efvi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Ensure mock sources are compiled for the test binary even if the library
    # was built without mock mode.
    if(NOT EFVI_MOCK_MODE)
        target_sources(efvi_tests PRIVATE src/mock/mock_efvi.cpp)
        target_compile_definitions(efvi-cpp INTERFACE EFVI_MOCK_MODE)
    endif()

    include(GoogleTest)
    gtest_discover_tests(efvi_tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(EFVI_BUILD_EXAMPLES AND NOT EFVI_MOCK_MODE)
    foreach(example udp_sink udp_send multiqueue_sink)
        add_executable(${example} examples/${example}.cpp)
        target_link_libraries(${example} PRIVATE efvi-cpp)
    endforeach()
endif()
