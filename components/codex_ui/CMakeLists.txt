idf_component_register(SRCS "codex_ui.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES codex_input codex_model)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
