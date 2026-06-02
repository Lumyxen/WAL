#include "wal/ui.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace wal::ui {
namespace {

struct FcPatternDeleter {
    void operator()(FcPattern* pattern) const
    {
        if (pattern != nullptr) {
            FcPatternDestroy(pattern);
        }
    }
};

struct FontResources {
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    FontResources()
    {
        if (FT_Init_FreeType(&library) != 0) {
            return;
        }

        FcInit();
        std::unique_ptr<FcPattern, FcPatternDeleter> pattern(FcNameParse(reinterpret_cast<const FcChar8*>("sans")));
        if (pattern == nullptr) {
            return;
        }
        FcConfigSubstitute(nullptr, pattern.get(), FcMatchPattern);
        FcDefaultSubstitute(pattern.get());

        FcResult result = FcResultNoMatch;
        std::unique_ptr<FcPattern, FcPatternDeleter> match(FcFontMatch(nullptr, pattern.get(), &result));
        FcChar8* file = nullptr;
        if (match == nullptr || FcPatternGetString(match.get(), FC_FILE, 0, &file) != FcResultMatch) {
            return;
        }

        FT_New_Face(library, reinterpret_cast<const char*>(file), 0, &face);
    }

    ~FontResources()
    {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }
};

[[nodiscard]] FontResources& fontResources()
{
    static FontResources resources;
    return resources;
}

[[nodiscard]] bool setFontSize(FT_Face face, float size)
{
    const auto pixelSize = static_cast<FT_UInt>(std::max(std::round(size), 1.0f));
    return FT_Set_Pixel_Sizes(face, 0, pixelSize) == 0;
}

struct TextVerticalMetrics {
    float baseline = 0.0f;
    float inkTop = 0.0f;
    float inkBottom = 0.0f;
};

[[nodiscard]] TextVerticalMetrics textVerticalMetrics(Rect bounds, TextStyle style)
{
    auto& font = fontResources();
    if (font.face == nullptr || !setFontSize(font.face, style.size)) {
        const float fallbackTop = bounds.y + (bounds.height - style.size) * 0.5f;
        return {
            .baseline = fallbackTop + style.size,
            .inkTop = fallbackTop,
            .inkBottom = fallbackTop + style.size,
        };
    }

    float minY = 0.0f;
    float maxY = style.size;
    bool hasInk = false;
    constexpr std::string_view representativeGlyphs = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (const unsigned char character : representativeGlyphs) {
        if (FT_Load_Char(font.face, character, FT_LOAD_RENDER) != 0) {
            continue;
        }

        const auto* glyph = font.face->glyph;
        if (glyph->bitmap.rows == 0) {
            continue;
        }

        const float glyphTop = -static_cast<float>(glyph->bitmap_top);
        const float glyphBottom = glyphTop + static_cast<float>(glyph->bitmap.rows);
        if (!hasInk) {
            minY = glyphTop;
            maxY = glyphBottom;
            hasInk = true;
        } else {
            minY = std::min(minY, glyphTop);
            maxY = std::max(maxY, glyphBottom);
        }
    }

    if (!hasInk) {
        minY = -style.size * 0.78f;
        maxY = style.size * 0.22f;
    }

    const float inkHeight = maxY - minY;
    const float baseline = bounds.y + (bounds.height - inkHeight) * 0.5f - minY;
    return {
        .baseline = baseline,
        .inkTop = baseline + minY,
        .inkBottom = baseline + maxY,
    };
}

} // namespace

float textWidth(std::string_view value, TextStyle style, size_t endIndex)
{
    auto& font = fontResources();
    if (font.face == nullptr || !setFontSize(font.face, style.size)) {
        return 0.0f;
    }

    float width = 0.0f;
    const size_t clippedEnd = std::min(endIndex, value.size());
    for (size_t i = 0; i < clippedEnd; ++i) {
        if (FT_Load_Char(font.face, static_cast<unsigned char>(value[i]), FT_LOAD_DEFAULT) == 0) {
            width += static_cast<float>(font.face->glyph->advance.x >> 6);
        }
    }
    return width;
}

size_t textIndexAtOffset(std::string_view value, float offset, TextStyle style)
{
    if (offset <= 0.0f) {
        return 0;
    }

    auto& font = fontResources();
    if (font.face == nullptr || !setFontSize(font.face, style.size)) {
        return 0;
    }

    float x = 0.0f;
    for (size_t i = 0; i < value.size(); ++i) {
        float advance = 0.0f;
        if (FT_Load_Char(font.face, static_cast<unsigned char>(value[i]), FT_LOAD_DEFAULT) == 0) {
            advance = static_cast<float>(font.face->glyph->advance.x >> 6);
        }
        if (offset < x + advance * 0.5f) {
            return i;
        }
        x += advance;
    }
    return value.size();
}

Canvas::Canvas(float width, float height)
    : width_(std::max(width, 1.0f)),
      height_(std::max(height, 1.0f))
{
}

void Canvas::clear()
{
    vertices_.clear();
}

void Canvas::box(Rect rect, BoxStyle style)
{
    fillRect(rect, style.fill);
    if (style.borderWidth > 0.0f) {
        strokeRect(rect, style.borderWidth, style.border);
    }
}

void Canvas::line(Vec2 from, Vec2 to, float thickness, Color color)
{
    const float half = thickness * 0.5f;
    if (std::abs(from.x - to.x) >= std::abs(from.y - to.y)) {
        const float left = std::min(from.x, to.x);
        const float right = std::max(from.x, to.x);
        fillRect({left, from.y - half, right - left, thickness}, color);
    } else {
        const float top = std::min(from.y, to.y);
        const float bottom = std::max(from.y, to.y);
        fillRect({from.x - half, top, thickness, bottom - top}, color);
    }
}

void Canvas::text(Rect bounds, std::string_view value, TextStyle style)
{
    if (value.empty()) {
        return;
    }

    auto& font = fontResources();
    if (font.face == nullptr || !setFontSize(font.face, style.size)) {
        return;
    }

    const float baseline = textVerticalMetrics(bounds, style).baseline;
    float x = bounds.x;
    const float maxX = bounds.x + bounds.width;

    for (const unsigned char character : value) {
        if (FT_Load_Char(font.face, character, FT_LOAD_RENDER) != 0) {
            continue;
        }

        const auto* glyph = font.face->glyph;
        const float advance = static_cast<float>(glyph->advance.x >> 6);
        if (x + advance > maxX) {
            break;
        }

        const FT_Bitmap& bitmap = glyph->bitmap;
        const float glyphX = x + static_cast<float>(glyph->bitmap_left);
        const float glyphY = baseline - static_cast<float>(glyph->bitmap_top);
        for (uint32_t row = 0; row < bitmap.rows; ++row) {
            uint32_t column = 0;
            while (column < bitmap.width) {
                const uint8_t alpha = bitmap.buffer[row * bitmap.pitch + column];
                if (alpha == 0) {
                    ++column;
                    continue;
                }

                uint32_t spanWidth = 1;
                while (column + spanWidth < bitmap.width &&
                       bitmap.buffer[row * bitmap.pitch + column + spanWidth] == alpha) {
                    ++spanWidth;
                }

                Color glyphColor = style.color;
                glyphColor.a *= static_cast<float>(alpha) / 255.0f;
                fillRect(
                    {
                        glyphX + static_cast<float>(column),
                        glyphY + static_cast<float>(row),
                        static_cast<float>(spanWidth),
                        1.0f,
                    },
                    glyphColor
                );
                column += spanWidth;
            }
        }

        x += advance;
    }
}

void Canvas::textField(
    Rect rect,
    std::string_view value,
    bool focused,
    TextFieldStyle style,
    size_t cursorIndex,
    size_t selectionAnchor
)
{
    box(rect, style.box);

    cursorIndex = std::min(cursorIndex, value.size());
    selectionAnchor = std::min(selectionAnchor, value.size());
    const Rect textBounds{rect.x + 14.0f, rect.y, rect.width - 28.0f, rect.height};
    const float selectionHeight = std::min(rect.height - 4.0f, style.text.size + 2.0f);
    const float selectionY = rect.y + (rect.height - selectionHeight) * 0.5f;
    const float cursorHeight = std::min(rect.height - 6.0f, style.text.size);
    const float cursorY = rect.y + (rect.height - cursorHeight) * 0.5f;
    const bool hasSelection = cursorIndex != selectionAnchor;

    if (hasSelection) {
        const size_t selectionStart = std::min(cursorIndex, selectionAnchor);
        const size_t selectionEnd = std::max(cursorIndex, selectionAnchor);
        const float selectionX = textBounds.x + textWidth(value, style.text, selectionStart);
        const float selectionWidth = textWidth(value, style.text, selectionEnd) - textWidth(value, style.text, selectionStart);
        fillRect({selectionX, selectionY, selectionWidth, selectionHeight}, style.selection);
    }

    text(textBounds, value, style.text);

    if (focused && !hasSelection) {
        const float cursorX = std::min(rect.x + rect.width - 15.0f, textBounds.x + textWidth(value, style.text, cursorIndex) + 2.0f);
        line({cursorX, cursorY}, {cursorX, cursorY + cursorHeight}, 2.0f, style.cursor);
    }
}

void Canvas::list(Rect rect, std::span<const std::string_view> items, uint32_t selectedIndex, ListStyle style)
{
    box(rect, style.container);

    float y = rect.y + style.padding;
    const float itemWidth = rect.width - style.padding * 2.0f;
    for (uint32_t i = 0; i < items.size(); ++i) {
        if (y + style.itemHeight > rect.y + rect.height - style.padding) {
            break;
        }

        auto itemStyle = style.item;
        if (i == selectedIndex) {
            itemStyle.fill = style.selectedFill;
            itemStyle.border = Color::srgb(0x7f, 0xbb, 0xca);
        }

        const Rect itemRect{rect.x + style.padding, y, itemWidth, style.itemHeight};
        box(itemRect, itemStyle);
        text({itemRect.x + 12.0f, itemRect.y, itemRect.width - 24.0f, itemRect.height}, items[i], {});

        y += style.itemHeight + style.itemGap;
    }
}

std::span<const Vertex> Canvas::vertices() const
{
    return vertices_;
}

Vec2 Canvas::toClipSpace(Vec2 point) const
{
    return {
        (point.x / width_) * 2.0f - 1.0f,
        (point.y / height_) * 2.0f - 1.0f,
    };
}

void Canvas::fillRect(Rect rect, Color color)
{
    const auto topLeft = toClipSpace({rect.x, rect.y});
    const auto topRight = toClipSpace({rect.x + rect.width, rect.y});
    const auto bottomRight = toClipSpace({rect.x + rect.width, rect.y + rect.height});
    const auto bottomLeft = toClipSpace({rect.x, rect.y + rect.height});

    vertices_.insert(
        vertices_.end(),
        {
            {topLeft, color},
            {bottomLeft, color},
            {bottomRight, color},
            {topLeft, color},
            {bottomRight, color},
            {topRight, color},
        }
    );
}

void Canvas::strokeRect(Rect rect, float thickness, Color color)
{
    fillRect({rect.x, rect.y, rect.width, thickness}, color);
    fillRect({rect.x, rect.y + rect.height - thickness, rect.width, thickness}, color);
    fillRect({rect.x, rect.y, thickness, rect.height}, color);
    fillRect({rect.x + rect.width - thickness, rect.y, thickness, rect.height}, color);
}

} // namespace wal::ui
