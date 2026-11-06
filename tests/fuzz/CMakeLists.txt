if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TINYSHP_BUILD_FUZZER needs clang (libFuzzer)")
endif()

add_executable(tinyshp_fuzz fuzz_load.cpp)
target_link_libraries(tinyshp_fuzz PRIVATE tinyshp::tinyshp tinyshp_warnings)
target_compile_options(tinyshp_fuzz PRIVATE -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
target_link_options(tinyshp_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
