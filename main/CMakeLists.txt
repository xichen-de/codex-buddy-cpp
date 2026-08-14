idf_component_register(SRCS "main.cpp" "codex_runtime.cpp" "claude_runtime.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES codex_controller codex_input codex_model buddy_ui
                                codex_ble_transport claude_ble_transport claude_model
                                claude_protocol claude_storage codex_protocol
                                codex_rpc motion_detector nvs_flash
                                platform_core_s3 codex_ui esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
