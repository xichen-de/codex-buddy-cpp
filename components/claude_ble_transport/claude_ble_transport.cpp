#include "claude_ble_transport.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if !CONFIG_BT_BLUEDROID_ENABLED || !CONFIG_BT_BLE_ENABLED
#error "Claude Buddy BLE transport requires Bluedroid BLE in sdkconfig"
#endif

namespace buddy::claude::transport {

constexpr std::uint16_t NusAppId = 0x43;
constexpr std::uint8_t NusServiceInstance = 0;
constexpr unsigned AdvertisingDataReady = 1U << 0;
constexpr unsigned AdvertisingScanReady = 1U << 1;
constexpr std::uint16_t NusMaxRx = 512;

enum class Attribute : std::size_t {
    Service,
    RxDeclaration,
    RxValue,
    TxDeclaration,
    TxValue,
    TxCccd,
    Count,
};

constexpr std::size_t attributeIndex(Attribute attribute) noexcept
{
    return static_cast<std::size_t>(attribute);
}

constexpr std::size_t AttributeCount = attributeIndex(Attribute::Count);

static const char *TAG = "claude_ble";
static const uint16_t s_primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t s_character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t s_cccd_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t s_rx_properties = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                       ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t s_tx_properties = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t s_cccd_initial[2] = {0, 0};
static const uint8_t s_nus_service_uuid[16] = {
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e,
};
static const uint8_t s_nus_rx_uuid[16] = {
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e,
};
static const uint8_t s_nus_tx_uuid[16] = {
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e,
};

static const std::array<esp_gatts_attr_db_t, AttributeCount> s_gatt_db{{
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(s_nus_service_uuid), sizeof(s_nus_service_uuid),
         (uint8_t *)s_nus_service_uuid}},
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
         ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
         (uint8_t *)&s_rx_properties}},
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)s_nus_rx_uuid,
         ESP_GATT_PERM_WRITE_ENCRYPTED, NusMaxRx, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
         ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
         (uint8_t *)&s_tx_properties}},
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)s_nus_tx_uuid,
         ESP_GATT_PERM_READ_ENCRYPTED, NusMaxRx, 0, nullptr}},
    {{ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&s_cccd_uuid,
         ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED,
         sizeof(s_cccd_initial), sizeof(s_cccd_initial),
         (uint8_t *)s_cccd_initial}},
}};

static esp_ble_adv_params_t s_advertising_params = [] {
    esp_ble_adv_params_t params{};
    /* BLE advertising intervals use 0.625 ms units: 250-500 ms here. */
    params.adv_int_min = 0x190;
    params.adv_int_max = 0x320;
    params.adv_type = ADV_TYPE_IND;
    params.own_addr_type = BLE_ADDR_TYPE_RANDOM;
    params.channel_map = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    return params;
}();

static Listener *s_listener;
static std::array<std::uint16_t, AttributeCount> s_handles{};
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_connection_id;
static uint16_t s_mtu = 23;
static unsigned s_advertising_ready;
static bool s_connected;
static bool s_secure;
static bool s_notifications_enabled;
static bool s_service_started;
static esp_bd_addr_t s_peer_address;
static bool s_peer_known;
static char s_device_name[24];

/* Forwards a connection edge to the runtime when a callback is installed. */
static void notify_connection(bool connected)
{
    if (s_listener != nullptr) s_listener->onConnectionChanged(connected);
}

/* Forwards an encryption/authentication edge to the runtime. */
static void notify_security(bool secure)
{
    if (s_listener != nullptr) s_listener->onSecurityChanged(secure);
}

/* Starts advertising after the service and both GAP payloads are ready. */
static void start_advertising_if_ready(void)
{
    if (s_service_started && !s_connected &&
        s_advertising_ready == (AdvertisingDataReady | AdvertisingScanReady)) {
        const esp_err_t error =
            esp_ble_gap_start_advertising(&s_advertising_params);
        if (error != ESP_OK)
            ESP_LOGE(TAG, "Could not start advertising: %s",
                     esp_err_to_name(error));
    }
}

/* Handles advertising, passkey display, and secure-bond completion events. */
static void gap_callback(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *parameters)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            s_advertising_ready |= AdvertisingDataReady;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            s_advertising_ready |= AdvertisingScanReady;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (parameters->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
                ESP_LOGI(TAG, "Advertising as %s", s_device_name);
            else
                ESP_LOGE(TAG, "Advertising failed: %u",
                         parameters->adv_start_cmpl.status);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(parameters->ble_security.ble_req.bd_addr,
                                     true);
            break;
        case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
            ESP_LOGI(TAG, "Pairing passkey %06" PRIu32,
                     parameters->ble_security.key_notif.passkey);
            if (s_listener != nullptr)
                s_listener->onPasskeyChanged(
                    true, parameters->ble_security.key_notif.passkey);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            s_secure = parameters->ble_security.auth_cmpl.success;
            if (s_listener != nullptr) s_listener->onPasskeyChanged(false, 0);
            notify_security(s_secure);
            if (s_secure) ESP_LOGI(TAG, "Secure bonding complete");
            else ESP_LOGW(TAG, "Bonding failed: 0x%x",
                          parameters->ble_security.auth_cmpl.fail_reason);
            break;
        default:
            break;
    }
}

/* Owns NUS service creation, connections, subscriptions, MTU, and RX writes. */
static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                           esp_ble_gatts_cb_param_t *parameters)
{
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            s_gatts_if = gatts_if;
            uint8_t base[6];
            if (esp_read_mac(base, ESP_MAC_BT) != ESP_OK) {
                ESP_LOGE(TAG, "Could not read Bluetooth MAC");
                return;
            }
            esp_bd_addr_t claude_address;
            memcpy(claude_address, base, sizeof(claude_address));
            claude_address[0] |= 0xc0U;
            claude_address[5] ^= 0xc1U;
            snprintf(s_device_name, sizeof(s_device_name), "Claude CoreS3-%02X%02X",
                     base[4], base[5]);
            esp_err_t error = esp_ble_gap_set_rand_addr(claude_address);
            if (error == ESP_OK)
                error = esp_ble_gap_set_device_name(s_device_name);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Claude BLE identity failed: %s",
                         esp_err_to_name(error));
                return;
            }
            esp_ble_adv_data_t advertising{};
            advertising.include_txpower = true;
            advertising.service_uuid_len = sizeof(s_nus_service_uuid);
            advertising.p_service_uuid =
                const_cast<uint8_t *>(s_nus_service_uuid);
            advertising.flag = ESP_BLE_ADV_FLAG_GEN_DISC |
                               ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
            esp_ble_adv_data_t scan_response{};
            scan_response.set_scan_rsp = true;
            scan_response.include_name = true;
            if (esp_ble_gap_config_adv_data(&advertising) != ESP_OK ||
                esp_ble_gap_config_adv_data(&scan_response) != ESP_OK ||
                esp_ble_gatts_create_attr_tab(s_gatt_db.data(), gatts_if,
                    AttributeCount, NusServiceInstance) != ESP_OK)
                ESP_LOGE(TAG, "Could not configure NUS service");
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (parameters->add_attr_tab.status != ESP_GATT_OK ||
                parameters->add_attr_tab.num_handle != AttributeCount) {
                ESP_LOGE(TAG, "NUS attribute table creation failed: 0x%x",
                         parameters->add_attr_tab.status);
                break;
            }
            std::copy_n(parameters->add_attr_tab.handles, AttributeCount,
                        s_handles.begin());
            if (esp_ble_gatts_start_service(
                    s_handles[attributeIndex(Attribute::Service)]) != ESP_OK)
                ESP_LOGE(TAG, "Could not start NUS service");
            break;
        case ESP_GATTS_START_EVT:
            s_service_started = parameters->start.status == ESP_GATT_OK;
            start_advertising_if_ready();
            break;
        case ESP_GATTS_CONNECT_EVT:
            s_connected = true;
            s_secure = false;
            s_notifications_enabled = false;
            s_connection_id = parameters->connect.conn_id;
            s_peer_known = true;
            memcpy(s_peer_address, parameters->connect.remote_bda,
                   sizeof(s_peer_address));
            {
                esp_ble_conn_update_params_t connection{};
                memcpy(connection.bda, parameters->connect.remote_bda,
                       sizeof(connection.bda));
                connection.min_int = 0x18;
                connection.max_int = 0x28;
                connection.latency = 0;
                connection.timeout = 400;
                const esp_err_t interval_error =
                    esp_ble_gap_update_conn_params(&connection);
                if (interval_error != ESP_OK)
                    ESP_LOGW(TAG, "Connection interval request failed: %s",
                             esp_err_to_name(interval_error));
            }
            notify_connection(true);
            esp_ble_set_encryption(parameters->connect.remote_bda,
                                   ESP_BLE_SEC_ENCRYPT_MITM);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            s_connected = false;
            s_secure = false;
            s_notifications_enabled = false;
            s_mtu = 23;
            notify_security(false);
            notify_connection(false);
            start_advertising_if_ready();
            break;
        case ESP_GATTS_MTU_EVT:
            s_mtu = parameters->mtu.mtu;
            break;
        case ESP_GATTS_WRITE_EVT:
            if (parameters->write.handle ==
                    s_handles[attributeIndex(Attribute::TxCccd)] &&
                parameters->write.len == 2) {
                const std::uint16_t configuration =
                    static_cast<std::uint16_t>(parameters->write.value[0]) |
                    (static_cast<std::uint16_t>(parameters->write.value[1]) << 8);
                s_notifications_enabled = configuration == 1;
            } else if (parameters->write.handle ==
                           s_handles[attributeIndex(Attribute::RxValue)] &&
                       !parameters->write.is_prep && s_listener != nullptr) {
                s_listener->onData(
                    {parameters->write.value, parameters->write.len});
            }
            if (parameters->write.need_rsp)
                esp_ble_gatts_send_response(gatts_if,
                    parameters->write.conn_id, parameters->write.trans_id,
                    ESP_GATT_OK, nullptr);
            break;
        default:
            break;
    }
}

/* Initializes the Claude-specific Bluedroid store, secure GAP, and NUS app. */
esp_err_t initialize(Listener &listener) noexcept
{
    if (s_gatts_if != ESP_GATT_IF_NONE) return ESP_ERR_INVALID_STATE;
    s_listener = &listener;
    esp_err_t error = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((error = esp_bt_controller_init(&controller)) != ESP_OK ||
        (error = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK ||
        /* Keep Claude's secure keys separate from Codex's existing default
           Bluedroid bond store. Both modes pair to the same Mac peer. */
        (error = esp_bt_config_file_path_update("claude_bt")) != ESP_OK ||
        (error = esp_bluedroid_init()) != ESP_OK ||
        (error = esp_bluedroid_enable()) != ESP_OK ||
        (error = esp_ble_gap_register_callback(gap_callback)) != ESP_OK ||
        (error = esp_ble_gatts_register_callback(gatts_callback)) != ESP_OK ||
        (error = esp_ble_gatts_app_register(NusAppId)) != ESP_OK)
        return error;

    esp_ble_auth_req_t authentication = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t io_capability = ESP_IO_CAP_OUT;
    uint8_t key_size = 16;
    uint8_t key_mask = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    if ((error = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
             &authentication, sizeof(authentication))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,
             &io_capability, sizeof(io_capability))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,
             &key_size, sizeof(key_size))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,
             &key_mask, sizeof(key_mask))) != ESP_OK ||
        (error = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,
             &key_mask, sizeof(key_mask))) != ESP_OK)
        return error;
    return ESP_OK;
}

/* Treats the transport as usable only after the client enables notifications. */
bool connected() noexcept
{
    return s_connected && s_notifications_enabled;
}

/* Reports authenticated encryption independently from notification readiness. */
bool secure() noexcept
{
    return s_connected && s_secure;
}

/* MTU-fragments JSON, appends one newline, and sends ordered notifications. */
esp_err_t sendLine(std::string_view json) noexcept
{
    if (json.empty()) return ESP_ERR_INVALID_ARG;
    if (!connected() || s_gatts_if == ESP_GATT_IF_NONE)
        return ESP_ERR_INVALID_STATE;
    size_t packet_size = s_mtu > 3 ? s_mtu - 3U : 20U;
    if (packet_size > 511U) packet_size = 511U;
    size_t offset = 0;
    while (offset <= json.size()) {
        uint8_t packet[512];
        size_t chunk = json.size() - offset;
        const bool append_newline = chunk < packet_size;
        if (chunk > packet_size) chunk = packet_size;
        memcpy(packet, json.data() + offset, chunk);
        if (append_newline) packet[chunk++] = '\n';
        const esp_err_t error = esp_ble_gatts_send_indicate(
            s_gatts_if, s_connection_id,
            s_handles[attributeIndex(Attribute::TxValue)],
            static_cast<std::uint16_t>(chunk), packet, false);
        if (error != ESP_OK) return error;
        offset += append_newline ? json.size() - offset : chunk;
        if (append_newline) break;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    return ESP_OK;
}

/* Removes the remembered active peer from Bluedroid's Claude bond store. */
esp_err_t forgetCurrentPeer() noexcept
{
    if (!s_peer_known) return ESP_ERR_NOT_FOUND;
    return esp_ble_remove_bond_device(s_peer_address);
}

}  // namespace buddy::claude::transport
