cmake_minimum_required(VERSION 2.18)
project(carpal LANGUAGES CXX)

option(BUILD_CARPAL_TESTS "Build tests" ON)
option(BUILD_CARPAL_DOC "Create documentation" ON)

set(CARPAL_SOURCES "src/Future.cpp" "src/ThreadPool.cpp" "src/Timer.cpp")
set(CARPAL_HEADERS "src/include/carpal/Executor.h" "src/include/carpal/Future.h" "src/include/carpal/ThreadPool.h" "src/include/carpal/Timer.h")
add_library(carpal STATIC ${CARPAL_SOURCES} ${CARPAL_HEADERS})
target_include_directories (carpal PUBLIC "src/include")
set_property(TARGET carpal PROPERTY CXX_STANDARD 17)

if(ENABLE_CARPAL_TESTS)
	find_package(Catch2 REQUIRED)

	add_executable(carpal_test "tests/Test.cpp" "tests/TestHelper.h" "tests/TestFutures.cpp" "tests/TestTimer.cpp")
	target_link_libraries(carpal_test carpal Catch2::Catch2)
    set_property(TARGET carpal_test PROPERTY CXX_STANDARD 17)
endif(ENABLE_CARPAL_TESTS)

if(BUILD_CARPAL_DOC)
    message("Configuring Doxygen")
    find_package(Doxygen REQUIRED)
    
    # set input and output files
    set(DOXYGEN_CONFIG_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_CONFIG_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_CONFIG_IN} ${DOXYGEN_CONFIG_OUT} @ONLY)

    # note the option ALL which allows to build the docs together with the application
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CONFIG_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
endif(BUILD_CARPAL_DOC)
