#include "claude_storage.hpp"

#include <cstddef>

#include "nvs.h"

namespace buddy::claude::storage {
namespace {

inline constexpr char Namespace[] = "claude";

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

/* Loads an optional NVS string while treating a missing key as a default. */
esp_err_t loadString(nvs_handle_t handle, const char *key,
                     char *destination, size_t capacity)
{
    size_t length = capacity;
    const esp_err_t error = nvs_get_str(handle, key, destination, &length);
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
}

}  // namespace

/* Restores durable counters and identity fields into an initialized model. */
esp_err_t load(Model &model) noexcept
{
    nvs_handle_t rawHandle;
    esp_err_t error = nvs_open(Namespace, NVS_READONLY, &rawHandle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    const NvsHandle handle{rawHandle};
    error = loadString(handle.get(), "name", model.deviceName.data(),
                       model.deviceName.size());
    if (error == ESP_OK)
        error = loadString(handle.get(), "owner", model.owner.data(),
                           model.owner.size());
    if (error == ESP_OK) {
        error = nvs_get_u32(handle.get(), "approvals", &model.approvals);
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    }
    if (error == ESP_OK) {
        error = nvs_get_u32(handle.get(), "denials", &model.denials);
        if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    }
    return error;
}

/* Writes all durable Claude fields and commits them as one NVS transaction. */
esp_err_t save(const Model &model) noexcept
{
    nvs_handle_t rawHandle;
    esp_err_t error = nvs_open(Namespace, NVS_READWRITE, &rawHandle);
    if (error != ESP_OK) return error;
    const NvsHandle handle{rawHandle};
    if ((error = nvs_set_str(handle.get(), "name", model.deviceName.data())) == ESP_OK &&
        (error = nvs_set_str(handle.get(), "owner", model.owner.data())) == ESP_OK &&
        (error = nvs_set_u32(handle.get(), "approvals", model.approvals)) == ESP_OK &&
        (error = nvs_set_u32(handle.get(), "denials", model.denials)) == ESP_OK)
        error = nvs_commit(handle.get());
    return error;
}

}  // namespace buddy::claude::storage
