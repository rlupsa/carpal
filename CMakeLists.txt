# Copyright Radu Lupsa 2023
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

cmake_minimum_required(VERSION 2.18)
project(carpal LANGUAGES CXX)

option(BUILD_CARPAL_TESTS "Build tests" ON)
option(ENABLE_COROUTINES "Enable coroutine based functionality" ON)

if(ENABLE_COROUTINES)
set(CXX_STANDARD 20)
else()
set(CXX_STANDARD 17)
endif()

# Library
set(CARPAL_SOURCES "src/Future.cpp" "src/ThreadPool.cpp" "src/Timer.cpp")
set(CARPAL_HEADERS "src/include/carpal/Executor.h" "src/include/carpal/Future.h" "src/include/carpal/ThreadPool.h" "src/include/carpal/Timer.h")
if(ENABLE_COROUTINES)
    list(APPEND CARPAL_SOURCES "src/CoroutineScheduler.cpp")
    list(APPEND CARPAL_HEADERS "src/include/carpal/CoroutineScheduler.h" "src/include/carpal/AsyncCoroutine.h")
endif(ENABLE_COROUTINES)
add_library(carpal STATIC ${CARPAL_SOURCES} ${CARPAL_HEADERS})
target_include_directories (carpal PUBLIC "src/include")
set_property(TARGET carpal PROPERTY CXX_STANDARD ${CXX_STANDARD})

# Tests
if(BUILD_CARPAL_TESTS)
    message("Configuring tests")

    Include(FetchContent)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.4.0 # or a later release
    )
    FetchContent_MakeAvailable(Catch2)

    set(CARPAL_TEST_SOURCES "tests/Test.cpp" "tests/TestHelper.h" "tests/TestFutures.cpp" "tests/TestTimer.cpp")
    if(ENABLE_COROUTINES)
        list(APPEND CARPAL_TEST_SOURCES "tests/TestAsyncCoroutine.cpp")
    endif(ENABLE_COROUTINES)
    add_executable(carpal_test ${CARPAL_TEST_SOURCES})
    target_link_libraries(carpal_test carpal Catch2::Catch2WithMain)
    set_property(TARGET carpal_test PROPERTY CXX_STANDARD ${CXX_STANDARD})
endif(BUILD_CARPAL_TESTS)
