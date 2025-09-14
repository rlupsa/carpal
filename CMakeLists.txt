# Copyright Radu Lupsa 2023
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

cmake_minimum_required(VERSION 2.24)
project(carpal LANGUAGES CXX)

option(BUILD_CARPAL_TESTS "Build tests" ON)
option(ENABLE_COROUTINES "Enable coroutine based functionality" ON)
option(CARPAL_ENABLE_LOGGING "Enable logging of carpal-related events" ON)

if(ENABLE_COROUTINES)
set(CXX_STANDARD 20)
else()
set(CXX_STANDARD 17)
endif()

#if(BUILD_CARPAL_TESTS)
#set(CMAKE_BUILD_TYPE Debug)
#else()
#set(CMAKE_BUILD_TYPE RelWithDebInfo)
#endif()

# Library
configure_file("src/config.h.in" "include/carpal/config.h")
set(CARPAL_SOURCES
  "src/Future.cpp"
  "src/PromiseFuturePair.cpp"
  "src/ThreadPool.cpp"
  "src/Timer.cpp")
set(CARPAL_HEADERS
  "${CMAKE_CURRENT_BINARY_DIR}/include/carpal/config.h"
  "src/include/carpal/Logger.h"
  "src/include/carpal/Executor.h"
  "src/include/carpal/Future.h"
  "src/include/carpal/FutureHelpers.h"
  "src/include/carpal/IntrusiveSharedPtr.h"
  "src/include/carpal/PromiseFuturePair.h"
  "src/include/carpal/SingleProducerConsumerQueue.h"
  "src/include/carpal/StreamSource.h"
  "src/include/carpal/ThreadPool.h"
  "src/include/carpal/Timer.h")
if(ENABLE_COROUTINES)
    list(APPEND CARPAL_HEADERS
        "src/include/carpal/CoroutineScheduler.h"
        "src/include/carpal/OneThreadScheduler.h" )
    list(APPEND CARPAL_SOURCES
        "src/OneThreadScheduler.cpp")
endif(ENABLE_COROUTINES)
if(CARPAL_ENABLE_LOGGING)
    list(APPEND CARPAL_SOURCES "src/Logger.cpp")
endif(CARPAL_ENABLE_LOGGING)

add_library(carpal STATIC ${CARPAL_SOURCES} ${CARPAL_HEADERS})
target_include_directories (carpal PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include" "src/include")
set_property(TARGET carpal PROPERTY CXX_STANDARD ${CXX_STANDARD})

# Tests
if(BUILD_CARPAL_TESTS)
    message("Configuring tests")

    Include(FetchContent)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.4.0 # or a later release
      FIND_PACKAGE_ARGS 3.4
    )
    FetchContent_MakeAvailable(Catch2)

    # Helper library
    set(CARPAL_TEST_HELPERS_SOURCES
        "tests/helper/HelperTest.cpp"
        )
    set(CARPAL_TEST_HELPERS_HEADER
        "tests/helper/TestHelper.h"
        )
    if(ENABLE_COROUTINES)
        list(APPEND CARPAL_TEST_HELPERS_SOURCES
            "tests/helper/TestHelperStreamReader.cpp"
            "tests/helper/Parsers.cpp"
            )
        list(APPEND CARPAL_TEST_HELPERS_HEADER
            "tests/helper/Generator.h"
            "tests/helper/TestHelperStreamReader.h"
            "tests/helper/Parsers.h"
            )
    endif(ENABLE_COROUTINES)
    add_library(carpal_test_helper ${CARPAL_TEST_HELPERS_SOURCES} ${CARPAL_TEST_HELPERS_HEADER})
    target_include_directories(carpal_test_helper PUBLIC "tests/helper")
    target_link_libraries(carpal_test_helper carpal Catch2::Catch2WithMain)
    set_property(TARGET carpal_test_helper PROPERTY CXX_STANDARD ${CXX_STANDARD})

    # Unit tests
    set(CARPAL_TEST_SOURCES
        "tests/tests/Test.cpp"
        "tests/tests/TestFutures.cpp"
        "tests/tests/TestSingleProducerConsumerQueue.cpp"
        "tests/tests/TestTimer.cpp")
    if(ENABLE_COROUTINES)
        list(APPEND CARPAL_TEST_SOURCES
            "tests/tests/TestAsyncCoroutine.cpp"
            "tests/tests/TestAsyncGenerator.cpp"
            "tests/tests/TestCustomAwaiter.cpp"
            "tests/tests/TestOneThreadScheduler.cpp")
    endif(ENABLE_COROUTINES)
    add_executable(carpal_test ${CARPAL_TEST_SOURCES})
    target_link_libraries(carpal_test carpal carpal_test_helper Catch2::Catch2WithMain)
    set_property(TARGET carpal_test PROPERTY CXX_STANDARD ${CXX_STANDARD})

    # Performance tests
    set(CARPAL_PERF_SOURCES
        "tests/perf/Perf.cpp"
        "tests/perf/PerfSample.cpp")
    if(ENABLE_COROUTINES)
        list(APPEND CARPAL_PERF_SOURCES
            "tests/perf/PerfParser.cpp")
    endif(ENABLE_COROUTINES)
    add_executable(carpal_perf ${CARPAL_PERF_SOURCES})
    target_link_libraries(carpal_perf carpal carpal_test_helper Catch2::Catch2WithMain)
    set_property(TARGET carpal_perf PROPERTY CXX_STANDARD ${CXX_STANDARD})

endif(BUILD_CARPAL_TESTS)
