idf_component_register(SRCS "codex_input.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES codex_model codex_protocol)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
