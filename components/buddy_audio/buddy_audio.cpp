#include "buddy_audio.hpp"

#include "buddy_settings.hpp"

namespace buddy::audio {

Initialization Controller::initialize() noexcept
{
    Initialization result;
    result.settingsError = settings::loadMuted(muted_);
    result.speakerError = platform::initializeSpeaker();
    speakerReady_ = result.speakerError == ESP_OK;
    return result;
}

esp_err_t Controller::setMuted(bool muted) noexcept
{
    muted_ = muted;
    return settings::saveMuted(muted_);
}

esp_err_t Controller::play(platform::Sound sound) noexcept
{
    if (muted_ || !speakerReady_) return ESP_OK;
    return platform::playSound(sound);
}

}  // namespace buddy::audio
