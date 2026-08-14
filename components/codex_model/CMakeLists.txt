idf_component_register(SRCS "codex_model.cpp"
                       INCLUDE_DIRS "include")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
