idf_component_register(SRCS "claude_ble_transport.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES bt)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
