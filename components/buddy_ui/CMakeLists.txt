idf_component_register(SRCS "buddy_ui.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES claude_model)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
