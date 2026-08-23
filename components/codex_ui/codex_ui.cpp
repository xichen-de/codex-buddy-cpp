#include "codex_ui.hpp"

#include <cstdio>
#include <cstring>

#include "buddy_layout.hpp"

#ifdef BUDDY_USE_LVGL_FONT
#include "ui_font.hpp"
#else
#include "buddy_glyph_font.hpp"
#endif

namespace buddy::codex::ui {

constexpr std::uint16_t rgb565(unsigned red, unsigned green,
                               unsigned blue) noexcept
{
    return static_cast<std::uint16_t>(
        ((red & 0xf8U) << 8) | ((green & 0xfcU) << 3) | (blue >> 3));
}

constexpr auto Background = rgb565(16, 19, 24);
constexpr auto Panel = rgb565(25, 30, 37);
constexpr auto PanelPressed = rgb565(41, 49, 59);
constexpr auto PanelDisabled = rgb565(20, 24, 30);
constexpr auto Outline = rgb565(53, 61, 71);
constexpr auto Text = rgb565(199, 206, 214);
constexpr auto Muted = rgb565(145, 155, 167);
constexpr auto Dim = rgb565(82, 92, 104);
constexpr auto Accent = rgb565(75, 135, 188);
constexpr auto Cyan = rgb565(73, 153, 174);
constexpr auto Green = rgb565(67, 146, 101);
constexpr auto Red = rgb565(181, 83, 89);
constexpr auto Amber = rgb565(181, 132, 68);
constexpr auto TintBlue = rgb565(23, 36, 48);
constexpr auto TintCyan = rgb565(22, 39, 46);
constexpr auto TintGreen = rgb565(22, 38, 30);
constexpr auto TintRed = rgb565(43, 27, 31);
constexpr auto TintAmber = rgb565(43, 35, 23);
constexpr int TabTop = 210;

struct Canvas {
    std::uint16_t *pixels{};
};

/* Fills a clipped axis-aligned rectangle in the canvas. */
static void fill_rect(Canvas *canvas, int x, int y, int width, int height,
                      uint16_t color)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > Width) width = Width - x;
    if (y + height > Height) height = Height - y;
    if (width <= 0 || height <= 0) return;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
            canvas->pixels[row * Width + column] = color;
}

/* Rasterizes a filled circle with clipping delegated to the rectangle helper. */
static void fill_circle(Canvas *canvas, int center_x, int center_y,
                        int radius, uint16_t color)
{
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                fill_rect(canvas, center_x + x, center_y + y, 1, 1, color);
}

/* Builds a filled rounded rectangle from rectangular bands and corner circles. */
static void fill_rounded_rect(Canvas *canvas, int x, int y, int width,
                              int height, int radius, uint16_t color)
{
    if (radius <= 0) {
        fill_rect(canvas, x, y, width, height, color);
        return;
    }
    fill_rect(canvas, x + radius, y, width - 2 * radius, height, color);
    fill_rect(canvas, x, y + radius, width, height - 2 * radius, color);
    fill_circle(canvas, x + radius, y + radius, radius, color);
    fill_circle(canvas, x + width - radius - 1, y + radius, radius, color);
    fill_circle(canvas, x + radius, y + height - radius - 1, radius, color);
    fill_circle(canvas, x + width - radius - 1,
                y + height - radius - 1, radius, color);
}

/* Draws a rounded border and inset fill using two filled rounded rectangles. */
static void rounded_box(Canvas *canvas, int x, int y, int width, int height,
                        int radius, int thickness, uint16_t border,
                        uint16_t background)
{
    fill_rounded_rect(canvas, x, y, width, height, radius, border);
    fill_rounded_rect(canvas, x + thickness, y + thickness,
                      width - 2 * thickness, height - 2 * thickness,
                      radius - thickness, background);
}

/* Calculates pixel width for fixed 5x7 glyphs plus one-column spacing. */
static int text_width(const char *text, int scale)
{
#ifdef BUDDY_USE_LVGL_FONT
    return text == nullptr ? 0 : buddy::font::textWidth(text, scale);
#else
    return text == nullptr ? 0
                           : static_cast<int>(strlen(text)) * 6 * scale - scale;
#endif
}

/* Rasterizes scaled bitmap text from left to right at a baseline origin. */
static void draw_text(Canvas *canvas, const char *text, int x, int y,
                      int scale, uint16_t color)
{
    if (text == nullptr || scale <= 0) return;
#ifdef BUDDY_USE_LVGL_FONT
    buddy::font::draw({canvas->pixels, PixelCount}, Width, Height, text,
                      x, y, scale, color);
#else
    for (; *text != '\0'; ++text, x += 6 * scale) {
        const uint8_t *columns = layout::glyphBitmap(*text);
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if ((columns[column] & (1U << row)) != 0)
                    fill_rect(canvas, x + column * scale, y + row * scale,
                              scale, scale, color);
    }
#endif
}

/* Centers bitmap text horizontally around a requested x coordinate. */
static void centered_text(Canvas *canvas, const char *text, int center_x,
                          int y, int scale, uint16_t color)
{
    draw_text(canvas, text, center_x - text_width(text, scale) / 2, y, scale,
              color);
}

/* Tests whether the active pressed action matches a button type and value. */
static bool action_matches(const Action *active, ActionType type,
                           std::uint8_t value)
{
    if (active == nullptr || active->type != type) return false;
    switch (type) {
        case ActionType::Slot: return active->slot == value;
        case ActionType::Key:
            return static_cast<std::uint8_t>(active->key) == value;
        case ActionType::Direction:
            return static_cast<std::uint8_t>(active->direction) == value;
        default: return false;
    }
}

static bool action_matches(const Action *active, ActionType type,
                           buddy::codex::Key value)
{
    return action_matches(active, type, static_cast<std::uint8_t>(value));
}

static bool action_matches(const Action *active, ActionType type,
                           buddy::codex::Direction value)
{
    return action_matches(active, type, static_cast<std::uint8_t>(value));
}

/* Draws one labeled control with enabled, disabled, and pressed styling. */
static void button(Canvas *canvas, int x, int y, int width, int height,
                   const char *label, const char *hint, uint16_t border,
                   uint16_t background, bool pressed, bool enabled)
{
    rounded_box(canvas, x, y, width, height, 7, 1,
                enabled ? (pressed ? border : Outline) : Dim,
                enabled ? (pressed ? PanelPressed : background)
                        : PanelDisabled);
    if (enabled)
        fill_rounded_rect(canvas, x + 12, y + 1, width - 24, 3, 1, border);
    const int scale = strlen(label) > 8 ? 1 : 2;
    centered_text(canvas, label, x + width / 2,
                  y + (hint == nullptr ? height / 2 - 7 * scale / 2 : height / 2 - 13),
                  scale, enabled ? Text : Muted);
    if (hint != nullptr)
        centered_text(canvas, hint, x + width / 2, y + height / 2 + 12, 1,
                      Muted);
}

/* Maps semantic Agent status to the compact on-screen label. */
const char *slotStatusLabel(SlotStatus status) noexcept
{
    static const char *const labels[] = {
        "UNASSIGNED", "IDLE", "THINKING", "COMPLETE", "INPUT", "ERROR", "STATUS"};
    return status >= SlotStatus::Unassigned && status <= SlotStatus::Unknown
               ? labels[static_cast<std::size_t>(status)] : "STATUS";
}

/* Maps connection state to the compact header/overlay label. */
const char *connectionLabel(Connection connection) noexcept
{
    static const char *const labels[] = {"DISCONNECTED", "CONNECTING", "CONNECTED"};
    return connection >= Connection::Disconnected &&
                   connection <= Connection::Connected
               ? labels[static_cast<std::size_t>(connection)] : "DISCONNECTED";
}

/* Applies brightness to RGB888 channels and packs the result as RGB565. */
static uint16_t scaled_color(unsigned red, unsigned green, unsigned blue,
                             float brightness)
{
    red = static_cast<unsigned>(red * brightness);
    green = static_cast<unsigned>(green * brightness);
    blue = static_cast<unsigned>(blue * brightness);
    return rgb565(red, green, blue);
}

/* Derives an animated RGB565 accent from raw slot color/effect metadata. */
static uint16_t slot_accent(const Slot *slot, uint32_t time_ms)
{
    /* Stable semantic colors keep status readable across host lighting themes.
       Unknown future states retain the raw host color as a fallback. */
    unsigned red = 82;
    unsigned green = 92;
    unsigned blue = 104;
    switch (slot->status) {
        case SlotStatus::Idle: red = 75; green = 135; blue = 188; break;
        case SlotStatus::Thinking: red = 73; green = 153; blue = 174; break;
        case SlotStatus::Complete: red = 67; green = 146; blue = 101; break;
        case SlotStatus::RequiresInput: red = 181; green = 132; blue = 68; break;
        case SlotStatus::Error: red = 181; green = 83; blue = 89; break;
        case SlotStatus::Unknown:
            red = (slot->color >> 16) & 0xffU;
            green = (slot->color >> 8) & 0xffU;
            blue = slot->color & 0xffU;
            if (red + green + blue < 80U) red = green = blue = 104U;
            break;
        case SlotStatus::Unassigned: break;
    }
    float brightness = 1.0f;
    if (slot->status == SlotStatus::Thinking || slot->breathing) {
        const uint32_t phase = time_ms % 1600U;
        const float triangle = phase < 800U ? phase / 800.0f
                                            : (1600U - phase) / 800.0f;
        brightness = 0.55f + 0.45f * triangle;
    }
    return scaled_color(red, green, blue, brightness);
}

/* Selects a subdued button background for each semantic Agent state. */
static uint16_t slot_background(SlotStatus status)
{
    switch (status) {
        case SlotStatus::Idle: return TintBlue;
        case SlotStatus::Thinking: return TintCyan;
        case SlotStatus::Complete: return TintGreen;
        case SlotStatus::RequiresInput: return TintAmber;
        case SlotStatus::Error: return TintRed;
        default: return Panel;
    }
}

/* Draws title, selected Agent, and current BLE connection label. */
static void draw_header(Canvas *canvas, const Model *model)
{
    fill_rect(canvas, 0, 0, Width, 30, Panel);
    draw_text(canvas, "CODEX", 7, 8, 2, Text);

    char selected[24] = "SELECT AGENT";
    uint16_t selected_color = Muted;
    if (model->selectedSlot < SlotCount) {
        const Slot *slot = &model->slots[model->selectedSlot];
        snprintf(selected, sizeof(selected), "A%u %s",
                 static_cast<unsigned>(model->selectedSlot) + 1U,
                 slotStatusLabel(slot->status));
        selected_color = slot_accent(slot, 800U);
    }
    draw_text(canvas, selected, 76, 7, 1, Text);
    fill_rect(canvas, 76, 20, 91, 2, selected_color);

    const bool connected = model->connection == Connection::Connected;
    const uint16_t connection_color = connected ? Green
        : model->connection == Connection::Connecting ? Amber
                                                        : Dim;
    rounded_box(canvas, 245, 6, 68, 18, 8, 1, Outline, Background);
    fill_circle(canvas, 256, 15, 4, connection_color);
    draw_text(canvas, connected ? "LIVE" : "PAIR", 266, 12, 1,
              connected ? Text : Muted);
}

/* Draws the three page tabs plus the menu tab and their selection states. */
static void draw_tabs(Canvas *canvas, Page selected, bool menu_open)
{
    static const char *const labels[] = {
        "CONTROL", "AGENTS", "NAVIGATE", "MENU"};
    fill_rect(canvas, 0, TabTop, Width, 1, Dim);
    for (int index = 0; index < 4; ++index) {
        const int x = index * 80;
        const int width = 80;
        const bool is_selected = index == 3 ? menu_open
                                             : !menu_open &&
                                                   static_cast<int>(selected) == index;
        fill_rect(canvas, x, TabTop + 1, width, 29, Panel);
        if (is_selected)
            fill_rect(canvas, x + 8, TabTop + 1, width - 16, 3,
                      Accent);
        centered_text(canvas, labels[index], x + width / 2, 221, 1,
                      is_selected ? Text : Muted);
    }
}

/* Draws the instruction shown when commands need an Agent selection. */
static void draw_select_agent(Canvas *canvas)
{
    rounded_box(canvas, 34, 57, 252, 112, 10, 2, Accent, Panel);
    fill_circle(canvas, 160, 82, 8, Accent);
    centered_text(canvas, "SELECT AN AGENT", 160, 105, 2, Text);
    centered_text(canvas, "OPEN THE AGENTS TAB", 160, 137, 1, Muted);
}

/* Draws the six Codex command buttons and their active gesture state. */
static void draw_control(Canvas *canvas, const Model *model,
                         const Action *active)
{
    static const char *const labels[] = {"FAST", "APPROVE", "DECLINE", "FORK", "MIC", "SEND"};
    static const uint16_t borders[] = {
        Cyan, Green, Red, Amber, Accent, Green};
    static const uint16_t backgrounds[] = {
        TintCyan, TintGreen, TintRed,
        TintAmber, TintBlue, TintGreen};
    if (model->connection == Connection::Connected &&
        model->selectedSlot >= SlotCount) {
        draw_select_agent(canvas);
        return;
    }
    for (int index = 0; index < 6; ++index) {
        const layout::Rect cell = layout::codex_grid::cell(index);
        const int x = cell.x;
        const int y = cell.y;
        button(canvas, x, y, cell.width, cell.height, labels[index],
               index == 4 ? "HOLD" : "TAP",
               borders[index], backgrounds[index],
               action_matches(active, ActionType::Key,
                              buddy::codex::controlKey(index)),
               commandsEnabled(*model));
    }
}

/* Draws all Agent slots with status, selection, and desktop lighting effects. */
static void draw_agents(Canvas *canvas, const Model *model,
                        const Action *active, uint32_t time_ms)
{
    for (int index = 0; index < static_cast<int>(SlotCount); ++index) {
        const layout::Rect cell = layout::codex_grid::cell(index);
        const int x = cell.x;
        const int y = cell.y;
        char label[10];
        char number[3];
        snprintf(label, sizeof(label), "AGENT %d", index + 1);
        snprintf(number, sizeof(number), "%02d", index + 1);
        const Slot *slot = &model->slots[index];
        const uint16_t accent = slot_accent(slot, time_ms);
        const bool enabled = model->connection == Connection::Connected;
        rounded_box(canvas, x, y, 100, 76, 7, 1,
                    enabled ? (model->selectedSlot == index ? Text : Outline)
                            : Dim,
                    action_matches(active, ActionType::Slot, index)
                        ? PanelPressed : slot_background(slot->status));
        fill_rounded_rect(canvas, x + 12, y + 1, 76, 3, 1, accent);
        draw_text(canvas, number, x + 12, y + 9, 1, accent);
        fill_circle(canvas, x + 88, y + 13, 4, accent);
        centered_text(canvas, label, x + 50, y + 28, 2,
                      enabled ? Text : Muted);
        centered_text(canvas, slotStatusLabel(slot->status), x + 50,
                      y + 58, 1, enabled ? accent : Dim);
        if (model->selectedSlot == index)
            fill_circle(canvas, x + 88, y + 13, 2, Text);
    }
}

/* Draws direction and dial controls, or the Agent-selection instruction. */
static void draw_navigate(Canvas *canvas, const Model *model,
                          const Action *active)
{
    const bool enabled = commandsEnabled(*model);
    button(canvas,48,38,64,48,"UP",nullptr,Accent,TintBlue,
           action_matches(active,ActionType::Direction,buddy::codex::Direction::Up),enabled);
    button(canvas,48,150,64,48,"DOWN",nullptr,Accent,TintBlue,
           action_matches(active,ActionType::Direction,buddy::codex::Direction::Down),enabled);
    button(canvas,8,94,64,48,"LEFT",nullptr,Accent,TintBlue,
           action_matches(active,ActionType::Direction,buddy::codex::Direction::Left),enabled);
    button(canvas,88,94,64,48,"RIGHT",nullptr,Accent,TintBlue,
           action_matches(active,ActionType::Direction,buddy::codex::Direction::Right),enabled);
    button(canvas,174,42,62,62,"CCW","STEP",Amber,TintAmber,
           action_matches(active,ActionType::Key,buddy::codex::Key::DialCounterClockwise),enabled);
    button(canvas,246,42,62,62,"CW","STEP",Amber,TintAmber,
           action_matches(active,ActionType::Key,buddy::codex::Key::DialClockwise),enabled);
    button(canvas,174,118,134,78,"DIAL","PRESS",Amber,TintAmber,
           action_matches(active,ActionType::Key,buddy::codex::Key::DialPress),enabled);
}

/* Draws the highest-priority modal overlay over the completed page. */
static void draw_overlay(Canvas *canvas, const Model *model, bool muted)
{
    const Overlay active_overlay = overlay(*model);
    if (active_overlay == Overlay::None) return;
    if (active_overlay == Overlay::Menu) {
        const layout::Rect panel = layout::codex_menu::Panel;
        const layout::Rect mute = layout::codex_menu::Mute;
        const layout::Rect switchMode = layout::codex_menu::SwitchMode;
        const layout::Rect close = layout::codex_menu::Close;
        rounded_box(canvas, panel.x, panel.y, panel.width, panel.height,
                    12, 3, Accent,
                    Background);
        centered_text(canvas, "BUDDY MENU", 160, 50, 2, Text);
        rounded_box(canvas, mute.x, mute.y, mute.width, mute.height, 8, 2,
                    muted ? Dim : Green,
                    muted ? Panel : TintGreen);
        centered_text(canvas, muted ? "SOUND: MUTED" : "SOUND: ON",
                      160, 89, 2, Text);
        rounded_box(canvas, switchMode.x, switchMode.y,
                    switchMode.width, switchMode.height, 8, 2, Accent,
                    TintBlue);
        centered_text(canvas, "SWITCH BUDDY", 160, 132, 2, Text);
        centered_text(canvas, "RESTART TO MODE SELECTOR", 160, 152, 1,
                      Muted);
        rounded_box(canvas, close.x, close.y, close.width, close.height,
                    7, 1, Dim, Panel);
        centered_text(canvas, "CLOSE", 160, 180, 1, Muted);
        return;
    }
    if (active_overlay == Overlay::Connection || active_overlay == Overlay::Listening) {
        const bool listening = active_overlay == Overlay::Listening;
        const char *title = listening ? "LISTENING"
                                      : connectionLabel(model->connection);
        const char *line1 = listening ? "USING MAC MICROPHONE"
            : model->connection == Connection::Connecting ? "STARTING BLE"
                                                            : "PAIR IN CHATGPT";
        const char *line2 = listening ? "RELEASE TO STOP" : "COMMANDS PAUSED";
        const uint16_t border = listening ||
            model->connection == Connection::Connecting ? Accent
                                                          : Dim;
        rounded_box(canvas, 20, 42, 280, 150, 12, 4, border, Background);
        centered_text(canvas, title, 160, 65,
                      strlen(title) > 12 ? 2 : 3, Text);
        centered_text(canvas, line1, 160, 120, 2, Muted);
        centered_text(canvas, line2, 160, 150, 2, Muted);
        return;
    }

    char title[28];
    const unsigned slot = static_cast<unsigned>(overlaySlot(*model)) + 1U;
    const char *line = "TAP TO DISMISS";
    uint16_t border = Amber;
    uint16_t background = TintAmber;
    if (active_overlay == Overlay::Complete) {
        snprintf(title, sizeof(title), "AGENT %u COMPLETE", slot);
        line = "TASK FINISHED  TAP TO DISMISS";
        border = Green;
        background = TintGreen;
    } else if (active_overlay == Overlay::Error) {
        snprintf(title, sizeof(title), "AGENT %u ERROR", slot);
        line = "CHECK CHATGPT  TAP TO DISMISS";
        border = Red;
        background = TintRed;
    } else {
        snprintf(title, sizeof(title), "AGENT %u NEEDS INPUT", slot);
        line = "CHECK MAC  TAP TO DISMISS";
    }
    rounded_box(canvas, 10, 144, 300, 56, 9, 3, border, background);
    centered_text(canvas, title, 160, 155, 2, Text);
    centered_text(canvas, line, 160, 181, 1, Muted);
}

/* Composes background, selected page, tabs, and overlays into a full frame. */
void render(const Model &model, const Action *activeAction,
            std::uint32_t timeMs, std::span<std::uint16_t> pixels,
            bool muted) noexcept
{
    if (pixels.size() < PixelCount) return;
    Canvas canvas = {.pixels = pixels.data()};
    fill_rect(&canvas, 0, 0, Width, Height, Background);
    draw_header(&canvas, &model);
    switch (model.page) {
        case Page::Control: draw_control(&canvas, &model, activeAction); break;
        case Page::Agents: draw_agents(&canvas, &model, activeAction, timeMs); break;
        case Page::Navigate: draw_navigate(&canvas, &model, activeAction); break;
    }
    draw_tabs(&canvas, model.page, model.menuOpen);
    draw_overlay(&canvas, &model, muted);
}

}  // namespace buddy::codex::ui
