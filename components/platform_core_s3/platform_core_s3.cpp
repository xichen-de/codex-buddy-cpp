#include "platform_core_s3.hpp"

#include <cstddef>

#include "bsp/m5stack_core_s3.h"
#include "bsp/touch.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "driver/i2c_master.h"
#include "bmi270.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace buddy::platform {

static const char *TAG = "platform_core_s3";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_transfer_done;
static uint16_t *s_framebuffer;
static bool s_touch_active;
static uint16_t s_touch_x;
static uint16_t s_touch_y;
static esp_codec_dev_handle_t s_speaker;
static int16_t s_tone_samples[2646];
static i2c_master_dev_handle_t s_rtc;
static bmi270_handle_t *s_imu;

/* Releases the waiting presenter when the asynchronous LCD DMA transfer ends. */
static bool transfer_complete(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *event_data,
                              void *user_context)
{
    (void)panel_io;
    (void)event_data;
    (void)user_context;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_transfer_done, &task_woken);
    return task_woken == pdTRUE;
}

/* Brings up the BSP display/touch stack and allocates the shared PSRAM frame. */
esp_err_t initialize() noexcept
{
    if (s_panel != nullptr) return ESP_ERR_INVALID_STATE;
    if (BSP_LCD_H_RES != Width || BSP_LCD_V_RES != Height) {
        ESP_LOGE(TAG, "BSP resolution %dx%d does not match UI %dx%d",
                 BSP_LCD_H_RES, BSP_LCD_V_RES, Width, Height);
        return ESP_ERR_INVALID_SIZE;
    }

    s_framebuffer = static_cast<uint16_t *>(heap_caps_malloc(
        PixelCount * sizeof(*s_framebuffer),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_framebuffer == nullptr) {
        ESP_LOGE(TAG, "Could not allocate %u-byte PSRAM framebuffer",
                 static_cast<unsigned>(PixelCount * sizeof(*s_framebuffer)));
        return ESP_ERR_NO_MEM;
    }
    s_transfer_done = xSemaphoreCreateBinary();
    if (s_transfer_done == nullptr) return ESP_ERR_NO_MEM;

    const bsp_display_config_t display_config = {
        .max_transfer_sz = PixelCount * sizeof(*s_framebuffer),
    };
    esp_err_t error = bsp_display_new(
        &display_config, &s_panel, &s_panel_io);
    if (error != ESP_OK) return error;
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = transfer_complete,
    };
    if ((error = esp_lcd_panel_io_register_event_callbacks(
             s_panel_io, &callbacks, nullptr)) != ESP_OK ||
        (error = bsp_display_brightness_init()) != ESP_OK ||
        (error = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK ||
        (error = bsp_display_backlight_on()) != ESP_OK ||
        (error = bsp_touch_new(nullptr, &s_touch)) != ESP_OK) {
        ESP_LOGE(TAG, "Display/touch initialization failed: %s",
                 esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "CoreS3 LCD and touch initialized");
    return ESP_OK;
}

/* Returns the single framebuffer owned by this board component. */
std::span<std::uint16_t> framebuffer() noexcept
{
    return s_framebuffer == nullptr
        ? std::span<std::uint16_t>{}
        : std::span<std::uint16_t>{s_framebuffer, PixelCount};
}

/* Byte-swaps RGB565 in place, submits LCD DMA, waits, then restores byte order. */
esp_err_t present() noexcept
{
    if (s_panel == nullptr || s_framebuffer == nullptr || s_transfer_done == nullptr)
        return ESP_ERR_INVALID_STATE;

#if BSP_LCD_BIGENDIAN
    /* esp_lcd transmits memory byte order; the ILI9342C expects MSB first. */
    for (size_t index = 0; index < PixelCount; ++index)
        s_framebuffer[index] = __builtin_bswap16(s_framebuffer[index]);
#endif
    while (xSemaphoreTake(s_transfer_done, 0) == pdTRUE) {
    }
    const esp_err_t error = esp_lcd_panel_draw_bitmap(
        s_panel, 0, 0, Width, Height, s_framebuffer);
    if (error != ESP_OK) return error;
    if (xSemaphoreTake(s_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "LCD transfer timed out");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* Coordinates panel sleep/display commands with the CoreS3 backlight. */
esp_err_t setDisplayAwake(bool awake) noexcept
{
    if (s_panel == nullptr) return ESP_ERR_INVALID_STATE;
    esp_err_t error;
    if (awake) {
        error = esp_lcd_panel_disp_on_off(s_panel, true);
        if (error == ESP_OK) error = bsp_display_backlight_on();
    } else {
        error = bsp_display_backlight_off();
        if (error == ESP_OK) error = esp_lcd_panel_disp_on_off(s_panel, false);
    }
    return error;
}

/* Converts BSP touch samples into stable press, move, and release edges. */
esp_err_t pollTouch(TouchEvent &event) noexcept
{
    if (s_touch == nullptr) return ESP_ERR_INVALID_STATE;
    event = TouchEvent{};
    esp_err_t error = esp_lcd_touch_read_data(s_touch);
    if (error != ESP_OK) return error;

    esp_lcd_touch_point_data_t point;
    uint8_t count = 0;
    error = esp_lcd_touch_get_data(s_touch, &point, &count, 1);
    if (error != ESP_OK) return error;
    const bool touched = count > 0 && point.x < Width && point.y < Height;
    if (touched) {
        event.x = point.x;
        event.y = point.y;
        event.type = s_touch_active ? TouchType::Moved : TouchType::Pressed;
        s_touch_active = true;
        s_touch_x = point.x;
        s_touch_y = point.y;
    } else if (s_touch_active) {
        event.type = TouchType::Released;
        event.x = s_touch_x;
        event.y = s_touch_y;
        s_touch_active = false;
    }
    return ESP_OK;
}

/* Explicitly reports unknown telemetry because the supported BSP lacks it. */
std::optional<BatteryStatus> battery() noexcept
{
    return std::nullopt;
}

/* Acquires the BSP codec speaker and enables its output path. */
esp_err_t initializeSpeaker() noexcept
{
    if (s_speaker != nullptr) return ESP_OK;
    s_speaker = bsp_audio_codec_speaker_init();
    if (s_speaker == nullptr) return ESP_FAIL;
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = 22050,
        .mclk_multiple = 0,
    };
    int error = esp_codec_dev_open(s_speaker, &format);
    if (error != ESP_CODEC_DEV_OK) return error;
    error = esp_codec_dev_set_out_vol(s_speaker, 38);
    if (error != ESP_CODEC_DEV_OK) return error;
    ESP_LOGI(TAG, "CoreS3 speaker initialized");
    return ESP_OK;
}

/* Synthesizes one square-wave tone and writes it to the speaker codec. */
static esp_err_t play_tone(unsigned frequency, unsigned duration_ms)
{
    if (s_speaker == nullptr || frequency == 0) return ESP_ERR_INVALID_STATE;
    size_t samples = 22050U * duration_ms / 1000U;
    if (samples > sizeof(s_tone_samples) / sizeof(s_tone_samples[0]))
        samples = sizeof(s_tone_samples) / sizeof(s_tone_samples[0]);
    const unsigned half_period = 22050U / frequency / 2U;
    if (half_period == 0) return ESP_ERR_INVALID_ARG;
    for (size_t index = 0; index < samples; ++index) {
        int amplitude = 4200;
        if (index < 100)
            amplitude = amplitude * static_cast<int>(index) / 100;
        if (samples - index < 100)
            amplitude = amplitude * static_cast<int>(samples - index) / 100;
        s_tone_samples[index] = ((index / half_period) & 1U)
            ? static_cast<std::int16_t>(amplitude)
            : static_cast<std::int16_t>(-amplitude);
    }
    const int error = esp_codec_dev_write(
        s_speaker, s_tone_samples,
        static_cast<int>(samples * sizeof(s_tone_samples[0])));
    return error == ESP_CODEC_DEV_OK ? ESP_OK : error;
}

/* Expands a semantic sound cue into its short fixed sequence of tones. */
esp_err_t playSound(Sound sound) noexcept
{
    if (s_speaker == nullptr) return ESP_ERR_INVALID_STATE;
    switch (sound) {
        case Sound::Connect:
            if (play_tone(660, 55) != ESP_OK) return ESP_FAIL;
            return play_tone(880, 75);
        case Sound::Attention:
            if (play_tone(740, 80) != ESP_OK) return ESP_FAIL;
            return play_tone(740, 80);
        case Sound::Approve:
            if (play_tone(880, 55) != ESP_OK) return ESP_FAIL;
            return play_tone(1175, 80);
        case Sound::Deny:
            return play_tone(260, 110);
        case Sound::Complete:
            if (play_tone(523, 45) != ESP_OK) return ESP_FAIL;
            if (play_tone(659, 45) != ESP_OK) return ESP_FAIL;
            return play_tone(784, 90);
    }
    return ESP_ERR_INVALID_ARG;
}

/* Converts one packed-BCD RTC byte to a binary integer. */
static uint8_t from_bcd(uint8_t value)
{
    return static_cast<std::uint8_t>((value >> 4) * 10U + (value & 0x0fU));
}

/* Converts a binary integer below 100 to one packed-BCD byte. */
static uint8_t to_bcd(unsigned value)
{
    return static_cast<std::uint8_t>(
        ((value / 10U) << 4) | (value % 10U));
}

/* Converts a Gregorian date to signed days relative to the Unix epoch. */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned march_month = month > 2 ? month - 3U : month + 9U;
    const unsigned doy = (153U * march_month
                          + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return static_cast<std::int64_t>(era) * 146097 +
           static_cast<std::int64_t>(doe) - 719468;
}

/* Converts signed Unix-epoch days back into Gregorian date fields. */
static void civil_from_days(int64_t days, int *year, unsigned *month,
                            unsigned *day)
{
    int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp < 10 ? mp + 3 : mp - 9;
    y += *month <= 2;
    *year = y;
}

/* Initializes the BSP I2C bus required by the BM8563 RTC. */
esp_err_t initializeRtc() noexcept
{
    if (s_rtc != nullptr) return ESP_OK;
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t error = i2c_master_get_bus_handle(BSP_I2C_NUM, &bus);
    if (error != ESP_OK) return error;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x51;
    config.scl_speed_hz = 400000;
    error = i2c_master_bus_add_device(bus, &config, &s_rtc);
    if (error == ESP_OK) ESP_LOGI(TAG, "BM8563 RTC initialized");
    return error;
}

/* Reads and validates RTC registers before converting them to local seconds. */
esp_err_t readRtc(std::int64_t &localSeconds) noexcept
{
    if (s_rtc == nullptr) return ESP_ERR_INVALID_STATE;
    const uint8_t start = 0x02;
    uint8_t data[7];
    esp_err_t error = i2c_master_transmit_receive(
        s_rtc, &start, sizeof(start), data, sizeof(data), 1000);
    if (error != ESP_OK) return error;
    if ((data[0] & 0x80U) != 0) return ESP_ERR_INVALID_STATE;
    const unsigned second = from_bcd(data[0] & 0x7fU);
    const unsigned minute = from_bcd(data[1] & 0x7fU);
    const unsigned hour = from_bcd(data[2] & 0x3fU);
    const unsigned day = from_bcd(data[3] & 0x3fU);
    const unsigned month = from_bcd(data[5] & 0x1fU);
    const int year = 2000 + from_bcd(data[6]);
    if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 ||
        month < 1 || month > 12) return ESP_ERR_INVALID_RESPONSE;
    localSeconds = days_from_civil(year, month, day) * 86400 +
        static_cast<std::int64_t>(hour) * 3600 +
        static_cast<std::int64_t>(minute) * 60 + second;
    return ESP_OK;
}

/* Converts local seconds to calendar/BCD fields and writes the RTC registers. */
esp_err_t writeRtc(std::int64_t localSeconds) noexcept
{
    if (s_rtc == nullptr || localSeconds < 0) return ESP_ERR_INVALID_ARG;
    const int64_t days = localSeconds / 86400;
    const unsigned seconds = static_cast<unsigned>(localSeconds % 86400);
    int year;
    unsigned month;
    unsigned day;
    civil_from_days(days, &year, &month, &day);
    if (year < 2000 || year > 2099) return ESP_ERR_INVALID_ARG;
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0) weekday += 7;
    const uint8_t data[] = {
        0x00, 0x00, 0x00,
        to_bcd(seconds % 60U),
        to_bcd(seconds / 60U % 60U),
        to_bcd(seconds / 3600U),
        to_bcd(day),
        to_bcd(static_cast<unsigned>(weekday)),
        to_bcd(month),
        to_bcd(static_cast<unsigned>(year - 2000)),
    };
    return i2c_master_transmit(s_rtc, data, sizeof(data), 1000);
}

/* Initializes the BMI270 and configures accelerometer range and output rate. */
esp_err_t initializeImu() noexcept
{
    if (s_imu != nullptr) return ESP_OK;
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t error = i2c_master_get_bus_handle(BSP_I2C_NUM, &bus);
    if (error != ESP_OK) return error;
    const bmi270_driver_config_t driver = {
        .addr = BMI270_I2C_ADDRESS_H,
        .interface = BMI270_USE_I2C,
        .i2c_bus = bus,
    };
    error = bmi270_create(&driver, &s_imu);
    if (error != ESP_OK) return error;
    const bmi270_config_t measurement = {
        .acce_odr = BMI270_ACC_ODR_50_HZ,
        .acce_range = BMI270_ACC_RANGE_4_G,
        .gyro_odr = BMI270_GYR_ODR_50_HZ,
        .gyro_range = BMI270_GYR_RANGE_500_DPS,
    };
    error = bmi270_start(s_imu, &measurement);
    if (error != ESP_OK) {
        bmi270_delete(s_imu);
        s_imu = nullptr;
        return error;
    }
    ESP_LOGI(TAG, "BMI270 IMU initialized at address 0x69");
    return ESP_OK;
}

/* Reads raw BMI270 acceleration and scales axes into g units. */
esp_err_t readImu(MotionSample &sample) noexcept
{
    if (s_imu == nullptr) return ESP_ERR_INVALID_STATE;
    return bmi270_get_acce_data(s_imu, &sample.x_g, &sample.y_g,
                                &sample.z_g);
}

}  // namespace buddy::platform
