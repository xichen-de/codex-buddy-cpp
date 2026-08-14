#include "claude_model.hpp"

#include <cstring>

namespace buddy::claude {

/* Establishes defaults for connection, page, identity, and transient state. */
void init(Model &model) noexcept
{
    model = Model{};
    std::strcpy(model.deviceName.data(), "Claude CoreS3");
}

/* Applies a connection transition and refreshes connection-scoped fields. */
void setConnection(Model &model, Connection connection) noexcept
{
    if (connection > Connection::Connected) return;
    model.connection = connection;
    if (connection != Connection::Connected) {
        model.totalSessions = 0;
        model.runningSessions = 0;
        model.waitingSessions = 0;
        model.promptActive = false;
        model.promptId[0] = '\0';
        model.promptTool[0] = '\0';
        model.promptHint[0] = '\0';
        model.lastSnapshotMs = 0;
    }
}

/* Selects a valid page and leaves state unchanged for invalid values. */
void setPage(Model &model, Page page) noexcept
{
    if (page <= Page::Info) model.page = page;
}

/* Counts entries until the first empty slot in the compact activity list. */
std::size_t activityCount(const Model &model) noexcept
{
    std::size_t count = 0;
    while (count < ModelEntryCount && model.entries[count][0] != '\0') ++count;
    return count;
}

/* Clamps activity scrolling to the available entries and visible window. */
void scrollActivity(Model &model, int direction) noexcept
{
    if (direction == 0) return;
    const std::size_t count = activityCount(model);
    const std::size_t maximum = count > ModelActivityVisibleCount
        ? count - ModelActivityVisibleCount : 0;
    if (direction < 0 && model.activityOffset > 0)
        --model.activityOffset;
    else if (direction > 0 && model.activityOffset < maximum)
        ++model.activityOffset;
}

/* Captures a clock synchronization value and its monotonic reference point. */
void setClock(Model &model, std::uint64_t epochSeconds,
             std::int32_t timezoneOffset, std::uint32_t nowMs) noexcept
{
    model.clockValid = true;
    model.clockEpochSeconds = epochSeconds;
    model.clockTimezoneOffset = timezoneOffset;
    model.clockSyncMs = nowMs;
}

/* Advances the synchronized clock by elapsed whole seconds and timezone offset. */
bool clockSeconds(const Model &model, std::uint32_t nowMs,
                  std::int64_t &localSeconds) noexcept
{
    if (!model.clockValid) return false;
    localSeconds = static_cast<std::int64_t>(model.clockEpochSeconds) +
        static_cast<std::int64_t>(model.clockTimezoneOffset) +
        static_cast<std::uint32_t>(nowMs - model.clockSyncMs) / 1000U;
    return true;
}

/* Updates approval statistics and selects short-lived positive/negative feedback. */
void recordDecision(Model &model, bool approved, std::uint32_t nowMs) noexcept
{
    if (!model.promptActive) return;
    if (approved) {
        ++model.approvals;
        model.transientState = PetState::Heart;
    } else {
        ++model.denials;
        model.transientState = PetState::Idle;
    }
    model.transientUntilMs = nowMs + 1800U;
    model.promptActive = false;
}

/* Replaces the current transient with a timed dizzy animation. */
void triggerDizzy(Model &model, std::uint32_t nowMs) noexcept
{
    model.transientState = PetState::Dizzy;
    model.transientUntilMs = nowMs + 2200U;
}

/* Stores the stable orientation state used to override the visible pet state. */
void setFaceDown(Model &model, bool faceDown) noexcept
{
    model.faceDown = faceDown;
}

/* Disconnects a model whose last desktop snapshot is older than the timeout. */
void expireConnection(Model &model, std::uint32_t nowMs, std::uint32_t timeoutMs) noexcept
{
    if (model.connection != Connection::Connected || model.lastSnapshotMs == 0)
        return;
    if (nowMs - model.lastSnapshotMs > timeoutMs)
        setConnection(model, Connection::Disconnected);
}

/* Resolves visible owl-state priority from sleep through active transients. */
PetState petState(const Model &model, std::uint32_t nowMs) noexcept
{
    if (model.connection != Connection::Connected) return PetState::Sleep;
    if (model.faceDown) return PetState::Sleep;
    if (model.transientUntilMs != 0 && nowMs < model.transientUntilMs)
        return model.transientState;
    if (model.promptActive || model.waitingSessions > 0) return PetState::Attention;
    if (model.runningSessions > 0) return PetState::Busy;
    return PetState::Idle;
}

}  // namespace buddy::claude
