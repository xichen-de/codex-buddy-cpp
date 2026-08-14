idf_component_register(SRCS "codex_ble_transport.cpp" "ble_hid_compat.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_hid codex_protocol)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# macOS forwards Report ID 6 in the BLE output characteristic value. Wrap the
# ESP-IDF table creation call so the characteristic can hold that extra byte
# without changing the HID descriptor ChatGPT enumerates.
target_link_options(${COMPONENT_LIB} INTERFACE
                    "-Wl,--wrap=esp_ble_gatts_create_attr_tab")
