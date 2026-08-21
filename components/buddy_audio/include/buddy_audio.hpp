#pragma once

#include "esp_err.h"
#include "platform_core_s3.hpp"

namespace buddy::audio {

struct Initialization {
    esp_err_t settingsError{ESP_OK};
    esp_err_t speakerError{ESP_OK};
};

/* Owns the device-wide mute preference and speaker availability. */
class Controller final {
public:
    [[nodiscard]] Initialization initialize() noexcept;

    [[nodiscard]] bool muted() const noexcept { return muted_; }
    [[nodiscard]] bool ready() const noexcept { return speakerReady_; }

    /* Updates the in-memory preference and attempts to persist it. */
    [[nodiscard]] esp_err_t setMuted(bool muted) noexcept;

    /* Plays a semantic cue, or succeeds silently while muted/unavailable. */
    [[nodiscard]] esp_err_t play(platform::Sound sound) noexcept;

private:
    bool muted_{};
    bool speakerReady_{};
};

}  // namespace buddy::audio
