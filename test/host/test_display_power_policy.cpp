#include <cassert>
#include <cstdint>

#include "display_power_policy.hpp"

int main()
{
    using buddy::runtime::DisplayPowerLevel;
    using buddy::runtime::DisplayPowerPolicy;

    DisplayPowerPolicy policy;
    policy.recordInteraction(1'000);
    assert(policy.desired(15'999, false) == DisplayPowerLevel::Normal);
    assert(policy.desired(16'000, false) == DisplayPowerLevel::Dimmed);
    assert(policy.desired(61'000, false) == DisplayPowerLevel::Off);
    assert(policy.desired(500'000, true) == DisplayPowerLevel::Off);
    assert(policy.desired(500'000, false) == DisplayPowerLevel::Off);

    policy.recordAttention(600'000);
    assert(policy.desired(659'999, true) == DisplayPowerLevel::Normal);
    assert(policy.desired(660'000, true) == DisplayPowerLevel::Off);

    policy.recordInteraction(UINT32_MAX - 9'999U);
    assert(policy.desired(5'000, false) == DisplayPowerLevel::Dimmed);
    return 0;
}
