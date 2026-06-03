#include "wal/config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace wal {
namespace {

std::string trim(std::string_view value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

std::string stripComment(std::string_view line)
{
    bool inString = false;
    std::string result;
    result.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];
        if (character == '"' && (i == 0 || line[i - 1] != '\\')) {
            inString = !inString;
        }
        if (!inString && character == '#') {
            break;
        }
        result.push_back(character);
    }
    return trim(result);
}

std::optional<std::string> parseString(std::string_view value)
{
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return std::nullopt;
    }

    std::string parsed;
    parsed.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) {
            ++i;
            switch (value[i]) {
            case 'n':
                parsed.push_back('\n');
                break;
            case 't':
                parsed.push_back('\t');
                break;
            case '"':
            case '\\':
                parsed.push_back(value[i]);
                break;
            default:
                parsed.push_back(value[i]);
                break;
            }
            continue;
        }
        parsed.push_back(value[i]);
    }
    return parsed;
}

std::optional<bool> parseBool(std::string_view value)
{
    const std::string normalized = trim(value);
    if (normalized == "true") {
        return true;
    }
    if (normalized == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> parseNumber(std::string_view value)
{
    const std::string normalized = trim(value);
    double parsed = 0.0;
    const char* begin = normalized.data();
    const char* end = begin + normalized.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<uint8_t> parseHexByte(char high, char low)
{
    const auto nibble = [](char value) -> std::optional<uint8_t> {
        if (value >= '0' && value <= '9') {
            return static_cast<uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<uint8_t>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F') {
            return static_cast<uint8_t>(value - 'A' + 10);
        }
        return std::nullopt;
    };

    const auto highNibble = nibble(high);
    const auto lowNibble = nibble(low);
    if (!highNibble.has_value() || !lowNibble.has_value()) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((*highNibble << 4) | *lowNibble);
}

std::optional<ui::Color> parseColor(std::string_view value)
{
    const auto stringValue = parseString(value);
    if (!stringValue.has_value()) {
        return std::nullopt;
    }

    std::string hex = *stringValue;
    if (!hex.empty() && hex.front() == '#') {
        hex.erase(hex.begin());
    }
    if (hex.size() != 6 && hex.size() != 8) {
        return std::nullopt;
    }

    const auto red = parseHexByte(hex[0], hex[1]);
    const auto green = parseHexByte(hex[2], hex[3]);
    const auto blue = parseHexByte(hex[4], hex[5]);
    if (!red.has_value() || !green.has_value() || !blue.has_value()) {
        return std::nullopt;
    }

    float alpha = 1.0f;
    if (hex.size() == 8) {
        const auto alphaByte = parseHexByte(hex[6], hex[7]);
        if (!alphaByte.has_value()) {
            return std::nullopt;
        }
        alpha = static_cast<float>(*alphaByte) / 255.0f;
    }

    return ui::Color::srgb(*red, *green, *blue, alpha);
}

std::optional<std::array<float, 3>> parseColorChannels(std::string_view value)
{
    const auto color = parseColor(value);
    if (!color.has_value()) {
        return std::nullopt;
    }
    return std::array<float, 3>{color->r, color->g, color->b};
}

void setMilliseconds(std::chrono::milliseconds& target, std::string_view value)
{
    const auto number = parseNumber(value);
    if (number.has_value()) {
        target = std::chrono::milliseconds{std::max<int64_t>(0, static_cast<int64_t>(*number))};
    }
}

void setFloat(float& target, std::string_view value)
{
    const auto number = parseNumber(value);
    if (number.has_value()) {
        target = static_cast<float>(*number);
    }
}

void setUint(uint32_t& target, std::string_view value)
{
    const auto number = parseNumber(value);
    if (number.has_value() && *number >= 0.0) {
        target = static_cast<uint32_t>(*number);
    }
}

void setColor(ui::Color& target, std::string_view value)
{
    const auto color = parseColor(value);
    if (color.has_value()) {
        target = *color;
    }
}

void applyValue(Config& config, std::string_view section, std::string_view key, std::string_view value)
{
    if (section == "animation") {
        if (key == "enabled") {
            if (const auto parsed = parseBool(value)) {
                config.animation.enabled = *parsed;
            }
        } else if (key == "duration_ms") {
            setMilliseconds(config.animation.duration, value);
            config.animation.open_fade_duration = config.animation.duration;
            config.animation.open_slide_duration = config.animation.duration;
            config.animation.list_duration = config.animation.duration;
            config.animation.scroll_duration = config.animation.duration;
            config.animation.multi_launch_duration = config.animation.duration;
        } else if (key == "background_tint_duration_ms") {
            setMilliseconds(config.animation.background_tint_duration, value);
        } else if (key == "close_duration_ms") {
            setMilliseconds(config.animation.close_duration, value);
        } else if (key == "open_duration_ms") {
            setMilliseconds(config.animation.open_fade_duration, value);
            setMilliseconds(config.animation.open_slide_duration, value);
        } else if (key == "open_fade_duration_ms") {
            setMilliseconds(config.animation.open_fade_duration, value);
        } else if (key == "open_slide_duration_ms") {
            setMilliseconds(config.animation.open_slide_duration, value);
        } else if (key == "list_duration_ms") {
            setMilliseconds(config.animation.list_duration, value);
        } else if (key == "scroll_duration_ms") {
            setMilliseconds(config.animation.scroll_duration, value);
        } else if (key == "selection_duration_ms" || key == "selection_highlight_duration_ms") {
            setMilliseconds(config.animation.selection_duration, value);
        } else if (key == "multi_launch_duration_ms") {
            setMilliseconds(config.animation.multi_launch_duration, value);
        } else if (key == "frame_timeout_ms") {
            setMilliseconds(config.animation.frame_timeout, value);
        } else if (key == "open_offset_px") {
            setFloat(config.animation.open_offset_px, value);
        }
        return;
    }

    if (section == "colours" || section == "colors") {
        if (key == "background_tint") {
            if (const auto parsed = parseColorChannels(value)) {
                config.colours.background_tint = *parsed;
            }
        } else if (key == "panel_fill") {
            setColor(config.colours.panel_fill, value);
        } else if (key == "panel_border") {
            setColor(config.colours.panel_border, value);
        } else if (key == "text_field_fill") {
            setColor(config.colours.text_field_fill, value);
        } else if (key == "text") {
            setColor(config.colours.text, value);
        } else if (key == "placeholder") {
            setColor(config.colours.placeholder, value);
        } else if (key == "text_selection") {
            setColor(config.colours.text_selection, value);
        } else if (key == "cursor") {
            setColor(config.colours.cursor, value);
        } else if (key == "selected_fill") {
            setColor(config.colours.selected_fill, value);
        } else if (key == "match_highlight") {
            setColor(config.colours.match_highlight, value);
        } else if (key == "multi_launch_border") {
            setColor(config.colours.multi_launch_border, value);
        } else if (key == "multi_launch_fill") {
            setColor(config.colours.multi_launch_fill, value);
        } else if (key == "icon") {
            setColor(config.colours.icon, value);
        } else if (key == "pin_icon") {
            setColor(config.colours.pin_icon, value);
        }
        return;
    }

    if (section == "layout") {
        if (key == "panel_padding") {
            setFloat(config.layout.panel_padding, value);
        } else if (key == "panel_width_fraction") {
            setFloat(config.layout.panel_width_fraction, value);
            config.layout.panel_width_fraction = std::clamp(config.layout.panel_width_fraction, 0.1f, 1.0f);
        } else if (key == "text_field_height") {
            setFloat(config.layout.text_field_height, value);
        } else if (key == "text_field_horizontal_inset") {
            setFloat(config.layout.text_field_horizontal_inset, value);
        } else if (key == "list_top_gap") {
            setFloat(config.layout.list_top_gap, value);
        } else if (key == "list_padding") {
            setFloat(config.layout.list_padding, value);
        } else if (key == "list_item_height") {
            setFloat(config.layout.list_item_height, value);
        } else if (key == "list_item_gap") {
            setFloat(config.layout.list_item_gap, value);
        } else if (key == "list_edge_fade_distance") {
            setFloat(config.layout.list_edge_fade_distance, value);
        } else if (key == "max_visible_entries") {
            setUint(config.layout.max_visible_entries, value);
            config.layout.max_visible_entries = std::max<uint32_t>(1, config.layout.max_visible_entries);
        } else if (key == "desktop_icon_size") {
            setFloat(config.layout.desktop_icon_size, value);
        } else if (key == "desktop_icon_text_gap") {
            setFloat(config.layout.desktop_icon_text_gap, value);
        } else if (key == "pin_icon_size") {
            setFloat(config.layout.pin_icon_size, value);
        } else if (key == "pin_icon_text_gap") {
            setFloat(config.layout.pin_icon_text_gap, value);
        } else if (key == "row_horizontal_inset") {
            setFloat(config.layout.row_horizontal_inset, value);
        } else if (key == "multi_launch_marker_size") {
            setFloat(config.layout.multi_launch_marker_size, value);
        } else if (key == "multi_launch_marker_text_gap") {
            setFloat(config.layout.multi_launch_marker_text_gap, value);
        } else if (key == "multi_launch_marker_slide_offset") {
            setFloat(config.layout.multi_launch_marker_slide_offset, value);
        }
        return;
    }

    if (section == "text") {
        if (key == "placeholder") {
            if (const auto parsed = parseString(value)) {
                config.text.placeholder = *parsed;
            }
        } else if (key == "text_field_size") {
            setFloat(config.text.text_field_size, value);
        } else if (key == "entry_size") {
            setFloat(config.text.entry_size, value);
        }
        return;
    }

    if (section == "window") {
        if (key == "background_tint") {
            setFloat(config.window.background_tint, value);
            config.window.background_tint = std::clamp(config.window.background_tint, 0.0f, 1.0f);
        }
    }
}

} // namespace

std::filesystem::path configPath()
{
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr && *configHome != '\0') {
        return std::filesystem::path(configHome) / "wal/config.toml";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config/wal/config.toml";
    }
    return "wal.toml";
}

Config loadConfig()
{
    Config config;
    std::ifstream file(configPath());
    if (!file) {
        return config;
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(std::string_view(line).substr(1, line.size() - 2));
            continue;
        }

        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trim(std::string_view(line).substr(0, separator));
        const std::string value = trim(std::string_view(line).substr(separator + 1));
        applyValue(config, section, key, value);
    }

    return config;
}

const Config& config()
{
    static const Config loadedConfig = loadConfig();
    return loadedConfig;
}

} // namespace wal
