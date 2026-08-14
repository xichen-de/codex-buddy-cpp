idf_component_register(SRCS "claude_storage.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES claude_model nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
