#include "wal/ui.hpp"

#include <algorithm>

namespace wal::ui {

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

    const float glyphWidth = std::max(style.size * 0.42f, 4.0f);
    const float glyphHeight = std::max(style.size * 0.52f, 6.0f);
    const float gap = std::max(style.size * 0.22f, 2.0f);
    float x = bounds.x;
    const float y = bounds.y + (bounds.height - glyphHeight) * 0.5f;
    const float maxX = bounds.x + bounds.width;

    for (const char character : value) {
        if (character == ' ') {
            x += glyphWidth;
            continue;
        }
        if (x + glyphWidth > maxX) {
            break;
        }
        fillRect({x, y, glyphWidth, glyphHeight}, style.color);
        x += glyphWidth + gap;
    }
}

void Canvas::textField(Rect rect, std::string_view value, bool focused, TextFieldStyle style)
{
    box(rect, style.box);
    text({rect.x + 14.0f, rect.y, rect.width - 28.0f, rect.height}, value, {});

    if (focused) {
        const float cursorX = std::min(rect.x + rect.width - 15.0f, rect.x + 18.0f + value.size() * 9.0f);
        line({cursorX, rect.y + 12.0f}, {cursorX, rect.y + rect.height - 12.0f}, 2.0f, style.cursor);
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
            itemStyle.border = {0.34f, 0.58f, 0.72f, 1.0f};
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
        1.0f - (point.y / height_) * 2.0f,
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
