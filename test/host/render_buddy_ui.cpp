#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "buddy_ui.hpp"
#include "codex_ui.hpp"

namespace {

void write_ppm(const char *directory, const char *name,
               std::span<const std::uint16_t> pixels)
{
    char path[512];
    const int length = std::snprintf(path, sizeof(path), "%s/%s.ppm", directory,
                                     name);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(path))
        std::exit(1);

    FILE *file = std::fopen(path, "wb");
    if (file == nullptr) std::exit(1);
    std::fprintf(file, "P6\n%d %d\n255\n", buddy::display::Width,
                 buddy::display::Height);
    for (const std::uint16_t value : pixels) {
        const std::array<std::uint8_t, 3> rgb = {
            static_cast<std::uint8_t>(((value >> 11) & 0x1fU) * 255U / 31U),
            static_cast<std::uint8_t>(((value >> 5) & 0x3fU) * 255U / 63U),
            static_cast<std::uint8_t>((value & 0x1fU) * 255U / 31U),
        };
        if (std::fwrite(rgb.data(), rgb.size(), 1, file) != 1) std::exit(1);
    }
    if (std::fclose(file) != 0) std::exit(1);
}

buddy::codex::Slot codex_slot(buddy::codex::SlotStatus status,
                              std::uint32_t color, bool breathing = false)
{
    buddy::codex::Slot slot{
        .status = status,
        .color = color,
        .brightness = 1.0F,
        .effectSpeed = 1.0F,
        .breathing = breathing,
    };
    std::snprintf(slot.effect.data(), slot.effect.size(), "%s",
                  breathing ? "breath" : "solid");
    return slot;
}

void render_codex_gallery(const char *directory,
                          std::span<std::uint16_t> pixels)
{
    buddy::codex::Model model;
    buddy::codex::init(model);
    model.connection = buddy::codex::Connection::Connected;
    model.selectedSlot = 1;
    model.slots = {
        codex_slot(buddy::codex::SlotStatus::Idle, 0x2d91eb),
        codex_slot(buddy::codex::SlotStatus::Thinking, 0x23be64, true),
        codex_slot(buddy::codex::SlotStatus::Complete, 0x23be64),
        codex_slot(buddy::codex::SlotStatus::RequiresInput, 0xffa21a),
        codex_slot(buddy::codex::SlotStatus::Error, 0xe6414b),
        codex_slot(buddy::codex::SlotStatus::Unassigned, 0x607080),
    };

    model.page = buddy::codex::Page::Control;
    buddy::codex::ui::render(model, nullptr, 800, pixels);
    write_ppm(directory, "codex-control", pixels);

    model.page = buddy::codex::Page::Agents;
    buddy::codex::ui::render(model, nullptr, 800, pixels);
    write_ppm(directory, "codex-agents", pixels);

    model.page = buddy::codex::Page::Navigate;
    buddy::codex::ui::render(model, nullptr, 800, pixels);
    write_ppm(directory, "codex-navigate", pixels);
}

void render_claude_gallery(const char *directory,
                           std::span<std::uint16_t> pixels)
{
    constexpr std::uint32_t now_ms = 5000;
    buddy::claude::Model model;
    buddy::claude::init(model);
    buddy::claude::setConnection(model, buddy::claude::Connection::Connected);
    model.totalSessions = 8;
    model.runningSessions = 2;
    model.waitingSessions = 1;
    model.tokensToday = 12345;
    model.tokens = 987654;
    std::snprintf(model.message.data(), model.message.size(),
                  "READY FOR YOUR NEXT IDEA");
    const std::array<const char *, buddy::claude::ModelEntryCount> entries = {
        "REFACTORED DISPLAY DRIVER", "HOST TESTS PASSED", "REVIEWING PROTOCOL",
        "UPDATED README", "BUILT RELEASE FIRMWARE", "CHECKED TOUCH TARGETS",
        "SYNCED DEVICE CLOCK", "WAITING FOR CLAUDE",
    };
    for (std::size_t index = 0; index < entries.size(); ++index)
        std::snprintf(model.entries[index].data(), model.entries[index].size(),
                      "%s", entries[index]);
    std::snprintf(model.owner.data(), model.owner.size(), "XI");
    std::snprintf(model.deviceName.data(), model.deviceName.size(),
                  "CLAUDE CORES3-A7F2");
    model.approvals = 12;
    model.denials = 1;

    model.page = buddy::claude::Page::Pet;
    model.transientState = buddy::claude::PetState::Celebrate;
    model.transientUntilMs = 10000;
    buddy::display::renderClaude(model, now_ms, false, 0, false, 0, false,
                                 pixels);
    write_ppm(directory, "claude-owl", pixels);

    model.page = buddy::claude::Page::Activity;
    buddy::display::renderClaude(model, now_ms, false, 0, false, 0, false,
                                 pixels);
    write_ppm(directory, "claude-activity", pixels);

    model.page = buddy::claude::Page::Clock;
    buddy::claude::setClock(model, 1786034096ULL, 7200, now_ms);
    buddy::display::renderClaude(model, now_ms, false, 0, false, 0, false,
                                 pixels);
    write_ppm(directory, "claude-clock", pixels);

    model.page = buddy::claude::Page::Info;
    buddy::display::renderClaude(model, now_ms, false, 0, false, 0, false,
                                 pixels);
    write_ppm(directory, "claude-info", pixels);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    std::vector<std::uint16_t> pixels(buddy::display::PixelCount);

    buddy::display::renderSelector(pixels);
    write_ppm(argv[1], "buddy-selector", pixels);
    render_codex_gallery(argv[1], pixels);
    render_claude_gallery(argv[1], pixels);
    return 0;
}
