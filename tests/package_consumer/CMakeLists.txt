# builds against an installed tinyshp to check that find_package works and pulls in nothing but the header.
#   cmake --install <build> --prefix <prefix>
#   cmake -S tests/package_consumer -B <out> -DCMAKE_PREFIX_PATH=<prefix>
cmake_minimum_required(VERSION 3.25)
project(tinyshp_package_consumer LANGUAGES CXX)

find_package(tinyshp 1 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tinyshp::tinyshp)

# the package exports exactly one interface target with only an include directory and the c++ standard
get_target_property(link_libraries tinyshp::tinyshp INTERFACE_LINK_LIBRARIES)
if(link_libraries)
    message(FATAL_ERROR "tinyshp::tinyshp links ${link_libraries}, it must not depend on anything")
endif()
