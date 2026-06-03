#include "wal/ui.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

struct FcFontSetDeleter {
    void operator()(FcFontSet* set) const
    {
        if (set != nullptr) {
            FcFontSetDestroy(set);
        }
    }
};

struct FontFace {
    std::string path;
    FT_Face face = nullptr;
};

struct FontResources {
    FT_Library library = nullptr;
    std::vector<FontFace> faces;

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
        std::unique_ptr<FcFontSet, FcFontSetDeleter> sorted(FcFontSort(nullptr, pattern.get(), FcTrue, nullptr, &result));
        if (sorted == nullptr) {
            return;
        }

        std::unordered_set<std::string> seenPaths;
        for (int i = 0; i < sorted->nfont; ++i) {
            FcChar8* file = nullptr;
            if (FcPatternGetString(sorted->fonts[i], FC_FILE, 0, &file) != FcResultMatch) {
                continue;
            }

            std::string path = reinterpret_cast<const char*>(file);
            if (seenPaths.insert(path).second) {
                faces.push_back({.path = std::move(path)});
            }
        }
    }

    ~FontResources()
    {
        for (FontFace& fontFace : faces) {
            if (fontFace.face != nullptr) {
                FT_Done_Face(fontFace.face);
            }
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    [[nodiscard]] bool loadFace(size_t index)
    {
        if (index >= faces.size()) {
            return false;
        }
        if (faces[index].face != nullptr) {
            return true;
        }

        return FT_New_Face(library, faces[index].path.c_str(), 0, &faces[index].face) == 0;
    }

    [[nodiscard]] std::pair<FT_Face, uint32_t> faceForCodepoint(uint32_t codepoint)
    {
        static std::unordered_map<uint32_t, uint32_t> faceIndexByCodepoint;
        if (const auto cached = faceIndexByCodepoint.find(codepoint); cached != faceIndexByCodepoint.end()) {
            const uint32_t faceIndex = cached->second;
            if (loadFace(faceIndex)) {
                return {faces[faceIndex].face, faceIndex};
            }
        }

        for (size_t i = 0; i < faces.size(); ++i) {
            if (!loadFace(i)) {
                continue;
            }
            if (FT_Get_Char_Index(faces[i].face, codepoint) != 0) {
                faceIndexByCodepoint.emplace(codepoint, static_cast<uint32_t>(i));
                return {faces[i].face, static_cast<uint32_t>(i)};
            }
        }

        if (!faces.empty() && loadFace(0)) {
            faceIndexByCodepoint.emplace(codepoint, 0);
            return {faces[0].face, 0};
        }
        return {nullptr, 0};
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

[[nodiscard]] uint32_t fontPixelSize(float size)
{
    return static_cast<uint32_t>(std::max(std::round(size), 1.0f));
}

struct TextVerticalMetrics {
    float baseline = 0.0f;
    float inkTop = 0.0f;
    float inkBottom = 0.0f;
};

struct CachedGlyph {
    float advance = 0.0f;
    int bitmapLeft = 0;
    int bitmapTop = 0;
    uint32_t width = 0;
    uint32_t rows = 0;
    std::vector<uint8_t> alpha;
};

struct Utf8Codepoint {
    uint32_t value = 0;
    size_t start = 0;
    size_t end = 0;
};

[[nodiscard]] bool isContinuationByte(unsigned char value)
{
    return (value & 0xc0u) == 0x80u;
}

[[nodiscard]] Utf8Codepoint nextCodepoint(std::string_view value, size_t index)
{
    const unsigned char first = static_cast<unsigned char>(value[index]);
    if (first < 0x80u) {
        return {.value = first, .start = index, .end = index + 1};
    }

    uint32_t codepoint = 0xfffdu;
    size_t length = 1;
    if ((first & 0xe0u) == 0xc0u) {
        codepoint = first & 0x1fu;
        length = 2;
    } else if ((first & 0xf0u) == 0xe0u) {
        codepoint = first & 0x0fu;
        length = 3;
    } else if ((first & 0xf8u) == 0xf0u) {
        codepoint = first & 0x07u;
        length = 4;
    }

    if (index + length > value.size()) {
        return {.value = 0xfffdu, .start = index, .end = index + 1};
    }

    for (size_t i = 1; i < length; ++i) {
        const unsigned char next = static_cast<unsigned char>(value[index + i]);
        if (!isContinuationByte(next)) {
            return {.value = 0xfffdu, .start = index, .end = index + 1};
        }
        codepoint = (codepoint << 6u) | (next & 0x3fu);
    }

    const bool overlong = (length == 2 && codepoint < 0x80u) || (length == 3 && codepoint < 0x800u) ||
                          (length == 4 && codepoint < 0x10000u);
    const bool surrogate = codepoint >= 0xd800u && codepoint <= 0xdfffu;
    if (overlong || surrogate || codepoint > 0x10ffffu) {
        return {.value = 0xfffdu, .start = index, .end = index + 1};
    }

    return {.value = codepoint, .start = index, .end = index + length};
}

[[nodiscard]] uint64_t glyphCacheKey(uint32_t pixelSize, uint32_t faceIndex, uint32_t codepoint)
{
    return (static_cast<uint64_t>(pixelSize & 0xffffu) << 37u) |
           (static_cast<uint64_t>(faceIndex & 0xffffu) << 21u) |
           static_cast<uint64_t>(codepoint & 0x1fffffu);
}

[[nodiscard]] const CachedGlyph* cachedGlyph(FT_Face face, uint32_t faceIndex, float size, uint32_t codepoint, int loadFlags)
{
    static std::unordered_map<uint64_t, CachedGlyph> renderedGlyphs;
    static std::unordered_map<uint64_t, CachedGlyph> metricGlyphs;

    const uint32_t pixelSize = fontPixelSize(size);
    auto& cache = loadFlags == FT_LOAD_RENDER ? renderedGlyphs : metricGlyphs;
    const uint64_t key = glyphCacheKey(pixelSize, faceIndex, codepoint);
    if (const auto cached = cache.find(key); cached != cache.end()) {
        return &cached->second;
    }

    if (!setFontSize(face, size) || FT_Load_Char(face, codepoint, loadFlags) != 0) {
        return nullptr;
    }

    const auto* glyph = face->glyph;
    CachedGlyph cached{
        .advance = static_cast<float>(glyph->advance.x >> 6),
        .bitmapLeft = glyph->bitmap_left,
        .bitmapTop = glyph->bitmap_top,
    };

    if (loadFlags == FT_LOAD_RENDER) {
        const FT_Bitmap& bitmap = glyph->bitmap;
        cached.width = bitmap.width;
        cached.rows = bitmap.rows;
        cached.alpha.reserve(static_cast<size_t>(cached.width) * cached.rows);
        for (uint32_t row = 0; row < cached.rows; ++row) {
            for (uint32_t column = 0; column < cached.width; ++column) {
                cached.alpha.push_back(bitmap.buffer[row * bitmap.pitch + column]);
            }
        }
    }

    const auto [inserted, _] = cache.emplace(key, std::move(cached));
    return &inserted->second;
}

[[nodiscard]] TextVerticalMetrics textVerticalMetrics(Rect bounds, TextStyle style)
{
    static std::unordered_map<uint32_t, std::pair<float, float>> inkBoundsBySize;

    auto& font = fontResources();
    if (font.faces.empty()) {
        const float fallbackTop = bounds.y + (bounds.height - style.size) * 0.5f;
        return {
            .baseline = fallbackTop + style.size,
            .inkTop = fallbackTop,
            .inkBottom = fallbackTop + style.size,
        };
    }

    const uint32_t pixelSize = fontPixelSize(style.size);
    auto cachedBounds = inkBoundsBySize.find(pixelSize);
    if (cachedBounds == inkBoundsBySize.end()) {
        float minY = 0.0f;
        float maxY = style.size;
        bool hasInk = false;
        constexpr std::string_view representativeGlyphs = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        for (const unsigned char character : representativeGlyphs) {
            auto [face, faceIndex] = font.faceForCodepoint(character);
            if (face == nullptr) {
                continue;
            }
            const CachedGlyph* glyph = cachedGlyph(face, faceIndex, style.size, character, FT_LOAD_RENDER);
            if (glyph == nullptr || glyph->rows == 0) {
                continue;
            }

            const float glyphTop = -static_cast<float>(glyph->bitmapTop);
            const float glyphBottom = glyphTop + static_cast<float>(glyph->rows);
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
        cachedBounds = inkBoundsBySize.emplace(pixelSize, std::pair{minY, maxY}).first;
    }

    const auto [minY, maxY] = cachedBounds->second;
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
    if (font.faces.empty()) {
        return 0.0f;
    }

    float width = 0.0f;
    const size_t clippedEnd = std::min(endIndex, value.size());
    for (size_t i = 0; i < clippedEnd;) {
        const Utf8Codepoint codepoint = nextCodepoint(value, i);
        if (codepoint.end > clippedEnd) {
            break;
        }
        i = codepoint.end;

        auto [face, faceIndex] = font.faceForCodepoint(codepoint.value);
        if (face == nullptr) {
            continue;
        }
        const CachedGlyph* glyph = cachedGlyph(face, faceIndex, style.size, codepoint.value, FT_LOAD_DEFAULT);
        if (glyph != nullptr) {
            width += glyph->advance;
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
    if (font.faces.empty()) {
        return 0;
    }

    float x = 0.0f;
    for (size_t i = 0; i < value.size();) {
        const Utf8Codepoint codepoint = nextCodepoint(value, i);
        float advance = 0.0f;
        auto [face, faceIndex] = font.faceForCodepoint(codepoint.value);
        const CachedGlyph* glyph = face == nullptr ? nullptr : cachedGlyph(face, faceIndex, style.size, codepoint.value, FT_LOAD_DEFAULT);
        if (glyph != nullptr) {
            advance = glyph->advance;
        }
        if (offset < x + advance * 0.5f) {
            return codepoint.start;
        }
        x += advance;
        i = codepoint.end;
    }
    return value.size();
}

float textFieldScrollOffset(std::string_view value, TextStyle style, size_t cursorIndex, float visibleWidth)
{
    const float cursorX = textWidth(value, style, std::min(cursorIndex, value.size()));
    return std::max(cursorX - std::max(visibleWidth - 3.0f, 0.0f), 0.0f);
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

void Canvas::bitmap(Rect rect, const Bitmap& bitmap)
{
    const size_t pixelCount = static_cast<size_t>(bitmap.width) * bitmap.height;
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.size() < pixelCount) {
        return;
    }

    const float pixelWidth = rect.width / static_cast<float>(bitmap.width);
    const float pixelHeight = rect.height / static_cast<float>(bitmap.height);
    for (uint32_t y = 0; y < bitmap.height; ++y) {
        uint32_t x = 0;
        while (x < bitmap.width) {
            Color color = bitmap.pixels[y * bitmap.width + x];
            if (color.a <= 0.0f) {
                ++x;
                continue;
            }

            uint32_t spanWidth = 1;
            while (x + spanWidth < bitmap.width) {
                const Color next = bitmap.pixels[y * bitmap.width + x + spanWidth];
                if (next.r != color.r || next.g != color.g || next.b != color.b || next.a != color.a) {
                    break;
                }
                ++spanWidth;
            }

            fillRect(
                {
                    rect.x + static_cast<float>(x) * pixelWidth,
                    rect.y + static_cast<float>(y) * pixelHeight,
                    static_cast<float>(spanWidth) * pixelWidth,
                    pixelHeight,
                },
                color
            );
            x += spanWidth;
        }
    }
}

void Canvas::text(Rect bounds, std::string_view value, TextStyle style)
{
    clippedText(bounds, bounds.x, value, style);
}

void Canvas::clippedText(Rect bounds, float startX, std::string_view value, TextStyle style)
{
    if (value.empty()) {
        return;
    }

    auto& font = fontResources();
    if (font.faces.empty()) {
        return;
    }

    const float baseline = textVerticalMetrics(bounds, style).baseline;
    float x = startX;
    const float minX = bounds.x;
    const float maxX = bounds.x + bounds.width;

    for (size_t i = 0; i < value.size();) {
        const Utf8Codepoint codepoint = nextCodepoint(value, i);
        i = codepoint.end;

        auto [face, faceIndex] = font.faceForCodepoint(codepoint.value);
        const CachedGlyph* glyph = face == nullptr ? nullptr : cachedGlyph(face, faceIndex, style.size, codepoint.value, FT_LOAD_RENDER);
        if (glyph == nullptr) {
            continue;
        }

        const float advance = glyph->advance;
        if (x >= maxX) {
            break;
        }
        if (x + advance <= minX) {
            x += advance;
            continue;
        }

        const float glyphX = x + static_cast<float>(glyph->bitmapLeft);
        const float glyphY = baseline - static_cast<float>(glyph->bitmapTop);
        for (uint32_t row = 0; row < glyph->rows; ++row) {
            uint32_t column = 0;
            while (column < glyph->width) {
                const uint8_t alpha = glyph->alpha[static_cast<size_t>(row) * glyph->width + column];
                if (alpha == 0) {
                    ++column;
                    continue;
                }

                uint32_t spanWidth = 1;
                while (column + spanWidth < glyph->width &&
                       glyph->alpha[static_cast<size_t>(row) * glyph->width + column + spanWidth] == alpha) {
                    ++spanWidth;
                }

                const float spanX = glyphX + static_cast<float>(column);
                const float clippedSpanX = std::max(spanX, minX);
                const float clippedSpanRight = std::min(spanX + static_cast<float>(spanWidth), maxX);
                if (clippedSpanRight <= clippedSpanX) {
                    column += spanWidth;
                    continue;
                }

                Color glyphColor = style.color;
                glyphColor.a *= static_cast<float>(alpha) / 255.0f;
                fillRect(
                    {
                        clippedSpanX,
                        glyphY + static_cast<float>(row),
                        clippedSpanRight - clippedSpanX,
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
    size_t selectionAnchor,
    std::string_view placeholder
)
{
    box(rect, style.box);

    cursorIndex = std::min(cursorIndex, value.size());
    selectionAnchor = std::min(selectionAnchor, value.size());
    constexpr float horizontalTextInset = 8.0f;
    const Rect textBounds{
        rect.x + horizontalTextInset,
        rect.y,
        rect.width - horizontalTextInset * 2.0f,
        rect.height,
    };
    const float selectionHeight = std::min(rect.height - 4.0f, style.text.size + 2.0f);
    const float selectionY = rect.y + (rect.height - selectionHeight) * 0.5f;
    const float cursorHeight = std::min(rect.height - 6.0f, style.text.size);
    const float cursorY = rect.y + (rect.height - cursorHeight) * 0.5f;
    const bool hasSelection = cursorIndex != selectionAnchor;
    const float scrollOffset = textFieldScrollOffset(value, style.text, cursorIndex, textBounds.width);

    if (hasSelection) {
        const size_t selectionStart = std::min(cursorIndex, selectionAnchor);
        const size_t selectionEnd = std::max(cursorIndex, selectionAnchor);
        const float selectionX = textBounds.x - scrollOffset + textWidth(value, style.text, selectionStart);
        const float selectionRight = textBounds.x - scrollOffset + textWidth(value, style.text, selectionEnd);
        const float clippedSelectionX = std::max(selectionX, textBounds.x);
        const float clippedSelectionRight = std::min(selectionRight, textBounds.x + textBounds.width);
        if (clippedSelectionRight > clippedSelectionX) {
            fillRect({clippedSelectionX, selectionY, clippedSelectionRight - clippedSelectionX, selectionHeight}, style.selection);
        }
    }

    if (value.empty()) {
        text(textBounds, placeholder, style.placeholder);
    } else {
        clippedText(textBounds, textBounds.x - scrollOffset, value, style.text);
    }

    if (focused && !hasSelection) {
        const float cursorX = textBounds.x - scrollOffset + textWidth(value, style.text, cursorIndex) + 2.0f;
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

size_t Canvas::vertexCount() const
{
    return vertices_.size();
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
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

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
