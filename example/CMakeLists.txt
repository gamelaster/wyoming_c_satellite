add_executable(wyoming_c_satellite main.c)

find_package(PkgConfig REQUIRED)

# Wyoming Satellite

target_sources(wyoming_c_satellite PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_server.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_event_decoder.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_event_handler.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_component.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_mode_always_stream.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/satellite_mode_wake_stream.c
)

target_include_directories(wyoming_c_satellite PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} # So Wyoming library can include wyoming_user.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# cJSON
pkg_check_modules(CJSON REQUIRED libcjson)

target_link_libraries(wyoming_c_satellite PUBLIC ${CJSON_LIBRARIES})
target_include_directories(wyoming_c_satellite PUBLIC ${CJSON_INCLUDE_DIRS})
target_compile_options(wyoming_c_satellite PUBLIC ${CJSON_CFLAGS_OTHER})

target_link_libraries(wyoming_c_satellite PUBLIC pthread)