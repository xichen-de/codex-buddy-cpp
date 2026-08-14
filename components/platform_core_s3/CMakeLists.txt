idf_component_register(SRCS "platform_core_s3.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES m5stack_core_s3_noglib esp_lcd
                                esp_lcd_touch esp_codec_dev bmi270 heap)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
