#include "buddy_ui.hpp"
#include "claude_runtime.hpp"
#include "codex_runtime.hpp"
#include "display_power_policy.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform_core_s3.hpp"
#include "runtime_clock.hpp"

namespace {

constexpr char Tag[] = "codex_buddy";

esp_err_t initializeNvs() noexcept
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(Tag, "NVS requires reinitialization");
        error = nvs_flash_erase();
        if (error == ESP_OK) error = nvs_flash_init();
    }
    return error;
}

esp_err_t initializePowerManagement() noexcept
{
    const esp_pm_config_t config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    return esp_pm_configure(&config);
}

buddy::display::Mode chooseMode() noexcept
{
    buddy::display::renderSelector(buddy::platform::framebuffer());
    ESP_ERROR_CHECK(buddy::platform::present());
    buddy::runtime::DisplayPowerPolicy powerPolicy;
    auto displayPower = buddy::runtime::DisplayPowerLevel::Normal;
    powerPolicy.recordInteraction(buddy::runtime::uptimeMs());
    for (;;) {
        buddy::platform::TouchEvent touch;
        const esp_err_t touchError = buddy::platform::pollTouch(touch);
        if (touchError != ESP_OK) {
            ESP_LOGW(Tag, "Mode selector touch read failed: %s",
                     esp_err_to_name(touchError));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (touch.type == buddy::platform::TouchType::Pressed) {
            const std::uint32_t now = buddy::runtime::uptimeMs();
            powerPolicy.recordInteraction(now);
            if (displayPower != buddy::runtime::DisplayPowerLevel::Normal) {
                ESP_ERROR_CHECK(buddy::platform::setDisplayPower(
                    buddy::platform::DisplayPower::Normal));
                displayPower = buddy::runtime::DisplayPowerLevel::Normal;
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }
            const auto mode = buddy::display::selectorHit(touch.x, touch.y);
            if (mode != buddy::display::Mode::None) return mode;
        }
        const auto desired = powerPolicy.desired(
            buddy::runtime::uptimeMs(), false);
        if (desired != displayPower) {
            const auto platformPower = desired == buddy::runtime::DisplayPowerLevel::Normal
                ? buddy::platform::DisplayPower::Normal
                : desired == buddy::runtime::DisplayPowerLevel::Dimmed
                    ? buddy::platform::DisplayPower::Dimmed
                    : buddy::platform::DisplayPower::Off;
            ESP_ERROR_CHECK(buddy::platform::setDisplayPower(platformPower));
            displayPower = desired;
        }
        vTaskDelay(pdMS_TO_TICKS(
            displayPower == buddy::runtime::DisplayPowerLevel::Off ? 250 : 40));
    }
}

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(Tag, "Codex Buddy dual-mode firmware starting");
    ESP_ERROR_CHECK(initializeNvs());
    ESP_ERROR_CHECK(initializePowerManagement());
    ESP_ERROR_CHECK(buddy::platform::initialize());

    if (chooseMode() == buddy::display::Mode::Claude) {
        static buddy::runtime::ClaudeRuntime runtime;
        runtime.run();
    } else {
        static buddy::runtime::CodexRuntime runtime;
        runtime.run();
    }
}
