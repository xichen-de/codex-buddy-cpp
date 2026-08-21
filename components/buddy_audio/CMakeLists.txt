idf_component_register(SRCS "buddy_audio.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES buddy_settings platform_core_s3)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
