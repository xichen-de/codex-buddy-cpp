#include <cstddef>
#include <cstdint>

#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

namespace {

constexpr std::uint16_t HidReportUuid = 0x2A4D;
constexpr std::uint16_t CodexReportBodySize = 63;

}  // namespace

static const char *TAG = "ble_hid_compat";

/* Linker-provided reference to ESP-IDF's original attribute-table function. */
extern "C" esp_err_t __real_esp_ble_gatts_create_attr_tab(
    const esp_gatts_attr_db_t *gatts_attr_db,
    esp_gatt_if_t gatts_if,
    uint8_t max_nb_attr,
    uint8_t srvc_inst_id);

/* Reads a little-endian 16-bit UUID, returning zero for other UUID forms. */
static uint16_t uuid16(const esp_gatts_attr_db_t *attribute)
{
    if (attribute == nullptr ||
        attribute->att_desc.uuid_length != ESP_UUID_LEN_16 ||
        attribute->att_desc.uuid_p == nullptr) {
        return 0;
    }
    return static_cast<std::uint16_t>(attribute->att_desc.uuid_p[0]) |
           (static_cast<std::uint16_t>(attribute->att_desc.uuid_p[1]) << 8);
}

/* Widens only the writable 63-byte HID report attribute for macOS raw reports. */
static void allow_raw_hid_output_report(
    const esp_gatts_attr_db_t *attributes, uint8_t count)
{
    if (attributes == nullptr) return;

    for (uint8_t index = 0; index < count; ++index) {
        const esp_gatts_attr_db_t *report = &attributes[index];
        if (uuid16(report) != HidReportUuid ||
            (report->att_desc.perm & ESP_GATT_PERM_WRITE) == 0 ||
            report->att_desc.max_length != CodexReportBodySize) {
            continue;
        }

        /* ESP-IDF derives the GATT maximum from the 63-byte HID body. macOS
         * writes the 64-byte raw HID form, including Report ID 6. The attribute
         * table is heap-backed and remains mutable until registration finishes. */
        const_cast<esp_gatts_attr_db_t *>(attributes)[index].att_desc.max_length =
            CodexReportBodySize + 1;
        ESP_LOGI(TAG, "Widened writable HID report attribute %u to 64 bytes",
                 index);
    }
}

/* Applies the compatibility adjustment before delegating to ESP-IDF. */
extern "C" esp_err_t __wrap_esp_ble_gatts_create_attr_tab(
    const esp_gatts_attr_db_t *gatts_attr_db,
    esp_gatt_if_t gatts_if,
    uint8_t max_nb_attr,
    uint8_t srvc_inst_id)
{
    allow_raw_hid_output_report(gatts_attr_db, max_nb_attr);
    return __real_esp_ble_gatts_create_attr_tab(
        gatts_attr_db, gatts_if, max_nb_attr, srvc_inst_id);
}
