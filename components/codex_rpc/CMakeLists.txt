idf_component_register(SRCS "codex_rpc.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES codex_model codex_protocol fixed_json)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
