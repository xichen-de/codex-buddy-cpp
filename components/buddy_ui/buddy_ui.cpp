#include "buddy_ui.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace buddy::display {

constexpr std::uint16_t rgb565(unsigned red, unsigned green,
                               unsigned blue) noexcept
{
    return static_cast<std::uint16_t>(
        ((red & 0xf8U) << 8) | ((green & 0xfcU) << 3) | (blue >> 3));
}

constexpr auto Background = rgb565(9, 13, 22);
constexpr auto Panel = rgb565(22, 30, 45);
constexpr auto Text = rgb565(242, 246, 252);
constexpr auto Muted = rgb565(146, 159, 180);
constexpr auto Dim = rgb565(76, 89, 108);
constexpr auto CodexColor = rgb565(45, 145, 235);
constexpr auto ClaudeColor = rgb565(215, 118, 76);
constexpr auto Owl = rgb565(184, 125, 73);
constexpr auto OwlLight = rgb565(233, 195, 133);
constexpr auto Amber = rgb565(245, 170, 40);
constexpr auto Green = rgb565(35, 190, 100);
constexpr auto Red = rgb565(230, 65, 75);
constexpr auto Cyan = rgb565(45, 190, 225);
constexpr int TabTop = 210;

struct Canvas {
    std::uint16_t *pixels{};
};

/* Tests a point against a left/top-inclusive, right/bottom-exclusive rectangle. */
static bool in_rect(uint16_t x, uint16_t y, unsigned left, unsigned top,
                    unsigned width, unsigned height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

/* Fills a clipped rectangle in the framebuffer-backed canvas. */
static void rect(Canvas *canvas, int x, int y, int width, int height,
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

/* Rasterizes a filled circle into the canvas. */
static void circle(Canvas *canvas, int center_x, int center_y, int radius,
                   uint16_t color)
{
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                rect(canvas, center_x + x, center_y + y, 1, 1, color);
}

/* Rasterizes a one-pixel line using an integer error accumulator. */
static void line(Canvas *canvas, int x0, int y0, int x1, int y1,
                 int thickness, uint16_t color)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        rect(canvas, x0 - thickness / 2, y0 - thickness / 2,
             thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

/* Builds a filled rounded rectangle from bands and corner circles. */
static void rounded(Canvas *canvas, int x, int y, int width, int height,
                    int radius, uint16_t color)
{
    rect(canvas, x + radius, y, width - radius * 2, height, color);
    rect(canvas, x, y + radius, width, height - radius * 2, color);
    circle(canvas, x + radius, y + radius, radius, color);
    circle(canvas, x + width - radius - 1, y + radius, radius, color);
    circle(canvas, x + radius, y + height - radius - 1, radius, color);
    circle(canvas, x + width - radius - 1, y + height - radius - 1,
           radius, color);
}

/* Draws a rounded border with a contrasting inset fill. */
static void box(Canvas *canvas, int x, int y, int width, int height,
                uint16_t border, uint16_t fill)
{
    rounded(canvas, x, y, width, height, 8, border);
    rounded(canvas, x + 2, y + 2, width - 4, height - 4, 6, fill);
}

/* Returns the five-column bitmap for a supported character or fallback glyph. */
static const uint8_t *glyph(char value)
{
    static const uint8_t blank[5] = {0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
    };
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t percent[5] = {0x63,0x13,0x08,0x64,0x63};
    static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t exclamation[5] = {0x00,0x00,0x5f,0x00,0x00};
    if (value >= '0' && value <= '9') return digits[value - '0'];
    value = static_cast<char>(
        toupper(static_cast<unsigned char>(value)));
    if (value >= 'A' && value <= 'Z') return letters[value - 'A'];
    if (value == '-') return dash;
    if (value == '/') return slash;
    if (value == ':') return colon;
    if (value == '%') return percent;
    if (value == '.') return dot;
    if (value == '!') return exclamation;
    return blank;
}

/* Calculates pixel width for fixed 5x7 glyphs plus spacing. */
static int text_width(const char *text, int scale)
{
    return text == nullptr || text[0] == '\0' ? 0
        : static_cast<int>(strlen(text)) * 6 * scale - scale;
}

/* Rasterizes scaled bitmap text at the requested top-left coordinate. */
static void text(Canvas *canvas, const char *value, int x, int y, int scale,
                 uint16_t color)
{
    if (value == nullptr) return;
    for (; *value != '\0'; ++value, x += 6 * scale) {
        const uint8_t *columns = glyph(*value);
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if ((columns[column] & (1U << row)) != 0)
                    rect(canvas, x + column * scale, y + row * scale,
                         scale, scale, color);
    }
}

/* Centers bitmap text horizontally around a requested coordinate. */
static void centered(Canvas *canvas, const char *value, int center_x, int y,
                     int scale, uint16_t color)
{
    text(canvas, value, center_x - text_width(value, scale) / 2, y, scale,
         color);
}

/* Truncates text to a pixel width before drawing it into a bounded row. */
static void clipped_text(Canvas *canvas, const char *source, int x, int y,
                         size_t characters, uint16_t color)
{
    char buffer[48];
    if (characters >= sizeof(buffer)) characters = sizeof(buffer) - 1;
    size_t length = source == nullptr ? 0 : strlen(source);
    if (length > characters) length = characters;
    if (length > 0) memcpy(buffer, source, length);
    buffer[length] = '\0';
    text(canvas, buffer, x, y, 1, color);
}

/* Draws the compact owl mark used by the startup selector. */
static void draw_mini_owl(Canvas *canvas, int x, int y, uint16_t accent)
{
    circle(canvas, x, y, 15, Owl);
    circle(canvas, x - 7, y - 3, 7, OwlLight);
    circle(canvas, x + 7, y - 3, 7, OwlLight);
    circle(canvas, x - 7, y - 3, 3, Background);
    circle(canvas, x + 7, y - 3, 3, Background);
    rect(canvas, x - 1, y + 4, 3, 5, accent);
}

/* Composes the complete Codex-versus-Claude startup selector. */
void renderSelector(std::span<std::uint16_t> pixels) noexcept
{
    if (pixels.size() < PixelCount) return;
    Canvas canvas = {.pixels = pixels.data()};
    rect(&canvas, 0, 0, Width, Height, Background);
    centered(&canvas, "CHOOSE YOUR BUDDY", 160, 18, 2, Text);
    centered(&canvas, "BLUETOOTH STARTS AFTER SELECTION", 160, 43, 1, Muted);

    box(&canvas, 12, 66, 142, 135, CodexColor, Panel);
    centered(&canvas, "CODEX", 83, 84, 3, Text);
    centered(&canvas, "6 AGENTS", 83, 124, 2, CodexColor);
    centered(&canvas, "CHATGPT", 83, 158, 1, Muted);
    centered(&canvas, "TAP TO START", 83, 179, 1, Text);

    box(&canvas, 166, 66, 142, 135, ClaudeColor, Panel);
    draw_mini_owl(&canvas, 237, 99, Amber);
    centered(&canvas, "CLAUDE", 237, 124, 2, Text);
    centered(&canvas, "OWL BUDDY", 237, 151, 1, ClaudeColor);
    centered(&canvas, "TAP TO START", 237, 179, 1, Text);
    centered(&canvas, "SELECT A MODE ON EVERY STARTUP", 160, 219, 1, Dim);
}

/* Hit-tests the two large selector cards. */
Mode selectorHit(std::uint16_t x, std::uint16_t y) noexcept
{
    if (in_rect(x, y, 12, 66, 142, 135)) return Mode::Codex;
    if (in_rect(x, y, 166, 66, 142, 135)) return Mode::Claude;
    return Mode::None;
}

/* Converts owl state to the short descriptive label below the illustration. */
static const char *pet_label(buddy::claude::PetState state)
{
    static const char *const labels[] = {
        "SLEEPING", "READY", "WORKING", "NEEDS YOU", "HOORAY", "THANK YOU",
        "DIZZY"};
    return state >= buddy::claude::PetState::Sleep && state <= buddy::claude::PetState::Dizzy
        ? labels[static_cast<std::size_t>(state)] : "READY";
}

/* Selects the owl accent color associated with its semantic state. */
static uint16_t pet_accent(buddy::claude::PetState state)
{
    switch (state) {
        case buddy::claude::PetState::Busy: return Cyan;
        case buddy::claude::PetState::Attention: return Amber;
        case buddy::claude::PetState::Celebrate: return Green;
        case buddy::claude::PetState::Heart: return Red;
        case buddy::claude::PetState::Dizzy: return Amber;
        default: return ClaudeColor;
    }
}

/* Draws the animated owl pose for the current state and frame time. */
static void draw_owl(Canvas *canvas, buddy::claude::PetState state,
                     uint32_t now_ms)
{
    const unsigned frame = (now_ms / 120U) % 12U;
    const int bob = state == buddy::claude::PetState::Busy
        ? static_cast<int>(frame % 3U) - 1
        : state == buddy::claude::PetState::Celebrate ? (frame < 6U ? -4 : 1) : 0;
    const int cx = 160;
    const int cy = 104 - bob;
    const uint16_t accent = pet_accent(state);
    const int wing = state == buddy::claude::PetState::Celebrate
        ? (frame % 4U < 2U ? 10 : -2) : state == buddy::claude::PetState::Busy ? 3 : 0;

    /* Ears, body, and animated wings form a deliberately chunky owl that
       remains readable from across a desk. */
    line(canvas, cx - 29, cy - 31, cx - 38, cy - 53, 15, Owl);
    line(canvas, cx + 29, cy - 31, cx + 38, cy - 53, 15, Owl);
    circle(canvas, cx, cy + 9, 47, Owl);
    line(canvas, cx - 38, cy + 4, cx - 55, cy + 26 - wing, 12, Owl);
    line(canvas, cx + 38, cy + 4, cx + 55, cy + 26 - wing, 12, Owl);
    circle(canvas, cx - 23, cy - 16, 25, OwlLight);
    circle(canvas, cx + 23, cy - 16, 25, OwlLight);

    const bool blink = state == buddy::claude::PetState::Idle && now_ms % 3600U < 180U;
    if (state == buddy::claude::PetState::Sleep || blink) {
        rect(canvas, cx - 34, cy - 16, 22, 3, Background);
        rect(canvas, cx + 12, cy - 16, 22, 3, Background);
    } else if (state == buddy::claude::PetState::Dizzy) {
        line(canvas, cx - 31, cy - 24, cx - 15, cy - 8, 4, Background);
        line(canvas, cx - 15, cy - 24, cx - 31, cy - 8, 4, Background);
        line(canvas, cx + 15, cy - 24, cx + 31, cy - 8, 4, Background);
        line(canvas, cx + 31, cy - 24, cx + 15, cy - 8, 4, Background);
    } else {
        const int pupil = state == buddy::claude::PetState::Busy
            ? static_cast<int>((now_ms / 240U) % 7U) - 3 : 0;
        circle(canvas, cx - 23, cy - 16, 9, Background);
        circle(canvas, cx + 23, cy - 16, 9, Background);
        circle(canvas, cx - 23 + pupil, cy - 16, 3, Text);
        circle(canvas, cx + 23 + pupil, cy - 16, 3, Text);
    }
    rect(canvas, cx - 4, cy - 4, 9, 10, accent);
    rect(canvas, cx - 28, cy + 18, 56, 3, OwlLight);
    rect(canvas, cx - 20, cy + 30, 40, 3, OwlLight);
    line(canvas, cx - 17, cy + 50, cx - 5, cy + 43, 3, OwlLight);
    line(canvas, cx + 17, cy + 50, cx + 5, cy + 43, 3, OwlLight);
    if (state == buddy::claude::PetState::Attention) {
        centered(canvas, "!", 222, 60, 3, Amber);
        line(canvas, 215, 48, 209, 39, 3, Amber);
        line(canvas, 229, 48, 235, 39, 3, Amber);
    } else if (state == buddy::claude::PetState::Heart) {
        const int pulse = frame % 4U == 0 ? 2 : 0;
        circle(canvas, 218, 66, 7 + pulse, Red);
        circle(canvas, 230, 66, 7 + pulse, Red);
        rect(canvas, 218 - pulse, 67, 13 + pulse * 2, 9 + pulse, Red);
        rect(canvas, 221, 76, 7, 5 + pulse, Red);
    } else if (state == buddy::claude::PetState::Sleep) {
        text(canvas, "Z", 215, 56, 2, Cyan);
        text(canvas, "Z", 234, 43, 1, Cyan);
    } else if (state == buddy::claude::PetState::Celebrate) {
        const int drift = static_cast<int>(frame % 5U);
        rect(canvas, 91 + drift, 57, 5, 5, Green);
        rect(canvas, 225 - drift, 89, 5, 5, Cyan);
        rect(canvas, 105, 118 + drift, 5, 5, Amber);
        line(canvas, 222, 47, 228, 39, 3, Red);
    } else if (state == buddy::claude::PetState::Dizzy) {
        circle(canvas, cx, cy - 61, 4, Amber);
        circle(canvas, cx - 18 + static_cast<int>(frame % 5U), cy - 57, 3, Cyan);
        circle(canvas, cx + 19 - static_cast<int>(frame % 5U), cy - 55, 3, Green);
    }
}

/* Draws the Claude title and live/secure connection badges. */
static void draw_header(Canvas *canvas, const buddy::claude::Model *model,
                        bool battery_known, uint8_t battery_percent,
                        bool charging)
{
    rect(canvas, 0, 0, Width, 30, Panel);
    text(canvas, "CLAUDE", 7, 8, 2, Text);
    draw_mini_owl(canvas, 88, 15, ClaudeColor);
    const bool connected = model->connection == buddy::claude::Connection::Connected;
    circle(canvas, 239, 15, 4, connected ? Green : Dim);
    text(canvas, connected ? "LIVE" : "PAIR", 249, 12, 1,
         connected ? Text : Muted);
    if (battery_known) {
        char battery[8];
        snprintf(battery, sizeof(battery), "%s%u%%", charging ? "+" : "",
                 static_cast<unsigned>(battery_percent));
        text(canvas, battery, 313 - text_width(battery, 1), 12, 1, Muted);
    }
}

/* Draws and highlights the four Claude page tabs. */
static void draw_tabs(Canvas *canvas, buddy::claude::Page selected)
{
    static const char *const labels[] = {"OWL", "ACT", "CLOCK", "INFO"};
    rect(canvas, 0, TabTop, Width, 1, Dim);
    for (int index = 0; index < 4; ++index) {
        const int x = index * 80;
        const int width = 80;
        rect(canvas, x, TabTop + 1, width, 29, Panel);
        if (static_cast<int>(selected) == index)
            rect(canvas, x + 12, TabTop + 1, width - 24, 3, ClaudeColor);
        centered(canvas, labels[index], x + width / 2, 221, 1,
                 static_cast<int>(selected) == index ? Text : Muted);
    }
}

/* Draws the main animated owl page and session summary. */
static void draw_pet_page(Canvas *canvas, const buddy::claude::Model *model,
                          uint32_t now_ms)
{
    const buddy::claude::PetState state = buddy::claude::petState(*model, now_ms);
    draw_owl(canvas, state, now_ms);
    centered(canvas, pet_label(state), 160, 163, 2, pet_accent(state));
    if (model->message[0] != '\0')
        clipped_text(canvas, model->message.data(), 18, 191, 47, Muted);
    else
        centered(canvas, model->connection == buddy::claude::Connection::Connected
            ? "WAITING FOR CLAUDE" : "OPEN HARDWARE BUDDY", 160, 191, 1, Muted);
}

/* Draws the scrollable activity list and token totals. */
static void draw_activity_page(Canvas *canvas, const buddy::claude::Model *model)
{
    char counts[40];
    snprintf(counts, sizeof(counts), "SESSIONS %u  RUNNING %u  WAITING %u",
             static_cast<unsigned>(model->totalSessions),
             static_cast<unsigned>(model->runningSessions),
             static_cast<unsigned>(model->waitingSessions));
    clipped_text(canvas, counts, 10, 42, 50, Text);
    char tokens[40];
    snprintf(tokens, sizeof(tokens), "TOKENS TODAY %llu  TOTAL %llu",
             (unsigned long long)model->tokensToday,
             (unsigned long long)model->tokens);
    clipped_text(canvas, tokens, 10, 60, 50, ClaudeColor);
    rect(canvas, 10, 78, 300, 1, Dim);
    const size_t count = buddy::claude::activityCount(*model);
    for (size_t row = 0; row < buddy::claude::ModelActivityVisibleCount; ++row) {
        const size_t index = model->activityOffset + row;
        if (index >= count) break;
        if (model->entries[index][0] == '\0') continue;
        char number[4];
        snprintf(number, sizeof(number), "%u", static_cast<unsigned>(index) + 1U);
        text(canvas, number, 12, 88 + static_cast<int>(row) * 29, 1, ClaudeColor);
        clipped_text(canvas, model->entries[index].data(), 30,
                     88 + static_cast<int>(row) * 29, 40, Text);
    }
    const bool can_up = model->activityOffset > 0;
    const bool can_down = model->activityOffset +
        buddy::claude::ModelActivityVisibleCount < count;
    box(canvas, 280, 84, 30, 40, can_up ? ClaudeColor : Dim, Panel);
    centered(canvas, "UP", 295, 98, 1, can_up ? Text : Dim);
    box(canvas, 280, 157, 30, 40, can_down ? ClaudeColor : Dim, Panel);
    centered(canvas, "DN", 295, 171, 1, can_down ? Text : Dim);
    char position[12];
    snprintf(position, sizeof(position), "%u/%u",
             count == 0 ? 0U
                        : static_cast<unsigned>(model->activityOffset) + 1U,
             static_cast<unsigned>(count));
    centered(canvas, position, 295, 137, 1, Muted);
}

/* Converts days since Unix epoch to a Gregorian civil date. */
static void civil_date(int64_t days, int *year, unsigned *month,
                       unsigned *day)
{
    int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp < 10 ? mp + 3 : mp - 9;
    y += *month <= 2;
    *year = y;
}

/* Draws synchronized local date/time or a waiting-for-sync message. */
static void draw_clock_page(Canvas *canvas, const buddy::claude::Model *model,
                            uint32_t now_ms)
{
    int64_t local = 0;
    if (!buddy::claude::clockSeconds(*model, now_ms, local) || local < 0) {
        draw_mini_owl(canvas, 160, 87, ClaudeColor);
        centered(canvas, "WAITING FOR TIME", 160, 121, 2, Text);
        centered(canvas, "CLAUDE WILL SYNC THE CLOCK", 160, 151, 1, Muted);
        return;
    }
    const int64_t days = local / 86400;
    const unsigned seconds = static_cast<unsigned>(local % 86400);
    const unsigned hour = seconds / 3600;
    const unsigned minute = seconds / 60 % 60;
    const unsigned second = seconds % 60;
    int year;
    unsigned month;
    unsigned day;
    civil_date(days, &year, &month, &day);
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0) weekday += 7;
    char clock[6];
    snprintf(clock, sizeof(clock), "%02u:%02u", hour % 24U, minute);
    centered(canvas, clock, 160, 65, 5, Text);
    char seconds_text[5];
    snprintf(seconds_text, sizeof(seconds_text), ":%02u", second);
    text(canvas, seconds_text, 258, 93, 2, ClaudeColor);
    char date[32];
    snprintf(date, sizeof(date), "%s  %04d-%02u-%02u",
             weekdays[weekday], year, month, day);
    centered(canvas, date, 160, 132, 2, OwlLight);
    draw_mini_owl(canvas, 160, 178, ClaudeColor);
}

/* Draws device identity, decision statistics, and the mode-switch control. */
static void draw_info_page(Canvas *canvas, const buddy::claude::Model *model)
{
    text(canvas, "DEVICE", 14, 44, 1, Muted);
    clipped_text(canvas, model->deviceName.data(), 95, 44, 32, Text);
    text(canvas, "OWNER", 14, 68, 1, Muted);
    clipped_text(canvas, model->owner[0] ? model->owner.data() : "UNKNOWN",
                 95, 68, 32, Text);
    text(canvas, "BLUETOOTH", 14, 92, 1, Muted);
    text(canvas, model->connection == buddy::claude::Connection::Connected
        ? "CONNECTED" : "WAITING", 95, 92, 1,
        model->connection == buddy::claude::Connection::Connected ? Green : Amber);
    box(canvas, 40, 137, 240, 50, ClaudeColor, Panel);
    centered(canvas, "SWITCH BUDDY", 160, 151, 2, Text);
    centered(canvas, "RESTART TO MODE SELECTOR", 160, 174, 1, Muted);
}

/* Draws the modal permission prompt with approve and deny buttons. */
static void draw_approval(Canvas *canvas, const buddy::claude::Model *model)
{
    rect(canvas, 0, 30, Width, 180, Background);
    centered(canvas, "PERMISSION REQUEST", 160, 40, 2, Amber);
    centered(canvas, model->promptTool[0] ? model->promptTool.data() : "TOOL",
             160, 70, 2, Text);
    clipped_text(canvas, model->promptHint.data(), 14, 102, 48, Muted);
    box(canvas, 12, 143, 143, 55, Green, Panel);
    centered(canvas, "APPROVE", 83, 160, 2, Text);
    box(canvas, 165, 143, 143, 55, Red, Panel);
    centered(canvas, "DENY", 236, 160, 2, Text);
}

/* Draws the modal six-digit BLE pairing passkey. */
static void draw_passkey(Canvas *canvas, uint32_t passkey)
{
    char code[8];
    snprintf(code, sizeof(code), "%06u",
             static_cast<unsigned>(passkey % 1000000U));
    rect(canvas, 0, 30, Width, 180, Background);
    box(canvas, 24, 54, 272, 126, ClaudeColor, Panel);
    centered(canvas, "BLUETOOTH PASSKEY", 160, 72, 2, Text);
    centered(canvas, code, 160, 108, 4, ClaudeColor);
    centered(canvas, "ENTER THIS CODE ON YOUR MAC", 160, 154, 1, Muted);
}

/* Composes the selected Claude page and any higher-priority modal overlay. */
void renderClaude(const claude::Model &model, std::uint32_t nowMs,
                  bool passkeyVisible, std::uint32_t passkey,
                  bool batteryKnown, std::uint8_t batteryPercent,
                  bool charging, std::span<std::uint16_t> pixels) noexcept
{
    if (pixels.size() < PixelCount) return;
    Canvas canvas = {.pixels = pixels.data()};
    rect(&canvas, 0, 0, Width, Height, Background);
    draw_header(&canvas, &model, batteryKnown, batteryPercent, charging);
    if (model.page == claude::Page::Pet) draw_pet_page(&canvas, &model, nowMs);
    else if (model.page == claude::Page::Activity) draw_activity_page(&canvas, &model);
    else if (model.page == claude::Page::Clock) draw_clock_page(&canvas, &model, nowMs);
    else draw_info_page(&canvas, &model);
    draw_tabs(&canvas, model.page);
    if (model.promptActive) draw_approval(&canvas, &model);
    if (passkeyVisible) draw_passkey(&canvas, passkey);
}

/* Hit-tests modal decisions before page-specific and tab controls. */
ClaudeAction claudeHit(const claude::Model &model, std::uint16_t x,
                       std::uint16_t y) noexcept
{
    if (model.promptActive) {
        if (in_rect(x, y, 12, 143, 143, 55)) return ClaudeAction::Approve;
        if (in_rect(x, y, 165, 143, 143, 55)) return ClaudeAction::Deny;
        return ClaudeAction::None;
    }
    if (y >= TabTop) {
        if (x < 80) return ClaudeAction::PagePet;
        if (x < 160) return ClaudeAction::PageActivity;
        if (x < 240) return ClaudeAction::PageClock;
        return ClaudeAction::PageInfo;
    }
    if (model.page == claude::Page::Activity) {
        if (in_rect(x, y, 280, 84, 30, 40))
            return ClaudeAction::ActivityUp;
        if (in_rect(x, y, 280, 157, 30, 40))
            return ClaudeAction::ActivityDown;
    }
    if (model.page == claude::Page::Info && in_rect(x, y, 40, 137, 240, 50))
        return ClaudeAction::SwitchMode;
    return ClaudeAction::None;
}

}  // namespace buddy::display
