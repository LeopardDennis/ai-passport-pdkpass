cmake_minimum_required(VERSION 3.16)

if(NOT APPLE)
    message(FATAL_ERROR "The PDKPASS simulator currently requires macOS.")
endif()

project(pdkpass_simulator LANGUAGES C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h"
    CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

get_filename_component(PDKPASS_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${PDKPASS_REPO}/managed_components/lvgl__lvgl" lvgl)

# Keep the host UI within the firmware's fixed LVGL pool, rather than silently
# using LVGL's desktop default. Reconfigure when firmware defaults change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PDKPASS_REPO}/sdkconfig.defaults")
file(STRINGS "${PDKPASS_REPO}/sdkconfig.defaults" PDKPASS_POOL_LINE
    REGEX "^CONFIG_LV_MEM_SIZE_KILOBYTES=[0-9]+$")
if(NOT PDKPASS_POOL_LINE MATCHES "=([0-9]+)$")
    message(FATAL_ERROR "Missing firmware LVGL memory budget")
endif()
math(EXPR PDKPASS_POOL_BYTES "${CMAKE_MATCH_1} * 1024")
target_compile_definitions(lvgl PUBLIC LV_MEM_SIZE=${PDKPASS_POOL_BYTES})

set(PDKPASS_UI_SOURCES
    "${PDKPASS_REPO}/main/ui_pixel.c"
    "${PDKPASS_REPO}/main/ui_pixel_math.c"
    "${PDKPASS_REPO}/main/pdkpass_ui.c"
    "${PDKPASS_REPO}/main/pdkpass_model.c"
    "${PDKPASS_REPO}/main/pdkpass_results_core.c"
    "${PDKPASS_REPO}/main/pdkpass_schedule.c"
    "${PDKPASS_REPO}/main/pdkpass_data.c"
    "${PDKPASS_REPO}/main/pdkpass_theme.c"
    "${PDKPASS_REPO}/main/pdkpass_tracks.c"
)

find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(UTTYPE_FRAMEWORK UniformTypeIdentifiers REQUIRED)

add_executable(pdkpass-simulator simulator.m ${PDKPASS_UI_SOURCES})
add_executable(pdkpass-ui-memory-test test_ui_memory.m ${PDKPASS_UI_SOURCES})
foreach(target pdkpass-simulator pdkpass-ui-memory-test)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${PDKPASS_REPO}/main")
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(${target} PRIVATE lvgl::lvgl m
        "${COCOA_FRAMEWORK}" "${UTTYPE_FRAMEWORK}")
endforeach()
enable_testing()
add_test(NAME pdkpass-ui-memory COMMAND pdkpass-ui-memory-test)
set_tests_properties(pdkpass-ui-memory PROPERTIES TIMEOUT 30)
