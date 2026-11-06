foreach(example shp_info shp2geojson shp2obj)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE tinyshp::tinyshp tinyshp_warnings)
endforeach()
