cmake_minimum_required(VERSION 3.16)
project(codex_buddy_ui_preview LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LVGL_ROOT "${PROJECT_ROOT}/managed_components/lvgl__lvgl")
if(NOT EXISTS "${LVGL_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL is not installed; run pio run first")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_ROOT}" lvgl)

add_executable(render_buddy_ui
    "${PROJECT_ROOT}/test/host/render_buddy_ui.cpp"
    "${PROJECT_ROOT}/components/buddy_ui/buddy_ui.cpp"
    "${PROJECT_ROOT}/components/claude_model/claude_model.cpp"
    "${PROJECT_ROOT}/components/codex_ui/codex_ui.cpp"
    "${PROJECT_ROOT}/components/codex_model/codex_model.cpp"
    "${PROJECT_ROOT}/components/ui_font/ui_font.cpp")

target_compile_definitions(render_buddy_ui PRIVATE BUDDY_USE_LVGL_FONT=1)
target_compile_options(render_buddy_ui PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(render_buddy_ui PRIVATE
    "${PROJECT_ROOT}/components/buddy_ui/include"
    "${PROJECT_ROOT}/components/claude_model/include"
    "${PROJECT_ROOT}/components/codex_ui/include"
    "${PROJECT_ROOT}/components/codex_input/include"
    "${PROJECT_ROOT}/components/codex_model/include"
    "${PROJECT_ROOT}/components/codex_protocol/include"
    "${PROJECT_ROOT}/components/ui_font/include")
target_link_libraries(render_buddy_ui PRIVATE lvgl::lvgl)
