#include "buddy_audio.hpp"

#include "buddy_settings.hpp"

namespace buddy::audio {

Initialization Controller::initialize() noexcept
{
    Initialization result;
    result.settingsError = settings::loadMuted(muted_);
    /* The codec is opened only for the short duration of a cue. */
    speakerReady_ = true;
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
    esp_err_t error = platform::initializeSpeaker();
    if (error == ESP_OK) error = platform::playSound(sound);
    const esp_err_t shutdownError = platform::shutdownSpeaker();
    return error == ESP_OK ? shutdownError : error;
}

}  // namespace buddy::audio
