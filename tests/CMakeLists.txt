add_executable(tinyshp_test shp_writer.h test_framework.h tinyshp_test.cpp)
target_link_libraries(tinyshp_test PRIVATE tinyshp::tinyshp tinyshp_warnings)
add_test(NAME tinyshp_test COMMAND tinyshp_test)

add_executable(tinyshp_reference_test reference_test.cpp test_framework.h)
target_link_libraries(tinyshp_reference_test PRIVATE tinyshp::tinyshp tinyshp_warnings)
target_compile_definitions(tinyshp_reference_test PRIVATE TINYSHP_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
add_test(NAME tinyshp_reference_test COMMAND tinyshp_reference_test)

if(EMSCRIPTEN)
    # the tests run under node and read the data and temporary files from the real file system
    target_link_options(tinyshp_test PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    target_link_options(tinyshp_reference_test PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
endif()

if(TARGET shp2geojson)
    add_test(NAME tinyshp_example_geojson
             COMMAND shp2geojson ${PROJECT_SOURCE_DIR}/data/naturalearth/ne_110m_lakes.shp
                     ${CMAKE_CURRENT_BINARY_DIR}/lakes.geojson)
    set_tests_properties(tinyshp_example_geojson PROPERTIES PASS_REGULAR_EXPRESSION "24 features")
    add_test(NAME tinyshp_example_obj
             COMMAND shp2obj ${PROJECT_SOURCE_DIR}/data/cambridge/cambridge_kendall_buildings.shp
                     ${CMAKE_CURRENT_BINARY_DIR}/kendall.obj)
    set_tests_properties(tinyshp_example_obj PROPERTIES PASS_REGULAR_EXPRESSION "709 shapes")
    if(EMSCRIPTEN)
        target_link_options(shp2geojson PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
        target_link_options(shp2obj PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
        target_link_options(shp_info PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    endif()
endif()
