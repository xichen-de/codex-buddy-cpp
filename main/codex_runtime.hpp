#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "codex_ble_transport.hpp"
#include "codex_controller.hpp"
#include "codex_input.hpp"
#include "codex_model.hpp"
#include "codex_protocol.hpp"
#include "display_power_policy.hpp"
#include "freertos_queue.hpp"
#include "platform_core_s3.hpp"

namespace buddy::runtime {

class CodexRuntime final : public codex::transport::Listener,
                           public codex::CommandTransport {
public:
    void run() noexcept;

private:
    enum class EventType : std::uint8_t { Connection, Report };

    struct Event {
        EventType type{EventType::Connection};
        bool connected{};
        std::array<std::uint8_t, codex::ReportBodySize + 1> report{};
        std::size_t reportLength{};
    };

    void onConnectionChanged(bool connected) noexcept override;
    void onReport(std::span<const std::uint8_t> report) noexcept override;
    [[nodiscard]] bool sendJson(std::string_view json) noexcept override;

    void render() noexcept;
    [[nodiscard]] bool applyConnection(bool connected) noexcept;
    [[nodiscard]] bool processCompleteRequest() noexcept;
    [[nodiscard]] bool processReport(
        std::span<const std::uint8_t> report) noexcept;
    [[nodiscard]] bool processTouch() noexcept;
    [[nodiscard]] bool animationDue() noexcept;
    void wakeDisplay(std::uint32_t nowMs) noexcept;
    [[nodiscard]] bool updateDisplayPower(std::uint32_t nowMs) noexcept;

    static constexpr std::size_t QueueCapacity = 12;
    static constexpr std::uint32_t AnimationPeriodMs = 80;

    codex::Model model_{};
    codex::Decoder decoder_{};
    codex::Input input_{};
    Queue<Event, QueueCapacity> queue_{};
    DisplayPowerPolicy displayPowerPolicy_{};
    platform::DisplayPower displayPower_{platform::DisplayPower::Normal};
    std::uint32_t lastAnimationMs_{};
};

}  // namespace buddy::runtime
