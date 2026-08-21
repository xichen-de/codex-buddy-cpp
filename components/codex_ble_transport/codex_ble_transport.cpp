#include "codex_ble_transport.hpp"

#include <cstring>
#include <array>
#include <string_view>

#include "codex_protocol.hpp"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if !CONFIG_BT_BLUEDROID_ENABLED || !CONFIG_BT_BLE_ENABLED
#error "Codex Buddy BLE transport requires Bluedroid BLE in sdkconfig"
#endif

namespace buddy::codex::transport {

constexpr unsigned AdvertisingDataReady = 1U << 0;
constexpr unsigned ScanResponseReady = 1U << 1;

static const char *TAG = "codex_ble_transport";
static esp_hidd_dev_t *s_hid_device;
static Listener *s_listener;
static bool s_hid_started;
static bool s_connected;
static unsigned s_advertising_ready;

static esp_hid_raw_report_map_t s_report_maps[] = {{
    .data = buddy::codex::HidReportMap.data(),
    .len = 29,
}};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = buddy::codex::VendorId,
    .product_id = buddy::codex::ProductId,
    .version = 0x0101,
    .device_name = "Codex Micro",
    .manufacturer_name = "Work Louder",
    .serial_number = "CodexBuddy",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

static esp_ble_adv_params_t s_advertising_params = [] {
    esp_ble_adv_params_t params{};
    /* BLE advertising intervals use 0.625 ms units: 250-500 ms here. */
    params.adv_int_min = 0x190;
    params.adv_int_max = 0x320;
    params.adv_type = ADV_TYPE_IND;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.channel_map = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    return params;
}();

/* Starts discoverable advertising only after HID and both GAP payloads are ready. */
static void start_advertising_if_ready(void)
{
    if (s_hid_started && !s_connected &&
        s_advertising_ready == (AdvertisingDataReady | ScanResponseReady)) {
        const esp_err_t error = esp_ble_gap_start_advertising(&s_advertising_params);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Could not start advertising: %s", esp_err_to_name(error));
        }
    }
}

/* Handles advertising configuration and legacy no-I/O bonding events. */
static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *parameters)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            s_advertising_ready |= AdvertisingDataReady;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            s_advertising_ready |= ScanResponseReady;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (parameters->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising as %s", s_hid_config.device_name);
            } else {
                ESP_LOGE(TAG, "Advertising start failed: %u",
                         parameters->adv_start_cmpl.status);
            }
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(parameters->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (parameters->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "Bonding complete");
            } else {
                ESP_LOGW(TAG, "Bonding failed: 0x%x",
                         parameters->ble_security.auth_cmpl.fail_reason);
            }
            break;
        default:
            break;
    }
}

/* Converts ESP HID lifecycle/output events into transport state and callbacks. */
static void hid_callback(void *handler_args, esp_event_base_t base,
                         int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    auto *data = static_cast<esp_hidd_event_data_t *>(event_data);
    switch ((esp_hidd_event_t)event_id) {
        case ESP_HIDD_START_EVENT:
            s_hid_started = true;
            start_advertising_if_ready();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            s_connected = true;
            ESP_LOGI(TAG, "Host connected");
            if (s_listener != nullptr) s_listener->onConnectionChanged(true);
            break;
        case ESP_HIDD_OUTPUT_EVENT:
            if (data != nullptr && data->output.map_index == 0 &&
                data->output.report_id == buddy::codex::ReportId &&
                s_listener != nullptr) {
                s_listener->onReport(
                    {data->output.data, data->output.length});
            }
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            s_connected = false;
            ESP_LOGI(TAG, "Host disconnected (reason=%d)",
                     data == nullptr ? -1 : data->disconnect.reason);
            if (s_listener != nullptr) s_listener->onConnectionChanged(false);
            start_advertising_if_ready();
            break;
        default:
            break;
    }
}

/* Registers GAP behavior, security parameters, name, and advertising payloads. */
static esp_err_t configure_gap(void)
{
    static uint8_t hid_service_uuid[] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    esp_ble_adv_data_t advertising{};
    advertising.include_txpower = true;
    advertising.min_interval = 0x0006;
    advertising.max_interval = 0x0012;
    advertising.appearance = ESP_HID_APPEARANCE_GENERIC;
    advertising.service_uuid_len = sizeof(hid_service_uuid);
    advertising.p_service_uuid = hid_service_uuid;
    advertising.flag = ESP_BLE_ADV_FLAG_GEN_DISC |
                       ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    esp_ble_adv_data_t scan_response{};
    scan_response.set_scan_rsp = true;
    scan_response.include_name = true;
    esp_ble_auth_req_t authentication = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io_capability = ESP_IO_CAP_NONE;
    uint8_t key_mask = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t key_size = 16;

    esp_err_t error = esp_ble_gap_register_callback(gap_callback);
    if (error != ESP_OK) return error;
    if ((error = esp_ble_gap_set_security_param(
             ESP_BLE_SM_AUTHEN_REQ_MODE, &authentication,
             sizeof(authentication))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(
             ESP_BLE_SM_IOCAP_MODE, &io_capability,
             sizeof(io_capability))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(
             ESP_BLE_SM_SET_INIT_KEY, &key_mask, sizeof(key_mask))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(
             ESP_BLE_SM_SET_RSP_KEY, &key_mask, sizeof(key_mask))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(
             ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size))) != ESP_OK ||
        (error = esp_ble_gap_set_device_name(s_hid_config.device_name)) != ESP_OK ||
        (error = esp_ble_gap_config_adv_data(&advertising)) != ESP_OK ||
        (error = esp_ble_gap_config_adv_data(&scan_response)) != ESP_OK) {
        return error;
    }
    return ESP_OK;
}

/* Initializes Bluedroid BLE, GAP, and the reference-compatible HID device. */
esp_err_t initialize(Listener &listener) noexcept
{
    if (s_hid_device != nullptr) return ESP_ERR_INVALID_STATE;
    s_listener = &listener;
    s_report_maps[0].len =
        static_cast<std::uint16_t>(buddy::codex::HidReportMap.size());

    esp_err_t error = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((error = esp_bt_controller_init(&controller)) != ESP_OK ||
        (error = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK ||
        (error = esp_bluedroid_init()) != ESP_OK ||
        (error = esp_bluedroid_enable()) != ESP_OK ||
        (error = configure_gap()) != ESP_OK ||
        (error = esp_ble_gatts_register_callback(
             esp_hidd_gatts_event_handler)) != ESP_OK ||
        (error = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE,
                                   hid_callback, &s_hid_device)) != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    return ESP_OK;
}

/* Combines callback state with ESP-IDF's HID connection state. */
bool connected() noexcept
{
    return s_connected && s_hid_device != nullptr &&
           esp_hidd_dev_connected(s_hid_device);
}

/* Encodes and sends HID fragments in order with a short host-friendly gap. */
esp_err_t sendJson(std::string_view json) noexcept
{
    if (json.empty()) return ESP_ERR_INVALID_ARG;
    if (!connected()) return ESP_ERR_INVALID_STATE;
    std::array<buddy::codex::Report, 8> reports{};
    std::size_t report_count{};
    const buddy::codex::Result encoded = buddy::codex::encodeReports(
        json, reports, report_count);
    if (encoded != buddy::codex::Result::Ok) return ESP_ERR_INVALID_SIZE;
    for (size_t i = 0; i < report_count; ++i) {
        const esp_err_t error = esp_hidd_dev_input_set(
            s_hid_device, 0, buddy::codex::ReportId, reports[i].data(),
            buddy::codex::ReportBodySize);
        if (error != ESP_OK) return error;
        if (i + 1 < report_count) vTaskDelay(pdMS_TO_TICKS(4));
    }
    return ESP_OK;
}

/* Validates and forwards battery percentage to the HID battery service. */
esp_err_t setBattery(std::uint8_t percent) noexcept
{
    if (s_hid_device == nullptr || percent > 100) return ESP_ERR_INVALID_ARG;
    return esp_hidd_dev_battery_set(s_hid_device, percent);
}

}  // namespace buddy::codex::transport
