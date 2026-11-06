cmake_minimum_required(VERSION 3.25)

project(
    tinyshp
    VERSION 1.0.0
    DESCRIPTION "ESRI shapefile (.shp, .dbf, .prj) loader, C++23, single header"
    HOMEPAGE_URL "https://github.com/varnholt/tinyshp"
    LANGUAGES CXX)

option(TINYSHP_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(TINYSHP_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(TINYSHP_BUILD_TESTBED "Build the OpenGL ES 3 / WebGL2 testbed (fetches GLFW and Dear ImGui)" OFF)
option(TINYSHP_BUILD_FUZZER "Build the libFuzzer target (clang only)" OFF)
option(TINYSHP_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

add_library(tinyshp INTERFACE)
add_library(tinyshp::tinyshp ALIAS tinyshp)

target_include_directories(tinyshp INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                             $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(tinyshp INTERFACE cxx_std_23)

# warnings for our own targets only, never forced onto consumers
if(TINYSHP_BUILD_TESTS
   OR TINYSHP_BUILD_EXAMPLES
   OR TINYSHP_BUILD_TESTBED
   OR TINYSHP_BUILD_FUZZER)
    add_library(tinyshp_warnings INTERFACE)
    if(MSVC)
        target_compile_options(tinyshp_warnings INTERFACE /W4 /WX /permissive- /utf-8)
    else()
        # partially initialized aggregates like {._encoding = ...} are the intended way to use the options
        target_compile_options(
            tinyshp_warnings
            INTERFACE -Wall
                      -Wextra
                      -Wpedantic
                      -Wconversion
                      -Wshadow
                      -Werror
                      -Wno-missing-field-initializers)
    endif()
endif()

# before the tests, which run the examples
if(TINYSHP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TINYSHP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TINYSHP_BUILD_TESTBED)
    add_subdirectory(testbed)
endif()

if(TINYSHP_BUILD_FUZZER)
    add_subdirectory(tests/fuzz)
endif()

if(TINYSHP_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS tinyshp EXPORT tinyshpTargets)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(
        EXPORT tinyshpTargets
        NAMESPACE tinyshp::
        FILE tinyshpConfig.cmake
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinyshp)

    write_basic_package_version_file(${PROJECT_BINARY_DIR}/tinyshpConfigVersion.cmake
                                     COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES ${PROJECT_BINARY_DIR}/tinyshpConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinyshp)
endif()
