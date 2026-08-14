#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/* Pure presentation/session state for Claude mode; contains no hardware I/O. */

namespace buddy::claude {

inline constexpr std::size_t ModelEntryCount = 8;
inline constexpr std::size_t ModelActivityVisibleCount = 4;
inline constexpr std::size_t ModelMessageSize = 96;
inline constexpr std::size_t ModelEntrySize = 80;
inline constexpr std::size_t ModelPromptIdSize = 80;
inline constexpr std::size_t ModelPromptToolSize = 40;
inline constexpr std::size_t ModelPromptHintSize = 128;
inline constexpr std::size_t ModelNameSize = 32;

enum class Connection : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
};

enum class Page : std::uint8_t {
    Pet,
    Activity,
    Clock,
    Info,
};

enum class PetState : std::uint8_t {
    Sleep,
    Idle,
    Busy,
    Attention,
    Celebrate,
    Heart,
    Dizzy,
};

struct Model {
    Connection connection{Connection::Disconnected};
    Page page{Page::Pet};
    bool faceDown{};
    std::uint32_t totalSessions{};
    std::uint32_t runningSessions{};
    std::uint32_t waitingSessions{};
    std::uint64_t tokens{};
    std::uint64_t tokensToday{};
    std::array<char, ModelMessageSize> message{};
    std::array<std::array<char, ModelEntrySize>, ModelEntryCount> entries{};
    std::uint8_t activityOffset{};
    bool clockValid{};
    std::uint64_t clockEpochSeconds{};
    std::int32_t clockTimezoneOffset{};
    std::uint32_t clockSyncMs{};
    bool promptActive{};
    std::array<char, ModelPromptIdSize> promptId{};
    std::array<char, ModelPromptToolSize> promptTool{};
    std::array<char, ModelPromptHintSize> promptHint{};
    std::array<char, ModelNameSize> owner{};
    std::array<char, ModelNameSize> deviceName{};
    std::uint32_t approvals{};
    std::uint32_t denials{};
    std::uint32_t lastSnapshotMs{};
    std::uint32_t transientUntilMs{};
    PetState transientState{PetState::Idle};
};

/* Initializes empty Claude session/UI state with the pet page selected. */
void init(Model &model) noexcept;

/* Changes connection state and clears connection-scoped data when needed. */
void setConnection(Model &model, Connection connection) noexcept;

/* Selects one of the four Claude pages when the value is valid. */
void setPage(Model &model, Page page) noexcept;

/* Moves the activity window one entry up or down within valid bounds. */
void scrollActivity(Model &model, int direction) noexcept;

/* Counts populated activity entries in the fixed-size newest-first list. */
[[nodiscard]] std::size_t activityCount(const Model &model) noexcept;

/* Stores a desktop clock sync and the uptime at which it was received. */
void setClock(Model &model, std::uint64_t epochSeconds,
             std::int32_t timezoneOffset, std::uint32_t nowMs) noexcept;

/* Computes current local seconds from the last sync and elapsed uptime. */
[[nodiscard]] bool clockSeconds(const Model &model, std::uint32_t nowMs,
                                std::int64_t &localSeconds) noexcept;

/* Records a permission decision, clears its prompt, and starts feedback state. */
void recordDecision(Model &model, bool approved, std::uint32_t nowMs) noexcept;

/* Starts the temporary dizzy pet state at the given uptime. */
void triggerDizzy(Model &model, std::uint32_t nowMs) noexcept;

/* Records orientation sleep state; face-down suppresses other pet states. */
void setFaceDown(Model &model, bool faceDown) noexcept;

/* Marks a stale connected model disconnected when snapshots stop arriving. */
void expireConnection(Model &model, std::uint32_t nowMs, std::uint32_t timeoutMs) noexcept;

/* Derives the owl's visible state from prompts, sessions, and transients. */
[[nodiscard]] PetState petState(const Model &model, std::uint32_t nowMs) noexcept;

}  // namespace buddy::claude
