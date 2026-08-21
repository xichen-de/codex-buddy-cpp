idf_component_register(SRCS "buddy_settings.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
