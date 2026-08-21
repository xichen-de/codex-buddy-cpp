#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "claude_ble_transport.hpp"
#include "claude_model.hpp"
#include "claude_protocol.hpp"
#include "display_power_policy.hpp"
#include "freertos_queue.hpp"
#include "motion_detector.hpp"
#include "platform_core_s3.hpp"

namespace buddy::runtime {

class ClaudeRuntime final : public claude::transport::Listener {
public:
    void run() noexcept;

private:
    enum class EventType : std::uint8_t {
        Connection,
        Data,
        Passkey,
        Security,
    };

    struct Event {
        EventType type{EventType::Connection};
        bool flag{};
        std::array<std::uint8_t, 512> data{};
        std::size_t dataLength{};
        std::uint32_t passkey{};
    };

    void onConnectionChanged(bool connected) noexcept override;
    void onData(std::span<const std::uint8_t> data) noexcept override;
    void onPasskeyChanged(bool visible,
                          std::uint32_t passkey) noexcept override;
    void onSecurityChanged(bool secure) noexcept override;

    void render() noexcept;
    void playSound(platform::Sound sound) noexcept;
    void wakeDisplay(std::uint32_t nowMs) noexcept;
    [[nodiscard]] bool processMotion(std::uint32_t nowMs) noexcept;
    [[nodiscard]] bool processLine() noexcept;
    [[nodiscard]] bool processBytes(
        std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool processTouch() noexcept;

    static constexpr std::size_t QueueCapacity = 12;
    static constexpr std::uint32_t SnapshotTimeoutMs = 30'000;
    static constexpr std::uint32_t MotionPollMs = 40;
    static constexpr std::uint32_t AnimationPeriodMs = 120;

    claude::Model model_{};
    claude::Decoder decoder_{};
    Queue<Event, QueueCapacity> queue_{};
    motion::Detector motion_{};
    DisplayPowerPolicy displayPowerPolicy_{};
    bool passkeyVisible_{};
    std::uint32_t passkey_{};
    platform::DisplayPower displayPower_{platform::DisplayPower::Normal};
    bool speakerReady_{};
    bool imuReady_{};
    std::uint32_t lastMotionMs_{};
    std::uint32_t lastAnimationMs_{};
};

}  // namespace buddy::runtime
