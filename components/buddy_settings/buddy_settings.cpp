#include "buddy_settings.hpp"

#include <cstdint>

#include "nvs.h"

namespace buddy::settings {
namespace {

inline constexpr char Namespace[] = "buddy";
inline constexpr char MutedKey[] = "muted";

class NvsHandle final {
public:
    explicit NvsHandle(nvs_handle_t handle) noexcept : handle_{handle} {}
    ~NvsHandle() { nvs_close(handle_); }

    NvsHandle(const NvsHandle &) = delete;
    NvsHandle &operator=(const NvsHandle &) = delete;

    [[nodiscard]] nvs_handle_t get() const noexcept { return handle_; }

private:
    nvs_handle_t handle_;
};

}  // namespace

esp_err_t loadMuted(bool &muted) noexcept
{
    nvs_handle_t rawHandle;
    esp_err_t error = nvs_open(Namespace, NVS_READONLY, &rawHandle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    const NvsHandle handle{rawHandle};
    std::uint8_t stored = 0;
    error = nvs_get_u8(handle.get(), MutedKey, &stored);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error == ESP_OK) muted = stored != 0;
    return error;
}

esp_err_t saveMuted(bool muted) noexcept
{
    nvs_handle_t rawHandle;
    esp_err_t error = nvs_open(Namespace, NVS_READWRITE, &rawHandle);
    if (error != ESP_OK) return error;
    const NvsHandle handle{rawHandle};
    error = nvs_set_u8(handle.get(), MutedKey, muted ? 1U : 0U);
    if (error == ESP_OK) error = nvs_commit(handle.get());
    return error;
}

}  // namespace buddy::settings
