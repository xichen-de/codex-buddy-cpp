#pragma once

#include <cstdint>

namespace buddy::runtime {

enum class DisplayPowerLevel : std::uint8_t { Normal, Dimmed, Off };

class DisplayPowerPolicy {
public:
    static constexpr std::uint32_t DimTimeoutMs = 15'000;
    static constexpr std::uint32_t OffTimeoutMs = 60'000;

    void recordInteraction(std::uint32_t nowMs) noexcept
    {
        lastInteractionMs_ = nowMs;
    }

    [[nodiscard]] DisplayPowerLevel desired(
        std::uint32_t nowMs, bool attentionRequired) const noexcept
    {
        if (attentionRequired) return DisplayPowerLevel::Normal;
        const std::uint32_t idleMs = nowMs - lastInteractionMs_;
        if (idleMs >= OffTimeoutMs) return DisplayPowerLevel::Off;
        if (idleMs >= DimTimeoutMs) return DisplayPowerLevel::Dimmed;
        return DisplayPowerLevel::Normal;
    }

private:
    std::uint32_t lastInteractionMs_{};
};

}  // namespace buddy::runtime
