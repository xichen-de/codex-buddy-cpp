idf_component_register(SRCS "ui_font.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
