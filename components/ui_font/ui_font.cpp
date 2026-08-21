#include "ui_font.hpp"

#include <cstddef>

#include "lvgl.h"

namespace buddy::font {
namespace {

const lv_font_t *fontForScale(int scale) noexcept
{
    if (scale <= 1) return &lv_font_montserrat_10;
    if (scale == 2) return &lv_font_montserrat_14;
    if (scale == 3) return &lv_font_montserrat_20;
    return &lv_font_montserrat_28;
}

bool glyphDescriptor(const lv_font_t *font, unsigned char letter,
                     unsigned char next, lv_font_glyph_dsc_t &glyph) noexcept
{
    glyph = {};
    if (!font->get_glyph_dsc(font, &glyph, letter, next)) return false;
    glyph.resolved_font = font;
    return true;
}

std::uint8_t alphaAt(const std::uint8_t *bitmap,
                     const lv_font_glyph_dsc_t &glyph, int x, int y) noexcept
{
    const unsigned bits = static_cast<unsigned>(glyph.format);
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8) return 0;
    const std::size_t bit = glyph.stride != 0
        ? static_cast<std::size_t>(y) * glyph.stride * 8U + x * bits
        : (static_cast<std::size_t>(y) * glyph.box_w + x) * bits;
    const unsigned shift = 8U - bits - static_cast<unsigned>(bit & 7U);
    const unsigned mask = (1U << bits) - 1U;
    const unsigned sample = (bitmap[bit / 8U] >> shift) & mask;
    return static_cast<std::uint8_t>(sample * 255U / mask);
}

std::uint16_t blend(std::uint16_t background, std::uint16_t foreground,
                    std::uint8_t alpha) noexcept
{
    const unsigned inverse = 255U - alpha;
    const unsigned red = (((foreground >> 11) & 0x1fU) * alpha +
                          ((background >> 11) & 0x1fU) * inverse + 127U) / 255U;
    const unsigned green = (((foreground >> 5) & 0x3fU) * alpha +
                            ((background >> 5) & 0x3fU) * inverse + 127U) / 255U;
    const unsigned blue = ((foreground & 0x1fU) * alpha +
                           (background & 0x1fU) * inverse + 127U) / 255U;
    return static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
}

}  // namespace

int textWidth(std::string_view value, int scale) noexcept
{
    const lv_font_t *font = fontForScale(scale);
    int width = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        lv_font_glyph_dsc_t glyph;
        const auto letter = static_cast<unsigned char>(value[index]);
        const auto next = index + 1 < value.size()
            ? static_cast<unsigned char>(value[index + 1]) : 0;
        if (glyphDescriptor(font, letter, next, glyph)) width += glyph.adv_w;
    }
    return width;
}

void draw(std::span<std::uint16_t> pixels, int canvasWidth, int canvasHeight,
          std::string_view value, int x, int y, int scale,
          std::uint16_t color) noexcept
{
    if (canvasWidth <= 0 || canvasHeight <= 0 ||
        pixels.size() < static_cast<std::size_t>(canvasWidth * canvasHeight))
        return;
    const lv_font_t *font = fontForScale(scale);
    for (std::size_t index = 0; index < value.size(); ++index) {
        lv_font_glyph_dsc_t glyph;
        const auto letter = static_cast<unsigned char>(value[index]);
        const auto next = index + 1 < value.size()
            ? static_cast<unsigned char>(value[index + 1]) : 0;
        if (!glyphDescriptor(font, letter, next, glyph)) continue;
        glyph.req_raw_bitmap = 1;
        const auto *bitmap = static_cast<const std::uint8_t *>(
            font->get_glyph_bitmap(&glyph, nullptr));
        const int glyphX = x + glyph.ofs_x;
        const int glyphY = y + font->line_height - font->base_line -
                           glyph.box_h - glyph.ofs_y;
        if (bitmap != nullptr) {
            for (int row = 0; row < glyph.box_h; ++row) {
                const int targetY = glyphY + row;
                if (targetY < 0 || targetY >= canvasHeight) continue;
                for (int column = 0; column < glyph.box_w; ++column) {
                    const int targetX = glyphX + column;
                    if (targetX < 0 || targetX >= canvasWidth) continue;
                    const std::uint8_t alpha = alphaAt(bitmap, glyph, column, row);
                    if (alpha == 0) continue;
                    auto &pixel = pixels[targetY * canvasWidth + targetX];
                    pixel = blend(pixel, color, alpha);
                }
            }
        }
        x += glyph.adv_w;
    }
}

}  // namespace buddy::font
