idf_component_register(SRCS "codex_ui.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES buddy_layout codex_input codex_model ui_font)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BUDDY_USE_LVGL_FONT=1)
