#include "claude_runtime.hpp"

#include <algorithm>
#include <string_view>

#include "buddy_ui.hpp"
#include "claude_storage.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime_clock.hpp"

namespace buddy::runtime {
namespace {

constexpr char Tag[] = "claude_runtime";

platform::DisplayPower platformPower(DisplayPowerLevel level) noexcept
{
    switch (level) {
        case DisplayPowerLevel::Normal: return platform::DisplayPower::Normal;
        case DisplayPowerLevel::Dimmed: return platform::DisplayPower::Dimmed;
        case DisplayPowerLevel::Off: return platform::DisplayPower::Off;
    }
    return platform::DisplayPower::Normal;
}

}  // namespace

void ClaudeRuntime::render() noexcept
{
    if (displayPower_ == platform::DisplayPower::Off) return;
    const auto battery = platform::battery();
    display::renderClaude(model_, uptimeMs(), passkeyVisible_, passkey_,
                          battery.has_value(), battery ? battery->percent : 0,
                          battery && battery->charging,
                          platform::framebuffer(), audio_.muted());
    const esp_err_t error = platform::present();
    if (error != ESP_OK)
        ESP_LOGE(Tag, "Claude display update failed: %s",
                 esp_err_to_name(error));
}

void ClaudeRuntime::playSound(platform::Sound sound) noexcept
{
    const esp_err_t error = audio_.play(sound);
    if (error != ESP_OK)
        ESP_LOGW(Tag, "Claude sound failed: %s", esp_err_to_name(error));
}

void ClaudeRuntime::wakeDisplay(std::uint32_t nowMs) noexcept
{
    displayPowerPolicy_.recordInteraction(nowMs);
    if (model_.faceDown ||
        displayPower_ == platform::DisplayPower::Normal) return;
    const esp_err_t error =
        platform::setDisplayPower(platform::DisplayPower::Normal);
    if (error != ESP_OK)
        ESP_LOGW(Tag, "Claude display could not wake: %s",
                 esp_err_to_name(error));
    else
        displayPower_ = platform::DisplayPower::Normal;
}

bool ClaudeRuntime::processMotion(std::uint32_t nowMs) noexcept
{
    if (!imuReady_ || nowMs - lastMotionMs_ < MotionPollMs) return false;
    lastMotionMs_ = nowMs;
    platform::MotionSample sample;
    const esp_err_t error = platform::readImu(sample);
    if (error != ESP_OK) {
        ESP_LOGW(Tag, "Claude motion read failed: %s", esp_err_to_name(error));
        return false;
    }
    const motion::Events events = motion::update(
        motion_, sample.x_g, sample.y_g, sample.z_g, nowMs);
    if (events.faceDown) {
        claude::setFaceDown(model_, true);
        if (displayPower_ != platform::DisplayPower::Off) {
            const esp_err_t displayError =
                platform::setDisplayPower(platform::DisplayPower::Off);
            if (displayError == ESP_OK)
                displayPower_ = platform::DisplayPower::Off;
            else
                ESP_LOGW(Tag, "Face-down display sleep failed: %s",
                         esp_err_to_name(displayError));
        }
        return false;
    }
    if (events.faceUp) {
        claude::setFaceDown(model_, false);
        claude::setPage(model_, claude::Page::Pet);
        wakeDisplay(nowMs);
        return true;
    }
    if (events.shake) {
        claude::triggerDizzy(model_, nowMs);
        claude::setPage(model_, claude::Page::Pet);
        wakeDisplay(nowMs);
        return true;
    }
    if (events.moved && displayPower_ == platform::DisplayPower::Off) {
        wakeDisplay(nowMs);
        return true;
    }
    return false;
}

void ClaudeRuntime::onConnectionChanged(bool connected) noexcept
{
    const Event event{.type = EventType::Connection, .flag = connected};
    if (!queue_.send(event))
        ESP_LOGW(Tag, "Claude queue full; connection event dropped");
}

void ClaudeRuntime::onData(std::span<const std::uint8_t> data) noexcept
{
    if (data.empty() || data.size() > Event{}.data.size()) {
        ESP_LOGW(Tag, "Rejected Claude BLE chunk of length %u",
                 static_cast<unsigned>(data.size()));
        return;
    }
    Event event{.type = EventType::Data, .dataLength = data.size()};
    std::ranges::copy(data, event.data.begin());
    if (!queue_.send(event)) ESP_LOGW(Tag, "Claude queue full; data dropped");
}

void ClaudeRuntime::onPasskeyChanged(bool visible,
                                     std::uint32_t passkey) noexcept
{
    const Event event{
        .type = EventType::Passkey, .flag = visible, .passkey = passkey};
    if (!queue_.send(event))
        ESP_LOGW(Tag, "Claude queue full; passkey event dropped");
}

void ClaudeRuntime::onSecurityChanged(bool secure) noexcept
{
    const Event event{.type = EventType::Security, .flag = secure};
    if (!queue_.send(event))
        ESP_LOGW(Tag, "Claude queue full; security event dropped");
}

bool ClaudeRuntime::processLine() noexcept
{
    const bool promptWasActive = model_.promptActive;
    const std::uint32_t runningBefore = model_.runningSessions;
    const std::uint32_t waitingBefore = model_.waitingSessions;
    const auto battery = platform::battery();
    const std::uint32_t now = uptimeMs();
    claude::Context context{
        .nowMs = now,
        .uptimeSeconds = now / 1000U,
        .secure = claude::transport::secure(),
        .batteryKnown = battery.has_value(),
        .batteryPercent = battery ? battery->percent : std::uint8_t{0},
        .usbPowered = battery && battery->charging,
    };
    claude::Action action;
    const claude::Result result =
        claude::handleLine(decoder_.line(), model_, context, action);
    if (result != claude::Result::Complete) {
        ESP_LOGW(Tag, "Malformed Claude JSON line rejected: %d",
                 static_cast<int>(result));
        return false;
    }
    if (action.sendResponse) {
        const esp_err_t error =
            claude::transport::sendLine(action.responseView());
        if (error != ESP_OK)
            ESP_LOGW(Tag, "Claude response could not be sent: %s",
                     esp_err_to_name(error));
    }
    if (action.forgetBondAfterResponse) {
        vTaskDelay(pdMS_TO_TICKS(30));
        const esp_err_t error = claude::transport::forgetCurrentPeer();
        if (error != ESP_OK)
            ESP_LOGW(Tag, "Claude bond could not be removed: %s",
                     esp_err_to_name(error));
    }
    if (action.persistModel) {
        const esp_err_t error = claude::storage::save(model_);
        if (error != ESP_OK)
            ESP_LOGW(Tag, "Claude settings could not be saved: %s",
                     esp_err_to_name(error));
    }
    if (action.clockChanged) {
        std::int64_t localSeconds = 0;
        if (claude::clockSeconds(model_, context.nowMs, localSeconds)) {
            const esp_err_t error = platform::writeRtc(localSeconds);
            if (error != ESP_OK)
                ESP_LOGW(Tag, "Claude time could not update the RTC: %s",
                         esp_err_to_name(error));
        }
    }
    if (action.errorOccurred) {
        wakeDisplay(context.nowMs);
        playSound(platform::Sound::Error);
    } else if ((!promptWasActive && model_.promptActive) ||
               (waitingBefore == 0 && model_.waitingSessions > 0)) {
        displayPowerPolicy_.recordAttention(context.nowMs);
        wakeDisplay(context.nowMs);
        playSound(platform::Sound::Attention);
    } else if (runningBefore > 0 && model_.runningSessions == 0 &&
               model_.waitingSessions == 0 && !model_.promptActive) {
        playSound(platform::Sound::Complete);
    }
    return action.modelChanged;
}

bool ClaudeRuntime::processBytes(
    std::span<const std::uint8_t> bytes) noexcept
{
    std::size_t offset = 0;
    bool redraw = false;
    while (offset < bytes.size()) {
        std::size_t consumed = 0;
        const claude::Result result = decoder_.push(bytes.subspan(offset), consumed);
        offset += consumed;
        if (result == claude::Result::Complete) {
            redraw |= processLine();
            decoder_.reset();
        } else if (result == claude::Result::Invalid ||
                   result == claude::Result::NoSpace) {
            ESP_LOGW(Tag, "Claude line framing rejected: %d",
                     static_cast<int>(result));
            decoder_.reset();
        }
    }
    return redraw;
}

bool ClaudeRuntime::processTouch() noexcept
{
    platform::TouchEvent touch;
    const esp_err_t error = platform::pollTouch(touch);
    if (error != ESP_OK) {
        ESP_LOGW(Tag, "Claude touch read failed: %s", esp_err_to_name(error));
        return false;
    }
    if (touch.type != platform::TouchType::Pressed || passkeyVisible_)
        return false;
    const std::uint32_t now = uptimeMs();
    if (displayPower_ == platform::DisplayPower::Off) {
        wakeDisplay(now);
        return true;
    }
    displayPowerPolicy_.recordInteraction(now);
    const display::ClaudeAction action =
        display::claudeHit(model_, touch.x, touch.y);
    switch (action) {
        case display::ClaudeAction::PagePet:
            claude::setPage(model_, claude::Page::Pet);
            return true;
        case display::ClaudeAction::PageActivity:
            claude::setPage(model_, claude::Page::Activity);
            return true;
        case display::ClaudeAction::PageClock:
            claude::setPage(model_, claude::Page::Clock);
            return true;
        case display::ClaudeAction::PageInfo:
            claude::setPage(model_, claude::Page::Info);
            return true;
        case display::ClaudeAction::ActivityUp:
            claude::scrollActivity(model_, -1);
            return true;
        case display::ClaudeAction::ActivityDown:
            claude::scrollActivity(model_, 1);
            return true;
        case display::ClaudeAction::Approve:
        case display::ClaudeAction::Deny: {
            std::array<char, 192> json{};
            std::size_t length = 0;
            const bool approve = action == display::ClaudeAction::Approve;
            if (claude::encodePermission(
                    std::string_view{model_.promptId.data()}, approve, json,
                    length) != claude::Result::Complete) return false;
            const esp_err_t sendError = claude::transport::sendLine(
                {json.data(), length});
            if (sendError != ESP_OK) {
                ESP_LOGW(Tag, "Permission decision could not be sent: %s",
                         esp_err_to_name(sendError));
                return false;
            }
            claude::recordDecision(model_, approve, now);
            playSound(approve ? platform::Sound::Approve
                              : platform::Sound::Deny);
            const esp_err_t storageError = claude::storage::save(model_);
            if (storageError != ESP_OK)
                ESP_LOGW(Tag, "Claude stats could not be saved: %s",
                         esp_err_to_name(storageError));
            return true;
        }
        case display::ClaudeAction::ToggleMute: {
            const bool muted = !audio_.muted();
            const esp_err_t saveError = audio_.setMuted(muted);
            if (saveError != ESP_OK)
                ESP_LOGW(Tag, "Mute setting could not be saved: %s",
                         esp_err_to_name(saveError));
            if (!muted) playSound(platform::Sound::Attention);
            return true;
        }
        case display::ClaudeAction::SwitchMode:
            esp_restart();
            return false;
        case display::ClaudeAction::None:
            return false;
    }
    return false;
}

void ClaudeRuntime::run() noexcept
{
    ESP_LOGI(Tag, "Claude Owl Buddy starting on M5Stack CoreS3");
    claude::init(model_);
    ESP_ERROR_CHECK(claude::storage::load(model_));
    const audio::Initialization audioInitialization = audio_.initialize();
    if (audioInitialization.settingsError != ESP_OK)
        ESP_LOGW(Tag, "Mute setting could not be loaded: %s",
                 esp_err_to_name(audioInitialization.settingsError));
    claude::setConnection(model_, claude::Connection::Connecting);
    decoder_.reset();
    displayPower_ = platform::DisplayPower::Normal;
    displayPowerPolicy_.recordInteraction(uptimeMs());

    if (!audio_.ready())
        ESP_LOGW(Tag, "Claude will run without sounds: %s",
                 esp_err_to_name(audioInitialization.speakerError));

    const esp_err_t rtcError = platform::initializeRtc();
    if (rtcError == ESP_OK) {
        std::int64_t localSeconds = 0;
        if (platform::readRtc(localSeconds) == ESP_OK)
            claude::setClock(model_, static_cast<std::uint64_t>(localSeconds),
                             0, uptimeMs());
    } else {
        ESP_LOGW(Tag, "Claude will wait for desktop time sync: %s",
                 esp_err_to_name(rtcError));
    }

    motion::init(motion_);
    const esp_err_t imuError = platform::initializeImu();
    imuReady_ = imuError == ESP_OK;
    if (!imuReady_)
        ESP_LOGW(Tag, "Claude will run without motion gestures: %s",
                 esp_err_to_name(imuError));
    if (!queue_.initialize()) {
        ESP_LOGE(Tag, "Could not allocate Claude event queue");
        return;
    }
    render();
    ESP_ERROR_CHECK(claude::transport::initialize(*this));

    for (;;) {
        Event event;
        bool redraw = false;
        const TickType_t wait = displayPower_ == platform::DisplayPower::Off
            ? pdMS_TO_TICKS(MotionPollMs) : pdMS_TO_TICKS(25);
        if (queue_.receive(event, wait)) {
            switch (event.type) {
                case EventType::Connection:
                    claude::setConnection(
                        model_, event.flag ? claude::Connection::Connected
                                           : claude::Connection::Disconnected);
                    if (event.flag) {
                        wakeDisplay(uptimeMs());
                        playSound(platform::Sound::Connect);
                    }
                    redraw = true;
                    break;
                case EventType::Data:
                    redraw |= processBytes(
                        {event.data.data(), event.dataLength});
                    break;
                case EventType::Passkey:
                    passkeyVisible_ = event.flag;
                    passkey_ = event.passkey;
                    if (event.flag) wakeDisplay(uptimeMs());
                    redraw = true;
                    break;
                case EventType::Security:
                    redraw = true;
                    break;
            }
        }
        redraw |= processTouch();
        const std::uint32_t now = uptimeMs();
        redraw |= processMotion(now);
        const claude::Connection before = model_.connection;
        claude::expireConnection(model_, now, SnapshotTimeoutMs);
        redraw |= before != model_.connection;
        const platform::DisplayPower desired = model_.faceDown
            ? platform::DisplayPower::Off
            : platformPower(displayPowerPolicy_.desired(
                  now, model_.promptActive || passkeyVisible_ ||
                           model_.waitingSessions > 0));
        if (desired != displayPower_) {
            const bool wasOff = displayPower_ == platform::DisplayPower::Off;
            const esp_err_t error = platform::setDisplayPower(desired);
            if (error != ESP_OK)
                ESP_LOGW(Tag, "Claude display power change failed: %s",
                         esp_err_to_name(error));
            else {
                displayPower_ = desired;
                redraw |= wasOff && desired != platform::DisplayPower::Off;
            }
        }
        const std::uint32_t refreshPeriod = model_.page == claude::Page::Clock
            ? ClockRefreshPeriodMs : PetAnimationPeriodMs;
        if (displayPower_ == platform::DisplayPower::Normal &&
            now - lastAnimationMs_ >= refreshPeriod) {
            lastAnimationMs_ = now;
            redraw |= model_.page == claude::Page::Pet ||
                      model_.page == claude::Page::Clock;
        }
        if (redraw) render();
    }
}

}  // namespace buddy::runtime
