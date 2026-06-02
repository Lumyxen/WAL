#pragma once

#include <cstdint>
#include <cmath>
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

    [[nodiscard]] static Color srgb(uint8_t red, uint8_t green, uint8_t blue, float alpha = 1.0f)
    {
        return {
            srgbToLinear(static_cast<float>(red) / 255.0f),
            srgbToLinear(static_cast<float>(green) / 255.0f),
            srgbToLinear(static_cast<float>(blue) / 255.0f),
            alpha,
        };
    }

private:
    [[nodiscard]] static float srgbToLinear(float channel)
    {
        if (channel <= 0.04045f) {
            return channel / 12.92f;
        }

        return std::pow((channel + 0.055f) / 1.055f, 2.4f);
    }
};

struct Vertex {
    Vec2 position;
    Color color;
};

struct BoxStyle {
    Color fill = Color::srgb(0x1c, 0x20, 0x23, 0.92f);
    Color border = Color::srgb(0x27, 0x2e, 0x33);
    float borderWidth = 1.0f;
};

struct TextStyle {
    Color color = Color::srgb(0xd3, 0xc6, 0xaa);
    float size = 14.0f;
};

struct ListStyle {
    BoxStyle container;
    BoxStyle item;
    Color selectedFill = Color::srgb(0x2e, 0x38, 0x3c, 0.95f);
    float padding = 10.0f;
    float itemHeight = 38.0f;
    float itemGap = 6.0f;
};

struct TextFieldStyle {
    BoxStyle box;
    Color cursor = Color::srgb(0xd3, 0xc6, 0xaa);
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
