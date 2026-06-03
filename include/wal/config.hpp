#pragma once

#include "wal/ui.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace wal {

inline constexpr uint32_t defaultWindowWidth = 1280;
inline constexpr uint32_t defaultWindowHeight = 720;
inline constexpr int maxFramesInFlight = 2;

struct AnimationConfig {
    bool enabled = true;
    std::chrono::milliseconds duration{150};
    std::chrono::milliseconds background_tint_duration{175};
    std::chrono::milliseconds close_duration{100};
    std::chrono::milliseconds open_fade_duration{150};
    std::chrono::milliseconds open_slide_duration{225};
    std::chrono::milliseconds list_duration{225};
    std::chrono::milliseconds scroll_duration{150};
    std::chrono::milliseconds selection_duration{75};
    std::chrono::milliseconds multi_launch_duration{150};
    std::chrono::milliseconds frame_timeout{1};
    float open_offset_px = 14.0f;
};

struct ColourConfig {
    std::array<float, 3> background_tint = {0.012983f, 0.016807f, 0.019382f};
    ui::Color panel_fill = ui::Color::srgb(0x27, 0x2e, 0x33);
    ui::Color panel_border = ui::Color::srgb(0x4f, 0x5b, 0x58);
    ui::Color text_field_fill = ui::Color::srgb(0x2e, 0x38, 0x3c);
    ui::Color text = ui::Color::srgb(0xd3, 0xc6, 0xaa);
    ui::Color placeholder = ui::Color::srgb(0x86, 0x8d, 0x80);
    ui::Color text_selection = ui::Color::srgb(0xa7, 0xc0, 0x80, 0.35f);
    ui::Color cursor = ui::Color::srgb(0xd3, 0xc6, 0xaa);
    ui::Color selected_fill = ui::Color::srgb(0x41, 0x4b, 0x50, 0.95f);
    ui::Color match_highlight = ui::Color::srgb(0x3c, 0x48, 0x41);
    ui::Color multi_launch_border = ui::Color::srgb(0xa7, 0xc0, 0x80);
    ui::Color multi_launch_fill = ui::Color::srgb(0xa7, 0xc0, 0x80, 0.85f);
    ui::Color icon = ui::Color::srgb(0xd3, 0xc6, 0xaa);
    ui::Color pin_icon = ui::Color::srgb(0xd3, 0xc6, 0xaa);
};

struct LayoutConfig {
    float panel_padding = 18.0f;
    float panel_width_fraction = 0.4f;
    float text_field_height = 37.0f;
    float text_field_horizontal_inset = 8.0f;
    float list_top_gap = 12.0f;
    float list_padding = 0.0f;
    float list_item_height = 42.0f;
    float list_item_gap = 3.0f;
    float list_edge_fade_distance = 5.0f;
    uint32_t max_visible_entries = 8;
    float desktop_icon_size = 30.0f;
    float desktop_icon_text_gap = 12.0f;
    float pin_icon_size = 17.0f;
    float pin_icon_text_gap = 10.0f;
    float row_horizontal_inset = 8.0f;
    float multi_launch_marker_size = 16.0f;
    float multi_launch_marker_text_gap = 10.0f;
    float multi_launch_marker_slide_offset = 8.0f;
};

struct TextConfig {
    std::string placeholder = "Search";
    float text_field_size = 16.0f;
    float entry_size = 15.0f;
};

struct WindowConfig {
    float background_tint = 0.72f;
};

struct Config {
    AnimationConfig animation;
    ColourConfig colours;
    LayoutConfig layout;
    TextConfig text;
    WindowConfig window;
};

[[nodiscard]] std::filesystem::path configPath();
[[nodiscard]] Config loadConfig();
[[nodiscard]] const Config& config();

} // namespace wal
