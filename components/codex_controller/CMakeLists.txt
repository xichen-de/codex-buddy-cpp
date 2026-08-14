idf_component_register(SRCS "codex_controller.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES codex_input codex_model codex_protocol)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
