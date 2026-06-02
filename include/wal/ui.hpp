#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wal::ui {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Vertex {
    Vec2 position;
    Color color;
};

struct BoxStyle {
    Color fill{0.10f, 0.11f, 0.12f, 0.92f};
    Color border{0.30f, 0.33f, 0.36f, 1.0f};
    float borderWidth = 1.0f;
};

struct TextStyle {
    Color color{0.86f, 0.88f, 0.90f, 1.0f};
    float size = 14.0f;
};

struct ListStyle {
    BoxStyle container;
    BoxStyle item;
    Color selectedFill{0.18f, 0.36f, 0.52f, 0.95f};
    float padding = 10.0f;
    float itemHeight = 38.0f;
    float itemGap = 6.0f;
};

struct TextFieldStyle {
    BoxStyle box;
    Color cursor{0.90f, 0.93f, 0.96f, 1.0f};
};

class Canvas {
public:
    Canvas(float width, float height);

    void clear();
    void box(Rect rect, BoxStyle style = {});
    void line(Vec2 from, Vec2 to, float thickness, Color color);
    void text(Rect bounds, std::string_view value, TextStyle style = {});
    void textField(Rect rect, std::string_view value, bool focused, TextFieldStyle style = {});
    void list(Rect rect, std::span<const std::string_view> items, uint32_t selectedIndex, ListStyle style = {});

    [[nodiscard]] std::span<const Vertex> vertices() const;

private:
    float width_ = 1.0f;
    float height_ = 1.0f;
    std::vector<Vertex> vertices_;

    [[nodiscard]] Vec2 toClipSpace(Vec2 point) const;
    void fillRect(Rect rect, Color color);
    void strokeRect(Rect rect, float thickness, Color color);
};

} // namespace wal::ui
