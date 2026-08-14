#include "codex_ui.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace buddy::codex;

constexpr std::uint16_t rgb565(unsigned red, unsigned green,
                               unsigned blue) noexcept
{
    return static_cast<std::uint16_t>(
        ((red & 0xf8U) << 8) | ((green & 0xfcU) << 3) | (blue >> 3));
}

static uint16_t pixel_at(const uint16_t *pixels, unsigned x, unsigned y)
{
    assert(x < buddy::codex::ui::Width && y < buddy::codex::ui::Height);
    return pixels[y * buddy::codex::ui::Width + x];
}

static uint64_t checksum(const uint16_t *pixels)
{
    uint64_t value = 1469598103934665603ULL;
    for (size_t i = 0; i < buddy::codex::ui::PixelCount; ++i)
        value = (value ^ pixels[i]) * 1099511628211ULL;
    return value;
}

static void dismiss_overlay(Model &model)
{
    if (overlay(model) == Overlay::None) return;
    const Event dismiss = OverlayDismissed{};
    assert(applyEvent(model, dismiss));
}

static void set_slot(Model &model, uint8_t index, SlotStatus status, bool breathing)
{
    Slot value{
        .status = status,
        .color = 0xffffff,
        .brightness = 1.0f,
        .effectSpeed = 1.0f,
        .breathing = breathing,
    };
    strcpy(value.effect.data(), breathing ? "breath" : "solid");
    assert(applyEvent(model, Event{SlotStatusChanged{.index = index, .value = value}}));
}

static void write_snapshot(const char *directory, const char *name,
                           const uint16_t *pixels)
{
    if (directory == nullptr) return;
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    assert(written > 0 && (size_t)written < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    fprintf(file, "P6\n%d %d\n255\n", buddy::codex::ui::Width, buddy::codex::ui::Height);
    for (size_t index = 0; index < buddy::codex::ui::PixelCount; ++index) {
        const uint16_t pixel = pixels[index];
        fputc((pixel >> 11) * 255 / 31, file);
        fputc(((pixel >> 5) & 0x3fU) * 255 / 63, file);
        fputc((pixel & 0x1fU) * 255 / 31, file);
    }
    assert(fclose(file) == 0);
}

int main(void)
{
    assert(strcmp(buddy::codex::ui::slotStatusLabel(SlotStatus::RequiresInput), "INPUT") == 0);
    assert(strcmp(buddy::codex::ui::connectionLabel(Connection::Connecting), "CONNECTING") == 0);
    std::vector<uint16_t> storage(buddy::codex::ui::PixelCount);
    auto *pixels = storage.data();
    Model model;
    init(model);
    buddy::codex::ui::render(model, nullptr, 0, storage);
    const uint64_t disconnected = checksum(pixels);

    const Event open_menu = MenuOpened{};
    assert(applyEvent(model, open_menu));
    buddy::codex::ui::render(model, nullptr, 0, storage);
    assert(checksum(pixels) != disconnected);
    write_snapshot(getenv("UI_SNAPSHOT_DIR"), "codex-menu", pixels);
    const Event close_menu = MenuClosed{};
    assert(applyEvent(model, close_menu));

    Event event = ConnectionChanged{Connection::Connected};
    assert(applyEvent(model, event));
    buddy::codex::ui::render(model, nullptr, 0, storage);
    assert(pixel_at(pixels, 160, 82) == rgb565(45, 145, 235));

    event = SlotSelected{1};
    assert(applyEvent(model, event));
    buddy::codex::ui::render(model, nullptr, 0, storage);
    assert(checksum(pixels) != disconnected);
    assert(pixel_at(pixels, 55, 38) == rgb565(45, 190, 225));
    assert(pixel_at(pixels, 160, 38) == rgb565(35, 190, 100));
    assert(pixel_at(pixels, 265, 38) == rgb565(230, 65, 75));
    write_snapshot(getenv("UI_SNAPSHOT_DIR"), "control", pixels);

    event = PageSelected{Page::Navigate};
    assert(applyEvent(model, event));
    buddy::codex::ui::render(model, nullptr, 0, storage);
    const uint64_t navigate = checksum(pixels);
    assert(navigate != disconnected);

    Action active{.type = ActionType::Key};
    active.key = buddy::codex::Key::DialPress;
    buddy::codex::ui::render(model, &active, 0, storage);
    assert(checksum(pixels) != navigate);

    event = PageSelected{Page::Agents};
    assert(applyEvent(model, event));
    set_slot(model, 0, SlotStatus::Idle, false);
    set_slot(model, 1, SlotStatus::Thinking, true);
    set_slot(model, 2, SlotStatus::Complete, false);
    dismiss_overlay(model);
    set_slot(model, 3, SlotStatus::RequiresInput, false);
    dismiss_overlay(model);
    set_slot(model, 4, SlotStatus::Error, false);
    dismiss_overlay(model);

    buddy::codex::ui::render(model, nullptr, 0, storage);
    assert(pixel_at(pixels, 60, 45) == rgb565(15, 42, 65));
    assert(pixel_at(pixels, 165, 45) == rgb565(13, 48, 62));
    assert(pixel_at(pixels, 270, 45) == rgb565(12, 47, 31));
    assert(pixel_at(pixels, 60, 129) == rgb565(55, 39, 17));
    assert(pixel_at(pixels, 165, 129) == rgb565(54, 22, 29));
    assert(pixel_at(pixels, 190, 38) == rgb565(242, 246, 252));
    const uint64_t agents_dim = checksum(pixels);
    buddy::codex::ui::render(model, nullptr, 800, storage);
    assert(checksum(pixels) != agents_dim);
    write_snapshot(getenv("UI_SNAPSHOT_DIR"), "agents", pixels);

    set_slot(model, 2, SlotStatus::Idle, false);
    set_slot(model, 2, SlotStatus::Complete, false);
    buddy::codex::ui::render(model, nullptr, 800, storage);
    assert(pixel_at(pixels, 160, 144) == rgb565(35, 190, 100));
    write_snapshot(getenv("UI_SNAPSHOT_DIR"), "complete", pixels);
    puts("codex_ui host tests passed");
    return 0;
}
