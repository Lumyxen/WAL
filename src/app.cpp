#include "wal/app.hpp"

#include "wal/config.hpp"
#include "wal/io.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <linux/input-event-codes.h>
#include <librsvg/rsvg.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace wal {
namespace {

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

constexpr std::array<float, 4> backgroundTint = {
    0.012983f,
    0.016807f,
    0.019382f,
    0.72f,
};

const ui::Color panelFill = ui::Color::srgb(0x27, 0x2e, 0x33);
const ui::Color panelBorder = ui::Color::srgb(0x4f, 0x5b, 0x58);
const ui::Color textFieldFill = ui::Color::srgb(0x2e, 0x38, 0x3c);
const ui::Color transparent = ui::Color::srgb(0x27, 0x2e, 0x33, 0.0f);
constexpr std::string_view textFieldPlaceholder = "Search";
const ui::TextStyle textFieldPlaceholderStyle{.color = ui::Color::srgb(0x86, 0x8d, 0x80), .size = 16.0f};
const ui::TextStyle textFieldText{.color = ui::Color::srgb(0xd3, 0xc6, 0xaa), .size = 16.0f};
constexpr float panelPadding = 18.0f;
constexpr float textFieldHeight = 37.0f;
constexpr float textFieldHorizontalTextInset = 8.0f;
constexpr size_t maxVisibleDesktopEntries = 8;
constexpr float listTopGap = 12.0f;
constexpr float desktopIconSize = 30.0f;
constexpr float desktopIconTextGap = 12.0f;
constexpr float desktopPinIconSize = 17.0f;
constexpr float desktopPinTextGap = 10.0f;
const ui::TextStyle desktopEntryText{.color = ui::Color::srgb(0xd3, 0xc6, 0xaa), .size = 15.0f};
const ui::Color desktopEntryMatchHighlight = ui::Color::srgb(0x3c, 0x48, 0x41);
constexpr std::string_view pinIconSvg =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#d3c6aa" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 17v5"/><path d="M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z"/></svg>)";

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

std::string lowercase(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

bool parseBool(std::string_view value)
{
    const std::string normalized = lowercase(trim(value));
    return normalized == "true" || normalized == "1";
}

void drawHighlightedText(
    ui::Canvas& canvas,
    ui::Rect bounds,
    std::string_view value,
    std::string_view lowercaseValue,
    std::string_view lowercaseQuery,
    ui::TextStyle style
)
{
    if (!lowercaseQuery.empty()) {
        const float highlightHeight = std::min(bounds.height - 8.0f, style.size + 6.0f);
        const float highlightY = bounds.y + (bounds.height - highlightHeight) * 0.5f;
        size_t matchStart = lowercaseValue.find(lowercaseQuery);
        while (matchStart != std::string_view::npos) {
            const size_t matchEnd = matchStart + lowercaseQuery.size();
            const float highlightX = bounds.x + ui::textWidth(value, style, matchStart);
            const float highlightRight = bounds.x + ui::textWidth(value, style, matchEnd);
            const float clippedHighlightX = std::max(highlightX, bounds.x);
            const float clippedHighlightRight = std::min(highlightRight, bounds.x + bounds.width);
            if (clippedHighlightRight > clippedHighlightX) {
                canvas.box(
                    {clippedHighlightX, highlightY, clippedHighlightRight - clippedHighlightX, highlightHeight},
                    {.fill = desktopEntryMatchHighlight, .borderWidth = 0.0f}
                );
            }

            matchStart = lowercaseValue.find(lowercaseQuery, matchEnd);
        }
    }

    canvas.text(bounds, value, style);
}

ui::ListStyle desktopListStyle()
{
    return {
        .container = {.fill = transparent, .borderWidth = 0.0f},
        .item = {.fill = transparent, .borderWidth = 0.0f},
        .selectedFill = ui::Color::srgb(0x41, 0x4b, 0x50, 0.95f),
        .padding = 0.0f,
        .itemHeight = 42.0f,
        .itemGap = 3.0f,
    };
}

std::vector<std::filesystem::path> xdgDataDirs()
{
    std::vector<std::filesystem::path> dirs;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        dirs.emplace_back(std::filesystem::path(home) / ".local/share");
    }

    if (const char* dataDirs = std::getenv("XDG_DATA_DIRS"); dataDirs != nullptr && *dataDirs != '\0') {
        std::string_view rawDirs(dataDirs);
        size_t start = 0;
        while (start <= rawDirs.size()) {
            const size_t separator = rawDirs.find(':', start);
            const std::string_view dir = rawDirs.substr(
                start,
                separator == std::string_view::npos ? std::string_view::npos : separator - start
            );
            if (!dir.empty()) {
                dirs.emplace_back(dir);
            }
            if (separator == std::string_view::npos) {
                break;
            }
            start = separator + 1;
        }
    } else {
        dirs.emplace_back("/usr/local/share");
        dirs.emplace_back("/usr/share");
    }

    return dirs;
}

std::optional<std::filesystem::path> resolveIconPath(std::string_view iconName)
{
    if (iconName.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path iconPath(iconName);
    std::error_code error;
    if (iconPath.is_absolute() && std::filesystem::is_regular_file(iconPath, error)) {
        return iconPath;
    }

    std::vector<std::string> filenames;
    if (iconPath.extension().empty()) {
        filenames = {
            std::string(iconName) + ".png",
            std::string(iconName) + ".svg",
            std::string(iconName) + ".xpm",
        };
    } else {
        filenames = {std::string(iconName)};
    }

    std::vector<std::filesystem::path> searchRoots;
    for (const auto& dataDir : xdgDataDirs()) {
        searchRoots.emplace_back(dataDir / "icons");
        searchRoots.emplace_back(dataDir / "pixmaps");
    }

    for (const auto& root : searchRoots) {
        if (!std::filesystem::is_directory(root, error)) {
            continue;
        }

        for (const auto& filename : filenames) {
            const std::filesystem::path direct = root / filename;
            if (std::filesystem::is_regular_file(direct, error)) {
                return direct;
            }
        }

        for (std::filesystem::recursive_directory_iterator it(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error
             ),
             end;
             !error && it != end;
             it.increment(error)) {
            if (!it->is_regular_file(error)) {
                continue;
            }
            for (const auto& filename : filenames) {
                if (it->path().filename() == filename) {
                    return it->path();
                }
            }
        }
    }

    return std::nullopt;
}

std::filesystem::path stateDirectory()
{
    if (const char* stateHome = std::getenv("XDG_STATE_HOME"); stateHome != nullptr && *stateHome != '\0') {
        return std::filesystem::path(stateHome) / "wal";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local/state/wal";
    }
    return std::filesystem::path(".wal-state");
}

std::filesystem::path pinnedDesktopEntriesPath()
{
    return stateDirectory() / "pinned-apps";
}

ui::Bitmap pixbufToBitmap(GdkPixbuf* pixbuf)
{
    if (pixbuf == nullptr) {
        return {};
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowStride = gdk_pixbuf_get_rowstride(pixbuf);
    const bool hasAlpha = gdk_pixbuf_get_has_alpha(pixbuf) != 0;
    const auto* pixels = gdk_pixbuf_get_pixels(pixbuf);

    ui::Bitmap bitmap{
        .width = static_cast<uint32_t>(std::max(width, 0)),
        .height = static_cast<uint32_t>(std::max(height, 0)),
    };
    bitmap.pixels.reserve(static_cast<size_t>(bitmap.width) * bitmap.height);
    for (uint32_t y = 0; y < bitmap.height; ++y) {
        const guchar* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(rowStride);
        for (uint32_t x = 0; x < bitmap.width; ++x) {
            const guchar* pixel = row + static_cast<size_t>(x) * static_cast<size_t>(channels);
            const float alpha = hasAlpha ? static_cast<float>(pixel[3]) / 255.0f : 1.0f;
            bitmap.pixels.push_back(ui::Color::srgb(pixel[0], pixel[1], pixel[2], alpha));
        }
    }

    return bitmap;
}

std::vector<float> renderSvgAlphaMask(std::string_view svg, uint32_t size)
{
    GError* error = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(svg.data()),
        svg.size(),
        &error
    );
    if (handle == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return {};
    }

    cairo_surface_t* surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32,
        static_cast<int>(size),
        static_cast<int>(size)
    );
    cairo_t* cairo = cairo_create(surface);
    cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

    const RsvgRectangle viewport{
        .x = 0.0,
        .y = 0.0,
        .width = static_cast<double>(size),
        .height = static_cast<double>(size),
    };
    const gboolean rendered = rsvg_handle_render_document(handle, cairo, &viewport, &error);
    cairo_destroy(cairo);
    g_object_unref(handle);

    if (rendered == FALSE) {
        if (error != nullptr) {
            g_error_free(error);
        }
        cairo_surface_destroy(surface);
        return {};
    }

    cairo_surface_flush(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    std::vector<float> alphaMask(static_cast<size_t>(size) * size, 0.0f);
    for (uint32_t y = 0; y < size; ++y) {
        const unsigned char* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (uint32_t x = 0; x < size; ++x) {
            alphaMask[static_cast<size_t>(y) * size + x] =
                static_cast<float>(row[static_cast<size_t>(x) * 4 + 3]) / 255.0f;
        }
    }

    cairo_surface_destroy(surface);
    return alphaMask;
}

ui::Bitmap sdfBitmapFromAlphaMask(
    std::span<const float> alphaMask,
    uint32_t maskSize,
    uint32_t outputSize,
    ui::Color color
)
{
    if (alphaMask.size() < static_cast<size_t>(maskSize) * maskSize || maskSize == 0 || outputSize == 0) {
        return {};
    }

    ui::Bitmap bitmap{
        .width = outputSize,
        .height = outputSize,
    };
    bitmap.pixels.reserve(static_cast<size_t>(outputSize) * outputSize);

    const float scale = static_cast<float>(maskSize) / static_cast<float>(outputSize);
    const int maxSearchRadius = static_cast<int>(std::ceil(scale * 5.0f));
    constexpr float edgeWidth = 0.85f;

    const auto alphaAt = [&](int x, int y) {
        x = std::clamp(x, 0, static_cast<int>(maskSize) - 1);
        y = std::clamp(y, 0, static_cast<int>(maskSize) - 1);
        return alphaMask[static_cast<size_t>(y) * maskSize + static_cast<size_t>(x)];
    };

    for (uint32_t y = 0; y < outputSize; ++y) {
        for (uint32_t x = 0; x < outputSize; ++x) {
            const float maskX = (static_cast<float>(x) + 0.5f) * scale - 0.5f;
            const float maskY = (static_cast<float>(y) + 0.5f) * scale - 0.5f;
            const int centerX = static_cast<int>(std::round(maskX));
            const int centerY = static_cast<int>(std::round(maskY));
            const bool inside = alphaAt(centerX, centerY) >= 0.5f;

            float nearestDistanceSq = std::numeric_limits<float>::max();
            for (int offsetY = -maxSearchRadius; offsetY <= maxSearchRadius; ++offsetY) {
                for (int offsetX = -maxSearchRadius; offsetX <= maxSearchRadius; ++offsetX) {
                    const int sampleX = centerX + offsetX;
                    const int sampleY = centerY + offsetY;
                    if (sampleX < 0 || sampleY < 0 ||
                        sampleX >= static_cast<int>(maskSize) || sampleY >= static_cast<int>(maskSize)) {
                        continue;
                    }

                    if ((alphaAt(sampleX, sampleY) >= 0.5f) == inside) {
                        continue;
                    }

                    const float dx = static_cast<float>(sampleX) - maskX;
                    const float dy = static_cast<float>(sampleY) - maskY;
                    nearestDistanceSq = std::min(nearestDistanceSq, dx * dx + dy * dy);
                }
            }

            float alpha = inside ? 1.0f : 0.0f;
            if (nearestDistanceSq != std::numeric_limits<float>::max()) {
                const float signedDistance = (inside ? 1.0f : -1.0f) * std::sqrt(nearestDistanceSq) / scale;
                alpha = std::clamp(0.5f + signedDistance / edgeWidth, 0.0f, 1.0f);
            }

            ui::Color pixel = color;
            pixel.a *= alpha;
            bitmap.pixels.push_back(pixel);
        }
    }

    return bitmap;
}

const ui::Bitmap& pinIconBitmap()
{
    static const ui::Bitmap bitmap = [] {
        constexpr uint32_t outputSize = static_cast<uint32_t>(desktopPinIconSize);
        constexpr uint32_t sourceScale = 8;
        const std::vector<float> alphaMask = renderSvgAlphaMask(pinIconSvg, outputSize * sourceScale);
        return sdfBitmapFromAlphaMask(alphaMask, outputSize * sourceScale, outputSize, desktopEntryText.color);
    }();
    return bitmap;
}

ui::Bitmap loadIconBitmap(std::string_view iconName)
{
    const auto iconPath = resolveIconPath(iconName);
    if (!iconPath.has_value()) {
        return {};
    }

    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
        iconPath->c_str(),
        static_cast<int>(desktopIconSize),
        static_cast<int>(desktopIconSize),
        TRUE,
        &error
    );
    if (pixbuf == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return {};
    }

    ui::Bitmap bitmap = pixbufToBitmap(pixbuf);
    g_object_unref(pixbuf);
    return bitmap;
}

std::string desktopExecCommand(std::string_view exec)
{
    std::string command;
    command.reserve(exec.size());
    bool inQuote = false;
    for (size_t i = 0; i < exec.size(); ++i) {
        const char character = exec[i];
        if (character == '"') {
            inQuote = !inQuote;
            command.push_back(character);
            continue;
        }

        if (!inQuote && character == '%' && i + 1 < exec.size()) {
            ++i;
            continue;
        }

        command.push_back(character);
    }

    return trim(command);
}

std::optional<DesktopEntry> parseDesktopEntry(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    bool inDesktopEntry = false;
    std::string name;
    std::string iconName;
    std::string genericName;
    std::string exec;
    std::string workingDirectory;
    std::string type;
    bool hidden = false;
    bool noDisplay = false;

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            inDesktopEntry = trimmed == "[Desktop Entry]";
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }

        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trimmed.substr(0, equals);
        const std::string value = trimmed.substr(equals + 1);
        if (key == "Name" && name.empty()) {
            name = value;
        } else if (key == "Icon" && iconName.empty()) {
            iconName = value;
        } else if (key == "GenericName" && genericName.empty()) {
            genericName = value;
        } else if (key == "Exec" && exec.empty()) {
            exec = value;
        } else if (key == "Path" && workingDirectory.empty()) {
            workingDirectory = value;
        } else if (key == "Type") {
            type = value;
        } else if (key == "Hidden") {
            hidden = parseBool(value);
        } else if (key == "NoDisplay") {
            noDisplay = parseBool(value);
        }
    }

    if (name.empty() || hidden || noDisplay || (!type.empty() && type != "Application")) {
        return std::nullopt;
    }

    std::string searchText = lowercase(name);
    if (!genericName.empty()) {
        searchText += ' ';
        searchText += lowercase(genericName);
    }
    if (!exec.empty()) {
        searchText += ' ';
        searchText += lowercase(exec);
    }

    return DesktopEntry{
        .name = name,
        .iconName = iconName,
        .execCommand = desktopExecCommand(exec),
        .workingDirectory = workingDirectory,
        .searchText = searchText,
    };
}

timespec toTimespec(std::chrono::nanoseconds duration)
{
    duration = std::max(duration, std::chrono::nanoseconds{1});
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    const auto nanoseconds = duration - seconds;
    return {
        static_cast<time_t>(seconds.count()),
        static_cast<long>(nanoseconds.count()),
    };
}

} // namespace

bool QueueFamilyIndices::complete() const
{
    return graphicsFamily.has_value() && presentFamily.has_value();
}

void App::run()
{
    surfaceWidth = defaultWindowWidth;
    surfaceHeight = defaultWindowHeight;
    loadDesktopEntries();

    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void App::registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* app = static_cast<App*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry,
            name,
            &wl_compositor_interface,
            std::min(version, 4u)
        ));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(
            registry,
            name,
            &zwlr_layer_shell_v1_interface,
            std::min(version, 5u)
        ));
    }
}

void App::registryGlobalRemove(void*, wl_registry*, uint32_t) {}

void App::layerSurfaceConfigure(
    void* data,
    zwlr_layer_surface_v1* surface,
    uint32_t serial,
    uint32_t width,
    uint32_t height
)
{
    auto* app = static_cast<App*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    const uint32_t nextWidth = width == 0 ? defaultWindowWidth : width;
    const uint32_t nextHeight = height == 0 ? defaultWindowHeight : height;
    if (nextWidth != app->surfaceWidth || nextHeight != app->surfaceHeight) {
        app->surfaceWidth = nextWidth;
        app->surfaceHeight = nextHeight;
        app->framebufferResized = app->configured;
    }
    app->setInputRegion();
    app->configured = true;
}

void App::layerSurfaceClosed(void* data, zwlr_layer_surface_v1*)
{
    static_cast<App*>(data)->running = false;
}

void App::keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size)
{
    auto* app = static_cast<App*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }

    if (app->xkbContext == nullptr) {
        app->xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }
    if (app->xkbContext == nullptr) {
        munmap(map, size);
        return;
    }

    xkb_keymap* keymap = xkb_keymap_new_from_string(
        app->xkbContext,
        static_cast<const char*>(map),
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    munmap(map, size);
    if (keymap == nullptr) {
        return;
    }

    xkb_state* xkbState = xkb_state_new(keymap);
    if (xkbState == nullptr) {
        xkb_keymap_unref(keymap);
        return;
    }

    if (app->xkbState != nullptr) {
        xkb_state_unref(app->xkbState);
    }
    if (app->xkbKeymap != nullptr) {
        xkb_keymap_unref(app->xkbKeymap);
    }
    app->xkbKeymap = keymap;
    app->xkbState = xkbState;
}

void App::keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void App::keyboardLeave(void* data, wl_keyboard*, uint32_t, wl_surface*)
{
    static_cast<App*>(data)->stopKeyRepeat();
}

void App::keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    auto* app = static_cast<App*>(data);

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        app->handleKeyboardRepeat();
        app->stopKeyRepeat(key);
        return;
    }

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
    }

    const bool handled = app->handleKey(key, true);
    if (handled) {
        app->beginKeyRepeat(key);
    } else {
        app->stopKeyRepeat();
    }
}

void App::keyboardModifiers(
    void* data,
    wl_keyboard*,
    uint32_t,
    uint32_t modsDepressed,
    uint32_t modsLatched,
    uint32_t modsLocked,
    uint32_t group
)
{
    auto* app = static_cast<App*>(data);
    if (app->xkbState != nullptr) {
        xkb_state_update_mask(app->xkbState, modsDepressed, modsLatched, modsLocked, 0, 0, group);
    }
}
void App::keyboardRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay)
{
    auto* app = static_cast<App*>(data);
    if (rate > 0) {
        app->keyboardRepeatRate = rate;
        app->keyboardRepeatDelay = std::chrono::milliseconds{std::max(delay, 0)};
    }
}

void App::pointerEnter(void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t surfaceX, wl_fixed_t surfaceY)
{
    auto* app = static_cast<App*>(data);
    app->pointerX = static_cast<float>(wl_fixed_to_double(surfaceX));
    app->pointerY = static_cast<float>(wl_fixed_to_double(surfaceY));
}

void App::pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*)
{
    static_cast<App*>(data)->mouseSelectingText = false;
}

void App::pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t surfaceX, wl_fixed_t surfaceY)
{
    auto* app = static_cast<App*>(data);
    app->pointerX = static_cast<float>(wl_fixed_to_double(surfaceX));
    app->pointerY = static_cast<float>(wl_fixed_to_double(surfaceY));
    if (app->mouseSelectingText) {
        const size_t nextIndex = app->textIndexAtPointer(app->pointerX);
        if (nextIndex != app->textCursorIndex) {
            app->moveCursor(nextIndex, true);
        }
    }
}

void App::pointerButton(void* data, wl_pointer*, uint32_t, uint32_t time, uint32_t button, uint32_t state)
{
    auto* app = static_cast<App*>(data);
    if (button != BTN_LEFT) {
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        const ui::Rect field = app->textFieldRect();
        if (app->pointerX >= field.x && app->pointerX <= field.x + field.width &&
            app->pointerY >= field.y && app->pointerY <= field.y + field.height) {
            app->mouseSelectingText = true;
            app->textCursorIndex = app->textIndexAtPointer(app->pointerX);
            app->textSelectionAnchor = app->textCursorIndex;
            app->refreshUi();
        }
        if (const auto entryIndex = app->desktopEntryIndexAtPointer(); entryIndex.has_value()) {
            const bool doubleClick = app->lastClickedDesktopEntryIndex == *entryIndex &&
                time - app->lastClickTime <= 500;
            app->selectedDesktopEntryIndex = *entryIndex;
            app->clampDesktopNavigation();
            app->lastClickedDesktopEntryIndex = *entryIndex;
            app->lastClickTime = time;
            app->refreshUi();
            if (doubleClick) {
                app->launchSelectedDesktopEntry();
            }
        }
    } else {
        app->mouseSelectingText = false;
    }
}
void App::pointerAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
{
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
        return;
    }

    auto* app = static_cast<App*>(data);
    if (wl_fixed_to_double(value) > 0.0) {
        app->moveDesktopSelection(1);
    } else if (wl_fixed_to_double(value) < 0.0) {
        app->moveDesktopSelection(-1);
    }
}
void App::pointerFrame(void*, wl_pointer*) {}
void App::pointerAxisSource(void*, wl_pointer*, uint32_t) {}
void App::pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void App::pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
void App::pointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
void App::pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

void App::seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities)
{
    auto* app = static_cast<App*>(data);

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && app->keyboard == nullptr) {
        app->keyboard = wl_seat_get_keyboard(seat);
        static constexpr wl_keyboard_listener keyboardListener = {
            keyboardKeymap,
            keyboardEnter,
            keyboardLeave,
            keyboardKey,
            keyboardModifiers,
            keyboardRepeatInfo,
        };
        wl_keyboard_add_listener(app->keyboard, &keyboardListener, app);
    } else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && app->keyboard != nullptr) {
        app->stopKeyRepeat();
        wl_keyboard_release(app->keyboard);
        app->keyboard = nullptr;
    }

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && app->pointer == nullptr) {
        app->pointer = wl_seat_get_pointer(seat);
        static constexpr wl_pointer_listener pointerListener = {
            pointerEnter,
            pointerLeave,
            pointerMotion,
            pointerButton,
            pointerAxis,
            pointerFrame,
            pointerAxisSource,
            pointerAxisStop,
            pointerAxisDiscrete,
            pointerAxisValue120,
            pointerAxisRelativeDirection,
        };
        wl_pointer_add_listener(app->pointer, &pointerListener, app);
    } else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && app->pointer != nullptr) {
        wl_pointer_release(app->pointer);
        app->pointer = nullptr;
    }
}

void App::seatName(void*, wl_seat*, const char*) {}

void App::initWindow()
{
    display = wl_display_connect(nullptr);
    if (display == nullptr) {
        throw std::runtime_error("failed to connect to Wayland display");
    }

    registry = wl_display_get_registry(display);
    static constexpr wl_registry_listener registryListener = {
        registryGlobal,
        registryGlobalRemove,
    };
    wl_registry_add_listener(registry, &registryListener, this);
    wl_display_roundtrip(display);

    if (compositor == nullptr) {
        throw std::runtime_error("Wayland compositor global is unavailable");
    }
    if (layerShell == nullptr) {
        throw std::runtime_error("zwlr_layer_shell_v1 global is unavailable");
    }
    if (seat == nullptr) {
        throw std::runtime_error("Wayland seat global is unavailable");
    }

    static constexpr wl_seat_listener seatListener = {
        seatCapabilities,
        seatName,
    };
    wl_seat_add_listener(seat, &seatListener, this);
    wl_display_roundtrip(display);

    waylandSurface = wl_compositor_create_surface(compositor);
    if (waylandSurface == nullptr) {
        throw std::runtime_error("failed to create Wayland surface");
    }

    layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        layerShell,
        waylandSurface,
        nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "wal"
    );
    if (layerSurface == nullptr) {
        throw std::runtime_error("failed to create layer surface");
    }

    static zwlr_layer_surface_v1_listener layerSurfaceListener = {
        layerSurfaceConfigure,
        layerSurfaceClosed,
    };
    wl_proxy_add_listener(
        reinterpret_cast<wl_proxy*>(layerSurface),
        reinterpret_cast<void (**)(void)>(&layerSurfaceListener),
        this
    );

    zwlr_layer_surface_v1_set_anchor(
        layerSurface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    );
    zwlr_layer_surface_v1_set_size(layerSurface, 0, 0);
    zwlr_layer_surface_v1_set_margin(layerSurface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
    );

    wl_surface_commit(waylandSurface);

    while (!configured && running) {
        if (wl_display_dispatch(display) == -1) {
            throw std::runtime_error("failed while waiting for layer surface configure");
        }
    }
}

void App::loadDesktopEntries()
{
    desktopEntries.clear();

    std::vector<std::filesystem::path> applicationDirs;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        applicationDirs.emplace_back(std::filesystem::path(home) / ".local/share/applications");
    }

    if (const char* dataDirs = std::getenv("XDG_DATA_DIRS"); dataDirs != nullptr && *dataDirs != '\0') {
        std::string_view dirs(dataDirs);
        size_t start = 0;
        while (start <= dirs.size()) {
            const size_t separator = dirs.find(':', start);
            const std::string_view dir = dirs.substr(
                start,
                separator == std::string_view::npos ? std::string_view::npos : separator - start
            );
            if (!dir.empty()) {
                applicationDirs.emplace_back(std::filesystem::path(dir) / "applications");
            }
            if (separator == std::string_view::npos) {
                break;
            }
            start = separator + 1;
        }
    } else {
        applicationDirs.emplace_back("/usr/local/share/applications");
        applicationDirs.emplace_back("/usr/share/applications");
    }

    std::set<std::string> seenNames;
    for (const auto& dir : applicationDirs) {
        std::error_code error;
        if (!std::filesystem::is_directory(dir, error)) {
            continue;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 dir,
                 std::filesystem::directory_options::skip_permission_denied,
                 error
             ),
             end;
             !error && it != end;
             it.increment(error)) {
            if (!it->is_regular_file(error) || it->path().extension() != ".desktop") {
                continue;
            }

            auto entry = parseDesktopEntry(it->path());
            if (!entry.has_value() || seenNames.contains(entry->name)) {
                continue;
            }

            seenNames.insert(entry->name);
            desktopEntries.push_back(std::move(*entry));
        }
    }

    std::ranges::sort(desktopEntries, {}, &DesktopEntry::name);
    loadPinnedDesktopEntries();
}

void App::loadPinnedDesktopEntries()
{
    for (auto& entry : desktopEntries) {
        entry.pinned = false;
        entry.pinOrder = -1;
    }

    std::ifstream file(pinnedDesktopEntriesPath());
    if (!file) {
        return;
    }

    int pinOrder = 0;
    std::string name;
    while (std::getline(file, name)) {
        if (name.empty()) {
            continue;
        }

        for (auto& entry : desktopEntries) {
            if (!entry.pinned && entry.name == name) {
                entry.pinned = true;
                entry.pinOrder = pinOrder++;
                break;
            }
        }
    }
}

void App::savePinnedDesktopEntries() const
{
    std::error_code error;
    std::filesystem::create_directories(stateDirectory(), error);
    if (error) {
        return;
    }

    std::ofstream file(pinnedDesktopEntriesPath());
    if (!file) {
        return;
    }

    for (const DesktopEntry* entry : orderedDesktopEntries()) {
        if (!entry->pinned) {
            break;
        }
        file << entry->name << '\n';
    }
}

void App::setInputRegion()
{
    wl_region* inputRegion = wl_compositor_create_region(compositor);
    if (inputRegion == nullptr) {
        throw std::runtime_error("failed to create Wayland input region");
    }

    wl_surface_set_input_region(waylandSurface, inputRegion);
    wl_region_destroy(inputRegion);
    wl_surface_commit(waylandSurface);
}

void App::initVulkan()
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createUiVertexBuffer();
    createCommandBuffers();
    createSyncObjects();
}

void App::mainLoop()
{
    bool needsFrame = true;
    while (running) {
        if (needsFrame || uiDirty) {
            drawFrame();
            needsFrame = false;
        }
        dispatchWaylandEvents();
    }

    vkDeviceWaitIdle(device);
}

void App::cleanup()
{
    cleanupSwapchain();

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (keyboardRepeatTimerFd != -1) {
        close(keyboardRepeatTimerFd);
    }
    if (pointer != nullptr) {
        wl_pointer_release(pointer);
    }
    if (keyboard != nullptr) {
        wl_keyboard_release(keyboard);
    }
    if (xkbState != nullptr) {
        xkb_state_unref(xkbState);
    }
    if (xkbKeymap != nullptr) {
        xkb_keymap_unref(xkbKeymap);
    }
    if (xkbContext != nullptr) {
        xkb_context_unref(xkbContext);
    }
    if (layerSurface != nullptr) {
        zwlr_layer_surface_v1_destroy(layerSurface);
    }
    if (waylandSurface != nullptr) {
        wl_surface_destroy(waylandSurface);
    }
    if (seat != nullptr) {
        wl_seat_destroy(seat);
    }
    if (layerShell != nullptr) {
        zwlr_layer_shell_v1_destroy(layerShell);
    }
    if (compositor != nullptr) {
        wl_compositor_destroy(compositor);
    }
    if (registry != nullptr) {
        wl_registry_destroy(registry);
    }
    if (display != nullptr) {
        wl_display_disconnect(display);
    }
}

void App::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "wal";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "wal-ui";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::array extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance");
    }
}

void App::createSurface()
{
    VkWaylandSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    createInfo.display = display;
    createInfo.surface = waylandSurface;

    if (vkCreateWaylandSurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}

void App::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find Vulkan-capable GPU");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& candidate : devices) {
        if (isDeviceSuitable(candidate)) {
            physicalDevice = candidate;
            return;
        }
    }

    throw std::runtime_error("failed to find a suitable GPU");
}

void App::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void App::createSwapchain()
{
    SwapchainSupport support = querySwapchainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = chooseCompositeAlpha(support.capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
    imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);
}

void App::createImageViews()
{
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views");
        }
    }
}

void App::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass");
    }
}

void App::createGraphicsPipeline()
{
    auto vertShaderCode = readFile(std::string(SHADER_DIR) + "/ui.vert.spv");
    auto fragShaderCode = readFile(std::string(SHADER_DIR) + "/ui.frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(ui::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(ui::Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(ui::Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void App::createFramebuffers()
{
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void App::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void App::createUiVertexBuffer()
{
    rebuildUi();
    uiDirty = false;
    if (uiVertices.empty()) {
        return;
    }

    for (size_t i = 0; i < vertexBuffers.size(); ++i) {
        uploadUiVertexBuffer(i);
    }
}

void App::createCommandBuffers()
{
    commandBuffers.resize(swapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void App::createSyncObjects()
{
    imageAvailableSemaphores.resize(maxFramesInFlight);
    renderFinishedSemaphores.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects");
        }
    }
}

void App::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, size_t frameIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }

    VkClearValue clearColor = {{{backgroundTint[0], backgroundTint[1], backgroundTint[2], backgroundTint[3]}}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    if (vertexBuffers[frameIndex] != VK_NULL_HANDLE && !uiVertices.empty()) {
        VkBuffer frameVertexBuffers[] = {vertexBuffers[frameIndex]};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, frameVertexBuffers, offsets);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(uiVertices.size()), 1, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }
}

void App::drawFrame()
{
    if (surfaceWidth == 0 || surfaceHeight == 0) {
        return;
    }

    const VkResult fenceResult = vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, 0);
    if (fenceResult == VK_TIMEOUT) {
        return;
    }
    if (fenceResult != VK_SUCCESS) {
        throw std::runtime_error("failed to wait for frame fence");
    }

    const bool wasUiDirty = uiDirty;
    if (uiDirty) {
        rebuildUi();
        uiDirty = false;
    }
    uploadUiVertexBuffer(currentFrame);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        device,
        swapchain,
        0,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_NOT_READY || result == VK_TIMEOUT) {
        uiDirty = uiDirty || wasUiDirty;
        return;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        const VkResult imageFenceResult = vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, 0);
        if (imageFenceResult == VK_TIMEOUT) {
            uiDirty = uiDirty || wasUiDirty;
            return;
        }
        if (imageFenceResult != VK_SUCCESS) {
            throw std::runtime_error("failed to wait for swapchain image fence");
        }
    }
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    recordCommandBuffer(commandBuffers[imageIndex], imageIndex, currentFrame);

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    const VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
    if (submitResult != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer");
    }

    VkSwapchainKHR swapchains[] = {swapchain};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

void App::recreateSwapchain()
{
    while (surfaceWidth == 0 || surfaceHeight == 0) {
        if (wl_display_dispatch(display) == -1) {
            throw std::runtime_error("failed while waiting for a non-empty surface size");
        }
    }

    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createUiVertexBuffer();
    createCommandBuffers();
}

void App::cleanupSwapchain()
{
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        commandBuffers.clear();
    }

    destroyUiVertexBuffer();

    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    imagesInFlight.clear();
}

void App::destroyUiVertexBuffer()
{
    for (size_t i = 0; i < vertexBuffers.size(); ++i) {
        destroyUiVertexBuffer(i);
    }
}

void App::destroyUiVertexBuffer(size_t frameIndex)
{
    if (vertexBuffers[frameIndex] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffers[frameIndex], nullptr);
        vertexBuffers[frameIndex] = VK_NULL_HANDLE;
    }
    if (vertexBufferMemories[frameIndex] != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemories[frameIndex], nullptr);
        vertexBufferMemories[frameIndex] = VK_NULL_HANDLE;
    }
    vertexBufferCapacities[frameIndex] = 0;
}

void App::uploadUiVertexBuffer(size_t frameIndex)
{
    if (uiVertices.empty()) {
        return;
    }

    const VkDeviceSize bufferSize = sizeof(ui::Vertex) * uiVertices.size();
    if (vertexBufferCapacities[frameIndex] < bufferSize) {
        destroyUiVertexBuffer(frameIndex);
        createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffers[frameIndex],
            vertexBufferMemories[frameIndex]
        );
        vertexBufferCapacities[frameIndex] = bufferSize;
    }

    void* data = nullptr;
    vkMapMemory(device, vertexBufferMemories[frameIndex], 0, bufferSize, 0, &data);
    std::memcpy(data, uiVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, vertexBufferMemories[frameIndex]);
}

void App::refreshUi()
{
    uiDirty = true;
}

bool App::hasTextSelection() const
{
    return textCursorIndex != textSelectionAnchor;
}

size_t App::textSelectionStart() const
{
    return std::min(textCursorIndex, textSelectionAnchor);
}

size_t App::textSelectionEnd() const
{
    return std::max(textCursorIndex, textSelectionAnchor);
}

void App::deleteSelection()
{
    if (!hasTextSelection()) {
        return;
    }

    const size_t start = textSelectionStart();
    textFieldValue.erase(start, textSelectionEnd() - start);
    textCursorIndex = start;
    textSelectionAnchor = start;
    resetDesktopNavigation();
}

void App::insertText(std::string_view value)
{
    deleteSelection();

    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value) {
        if (character >= 0x20 && character <= 0x7e) {
            sanitized.push_back(character);
        }
    }
    if (sanitized.empty()) {
        refreshUi();
        return;
    }

    textFieldValue.insert(textCursorIndex, sanitized);
    textCursorIndex += sanitized.size();
    textSelectionAnchor = textCursorIndex;
    resetDesktopNavigation();
    refreshUi();
}

void App::deleteBackward()
{
    if (hasTextSelection()) {
        deleteSelection();
    } else if (textCursorIndex > 0) {
        textFieldValue.erase(textCursorIndex - 1, 1);
        --textCursorIndex;
        textSelectionAnchor = textCursorIndex;
        resetDesktopNavigation();
    }
    refreshUi();
}

void App::deleteForward()
{
    if (hasTextSelection()) {
        deleteSelection();
    } else if (textCursorIndex < textFieldValue.size()) {
        textFieldValue.erase(textCursorIndex, 1);
        textSelectionAnchor = textCursorIndex;
        resetDesktopNavigation();
    }
    refreshUi();
}

bool App::handleKey(uint32_t key, bool allowSingleShotShortcuts)
{
    if (xkbState == nullptr) {
        return false;
    }

    const xkb_keycode_t keycode = key + 8;
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkbState, keycode);
    const bool ctrl = xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) != 0;
    const bool shift = xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) != 0;

    if (allowSingleShotShortcuts && keysym == XKB_KEY_Escape) {
        running = false;
        return false;
    }

    if (ctrl) {
        switch (keysym) {
        case XKB_KEY_a:
        case XKB_KEY_A:
            if (allowSingleShotShortcuts) {
                selectAllText();
            }
            return false;
        case XKB_KEY_c:
        case XKB_KEY_C:
            if (allowSingleShotShortcuts) {
                copySelectionToClipboard();
            }
            return false;
        case XKB_KEY_x:
        case XKB_KEY_X:
            if (allowSingleShotShortcuts) {
                cutSelectionToClipboard();
            }
            return false;
        case XKB_KEY_v:
        case XKB_KEY_V:
            if (allowSingleShotShortcuts) {
                pasteFromClipboard();
            }
            return false;
        case XKB_KEY_p:
        case XKB_KEY_P:
            if (allowSingleShotShortcuts) {
                toggleSelectedDesktopEntryPin();
            }
            return false;
        case XKB_KEY_Left:
            moveCursorByWord(false, shift);
            return true;
        case XKB_KEY_Right:
            moveCursorByWord(true, shift);
            return true;
        case XKB_KEY_BackSpace:
            if (hasTextSelection()) {
                deleteSelection();
            } else {
                moveCursorByWord(false, true);
                deleteSelection();
            }
            refreshUi();
            return true;
        case XKB_KEY_Delete:
            if (hasTextSelection()) {
                deleteSelection();
            } else {
                moveCursorByWord(true, true);
                deleteSelection();
            }
            refreshUi();
            return true;
        default:
            return false;
        }
    }

    switch (keysym) {
    case XKB_KEY_Up:
        if (shift) {
            moveSelectedDesktopEntryPin(-1);
        } else {
            moveDesktopSelection(-1);
        }
        return true;
    case XKB_KEY_Down:
        if (shift) {
            moveSelectedDesktopEntryPin(1);
        } else {
            moveDesktopSelection(1);
        }
        return true;
    case XKB_KEY_Page_Up:
        moveDesktopSelection(-static_cast<int>(maxVisibleDesktopEntries));
        return true;
    case XKB_KEY_Page_Down:
        moveDesktopSelection(static_cast<int>(maxVisibleDesktopEntries));
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (allowSingleShotShortcuts) {
            launchSelectedDesktopEntry();
        }
        return false;
    case XKB_KEY_Left:
        moveCursor(textCursorIndex == 0 ? 0 : textCursorIndex - 1, shift);
        return true;
    case XKB_KEY_Right:
        moveCursor(std::min(textCursorIndex + 1, textFieldValue.size()), shift);
        return true;
    case XKB_KEY_Home:
        moveCursor(0, shift);
        return true;
    case XKB_KEY_End:
        moveCursor(textFieldValue.size(), shift);
        return true;
    case XKB_KEY_BackSpace:
        deleteBackward();
        return true;
    case XKB_KEY_Delete:
        deleteForward();
        return true;
    default:
        break;
    }

    if (shift && keysym != XKB_KEY_space && keysym < 0x20) {
        return false;
    }

    std::array<char, 8> text{};
    const int length = xkb_state_key_get_utf8(xkbState, keycode, text.data(), text.size());
    if (length == 1 && text[0] >= 0x20 && text[0] <= 0x7e) {
        insertText(std::string_view{text.data(), static_cast<size_t>(length)});
        return true;
    }

    return false;
}

void App::beginKeyRepeat(uint32_t key)
{
    if (keyboardRepeatRate <= 0) {
        stopKeyRepeat();
        return;
    }

    if (keyboardRepeatTimerFd == -1) {
        keyboardRepeatTimerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (keyboardRepeatTimerFd == -1) {
            stopKeyRepeat();
            return;
        }
    }

    const auto interval = std::chrono::nanoseconds{std::max<int64_t>(
        1,
        1'000'000'000LL / static_cast<int64_t>(keyboardRepeatRate)
    )};

    itimerspec timer{};
    timer.it_value = toTimespec(std::chrono::duration_cast<std::chrono::nanoseconds>(keyboardRepeatDelay));
    timer.it_interval = toTimespec(interval);
    if (timerfd_settime(keyboardRepeatTimerFd, 0, &timer, nullptr) == -1) {
        stopKeyRepeat();
        return;
    }

    repeatingKey = key;
}

void App::stopKeyRepeat(uint32_t key)
{
    if (repeatingKey == key) {
        stopKeyRepeat();
    }
}

void App::stopKeyRepeat()
{
    repeatingKey.reset();
    if (keyboardRepeatTimerFd != -1) {
        itimerspec timer{};
        timerfd_settime(keyboardRepeatTimerFd, 0, &timer, nullptr);
    }
}

void App::handleKeyboardRepeat()
{
    if (!repeatingKey.has_value() || keyboardRepeatTimerFd == -1) {
        return;
    }

    uint64_t repeatCount = 0;
    if (read(keyboardRepeatTimerFd, &repeatCount, sizeof(repeatCount)) != sizeof(repeatCount)) {
        return;
    }

    repeatCount = std::min<uint64_t>(repeatCount, 32);
    for (uint64_t i = 0; running && repeatingKey.has_value() && i < repeatCount; ++i) {
        handleKey(*repeatingKey, false);
    }
}

void App::moveCursor(size_t nextIndex, bool extendSelection)
{
    const size_t previousCursor = textCursorIndex;
    const size_t previousAnchor = textSelectionAnchor;

    textCursorIndex = std::min(nextIndex, textFieldValue.size());
    if (!extendSelection) {
        textSelectionAnchor = textCursorIndex;
    }
    if (textCursorIndex != previousCursor || textSelectionAnchor != previousAnchor) {
        refreshUi();
    }
}

void App::moveCursorByWord(bool forward, bool extendSelection)
{
    size_t index = textCursorIndex;
    if (forward) {
        while (index < textFieldValue.size() &&
               std::isalnum(static_cast<unsigned char>(textFieldValue[index])) == 0) {
            ++index;
        }
        while (index < textFieldValue.size() &&
               std::isalnum(static_cast<unsigned char>(textFieldValue[index])) != 0) {
            ++index;
        }
    } else {
        while (index > 0 && std::isalnum(static_cast<unsigned char>(textFieldValue[index - 1])) == 0) {
            --index;
        }
        while (index > 0 && std::isalnum(static_cast<unsigned char>(textFieldValue[index - 1])) != 0) {
            --index;
        }
    }
    moveCursor(index, extendSelection);
}

void App::selectAllText()
{
    textSelectionAnchor = 0;
    textCursorIndex = textFieldValue.size();
    refreshUi();
}

void App::copySelectionToClipboard() const
{
    if (!hasTextSelection()) {
        return;
    }

    FILE* pipe = popen("wl-copy", "w");
    if (pipe == nullptr) {
        return;
    }
    const std::string_view selection{
        textFieldValue.data() + textSelectionStart(),
        textSelectionEnd() - textSelectionStart(),
    };
    fwrite(selection.data(), 1, selection.size(), pipe);
    pclose(pipe);
}

void App::cutSelectionToClipboard()
{
    if (!hasTextSelection()) {
        return;
    }

    copySelectionToClipboard();
    deleteSelection();
    refreshUi();
}

void App::pasteFromClipboard()
{
    FILE* pipe = popen("wl-paste -n", "r");
    if (pipe == nullptr) {
        return;
    }

    std::string pasted;
    std::array<char, 256> buffer{};
    while (const size_t count = fread(buffer.data(), 1, buffer.size(), pipe)) {
        pasted.append(buffer.data(), count);
    }
    pclose(pipe);
    insertText(pasted);
}

void App::resetDesktopNavigation()
{
    selectedDesktopEntryIndex = 0;
    firstVisibleDesktopEntryIndex = 0;
    lastClickedDesktopEntryIndex = std::numeric_limits<size_t>::max();
    lastClickTime = 0;
}

void App::clampDesktopNavigation()
{
    const size_t count = visibleDesktopEntryCount();
    if (count == 0) {
        selectedDesktopEntryIndex = 0;
        firstVisibleDesktopEntryIndex = 0;
        return;
    }

    selectedDesktopEntryIndex = std::min(selectedDesktopEntryIndex, count - 1);
    if (selectedDesktopEntryIndex < firstVisibleDesktopEntryIndex) {
        firstVisibleDesktopEntryIndex = selectedDesktopEntryIndex;
    } else if (selectedDesktopEntryIndex >= firstVisibleDesktopEntryIndex + maxVisibleDesktopEntries) {
        firstVisibleDesktopEntryIndex = selectedDesktopEntryIndex - maxVisibleDesktopEntries + 1;
    }

    const size_t maxFirstVisible = count > maxVisibleDesktopEntries ? count - maxVisibleDesktopEntries : 0;
    firstVisibleDesktopEntryIndex = std::min(firstVisibleDesktopEntryIndex, maxFirstVisible);
}

void App::moveDesktopSelection(int delta)
{
    const size_t count = visibleDesktopEntryCount();
    if (count == 0 || delta == 0) {
        return;
    }

    const int current = static_cast<int>(selectedDesktopEntryIndex);
    const int last = static_cast<int>(count - 1);
    selectedDesktopEntryIndex = static_cast<size_t>(std::clamp(current + delta, 0, last));
    clampDesktopNavigation();
    refreshUi();
}

void App::toggleSelectedDesktopEntryPin()
{
    DesktopEntry* entry = selectedDesktopEntry();
    if (entry == nullptr) {
        return;
    }

    if (entry->pinned) {
        entry->pinned = false;
        entry->pinOrder = -1;
    } else {
        int nextPinOrder = 0;
        for (const auto& desktopEntry : desktopEntries) {
            if (desktopEntry.pinned) {
                nextPinOrder = std::max(nextPinOrder, desktopEntry.pinOrder + 1);
            }
        }
        entry->pinned = true;
        entry->pinOrder = nextPinOrder;
    }

    selectDesktopEntry(entry);
    savePinnedDesktopEntries();
    refreshUi();
}

void App::moveSelectedDesktopEntryPin(int delta)
{
    DesktopEntry* entry = selectedDesktopEntry();
    if (entry == nullptr || !entry->pinned || delta == 0) {
        return;
    }

    std::vector<DesktopEntry*> pins;
    for (auto& desktopEntry : desktopEntries) {
        if (desktopEntry.pinned) {
            pins.push_back(&desktopEntry);
        }
    }
    std::ranges::sort(pins, [](const DesktopEntry* left, const DesktopEntry* right) {
        if (left->pinOrder != right->pinOrder) {
            return left->pinOrder < right->pinOrder;
        }
        return left->name < right->name;
    });

    const auto current = std::ranges::find(pins, entry);
    if (current == pins.end()) {
        return;
    }

    const int currentIndex = static_cast<int>(current - pins.begin());
    const int nextIndex = std::clamp(currentIndex + delta, 0, static_cast<int>(pins.size() - 1));
    if (nextIndex == currentIndex) {
        return;
    }

    std::swap(entry->pinOrder, pins[static_cast<size_t>(nextIndex)]->pinOrder);
    selectDesktopEntry(entry);
    savePinnedDesktopEntries();
    refreshUi();
}

void App::launchSelectedDesktopEntry()
{
    const std::vector<const DesktopEntry*> entries = filteredDesktopEntries();
    if (selectedDesktopEntryIndex >= entries.size()) {
        return;
    }

    launchDesktopEntry(*entries[selectedDesktopEntryIndex]);
}

void App::selectDesktopEntry(const DesktopEntry* entry)
{
    if (entry == nullptr) {
        return;
    }

    const std::vector<const DesktopEntry*> entries = filteredDesktopEntries();
    const auto selected = std::ranges::find(entries, entry);
    if (selected == entries.end()) {
        clampDesktopNavigation();
        return;
    }

    selectedDesktopEntryIndex = static_cast<size_t>(selected - entries.begin());
    clampDesktopNavigation();
}

void App::launchDesktopEntry(const DesktopEntry& entry)
{
    if (entry.execCommand.empty()) {
        return;
    }

    const pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (!entry.workingDirectory.empty() && chdir(entry.workingDirectory.c_str()) != 0) {
            std::fprintf(
                stderr,
                "wal: failed to enter desktop entry Path '%s': %s\n",
                entry.workingDirectory.c_str(),
                std::strerror(errno)
            );
            _exit(127);
        }
        execl("/bin/sh", "sh", "-c", entry.execCommand.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    if (pid > 0) {
        running = false;
    }
}

ui::Rect App::textFieldRect() const
{
    const ui::Rect panel = panelRect(visibleDesktopEntryCount());

    return {
        panel.x + panelPadding,
        panel.y + panelPadding,
        panel.width - panelPadding * 2.0f,
        textFieldHeight,
    };
}

ui::Rect App::desktopListRect() const
{
    const size_t visibleCount = std::min(visibleDesktopEntryCount(), maxVisibleDesktopEntries);
    if (visibleCount == 0) {
        return {};
    }

    const ui::ListStyle listStyle = desktopListStyle();
    const ui::Rect field = textFieldRect();
    const ui::Rect panel = panelRect(visibleCount);
    const float listHeight =
        listStyle.padding * 2.0f +
        static_cast<float>(visibleCount) * listStyle.itemHeight +
        static_cast<float>(visibleCount - 1) * listStyle.itemGap;
    return {
        panel.x + panelPadding,
        field.y + field.height + listTopGap,
        panel.width - panelPadding * 2.0f,
        listHeight,
    };
}

std::optional<size_t> App::desktopEntryIndexAtPointer() const
{
    const size_t count = visibleDesktopEntryCount();
    if (count == 0) {
        return std::nullopt;
    }

    const ui::ListStyle listStyle = desktopListStyle();
    const ui::Rect listRect = desktopListRect();
    if (pointerX < listRect.x || pointerX > listRect.x + listRect.width ||
        pointerY < listRect.y || pointerY > listRect.y + listRect.height) {
        return std::nullopt;
    }

    const float localY = pointerY - listRect.y - listStyle.padding;
    const float rowStride = listStyle.itemHeight + listStyle.itemGap;
    if (localY < 0.0f) {
        return std::nullopt;
    }

    const auto row = static_cast<size_t>(localY / rowStride);
    if (row >= maxVisibleDesktopEntries || row >= count || localY - static_cast<float>(row) * rowStride > listStyle.itemHeight) {
        return std::nullopt;
    }

    const size_t entryIndex = firstVisibleDesktopEntryIndex + row;
    if (entryIndex >= count) {
        return std::nullopt;
    }
    return entryIndex;
}

ui::Rect App::panelRect(size_t visibleResultCount) const
{
    const ui::ListStyle listStyle = desktopListStyle();
    const size_t cappedResultCount = std::min(visibleResultCount, maxVisibleDesktopEntries);
    const float listHeight = cappedResultCount == 0
        ? 0.0f
        : listStyle.padding * 2.0f +
              static_cast<float>(cappedResultCount) * listStyle.itemHeight +
              static_cast<float>(cappedResultCount - 1) * listStyle.itemGap;
    const float panelHeight =
        panelPadding * 2.0f + textFieldHeight + (cappedResultCount == 0 ? 0.0f : listTopGap + listHeight);
    const float panelWidth = static_cast<float>(swapchainExtent.width) * 0.4f;

    return {
        (static_cast<float>(swapchainExtent.width) - panelWidth) * 0.5f,
        (static_cast<float>(swapchainExtent.height) - panelHeight) * 0.5f,
        panelWidth,
        panelHeight,
    };
}

size_t App::visibleDesktopEntryCount() const
{
    return filteredDesktopEntries().size();
}

std::vector<const DesktopEntry*> App::filteredDesktopEntries() const
{
    std::vector<const DesktopEntry*> entries;
    const std::string query = lowercase(textFieldValue);
    if (query.empty()) {
        for (const DesktopEntry* entry : orderedDesktopEntries()) {
            entries.push_back(entry);
        }
    } else {
        for (const auto& entry : desktopEntries) {
            if (entry.searchText.find(query) == std::string::npos) {
                continue;
            }
            entries.push_back(&entry);
        }
    }
    return entries;
}

std::vector<DesktopEntry*> App::visibleDesktopEntries()
{
    std::vector<DesktopEntry*> entries;
    entries.reserve(maxVisibleDesktopEntries);

    const std::string query = lowercase(textFieldValue);
    size_t matchIndex = 0;
    const auto appendIfVisible = [&](DesktopEntry& entry) {
        if (!query.empty() && entry.searchText.find(query) == std::string::npos) {
            return false;
        }

        if (matchIndex++ < firstVisibleDesktopEntryIndex) {
            return false;
        }

        entries.push_back(&entry);
        return entries.size() == maxVisibleDesktopEntries;
    };

    if (query.empty()) {
        for (DesktopEntry* entry : orderedDesktopEntries()) {
            if (appendIfVisible(*entry)) {
                break;
            }
        }
    } else {
        for (auto& entry : desktopEntries) {
            if (appendIfVisible(entry)) {
                break;
            }
        }
    }

    return entries;
}

std::vector<DesktopEntry*> App::orderedDesktopEntries()
{
    std::vector<DesktopEntry*> entries;
    entries.reserve(desktopEntries.size());
    for (auto& entry : desktopEntries) {
        entries.push_back(&entry);
    }

    std::ranges::stable_sort(entries, [](const DesktopEntry* left, const DesktopEntry* right) {
        if (left->pinned != right->pinned) {
            return left->pinned;
        }
        if (left->pinned && left->pinOrder != right->pinOrder) {
            return left->pinOrder < right->pinOrder;
        }
        return left->name < right->name;
    });
    return entries;
}

std::vector<const DesktopEntry*> App::orderedDesktopEntries() const
{
    std::vector<const DesktopEntry*> entries;
    entries.reserve(desktopEntries.size());
    for (const auto& entry : desktopEntries) {
        entries.push_back(&entry);
    }

    std::ranges::stable_sort(entries, [](const DesktopEntry* left, const DesktopEntry* right) {
        if (left->pinned != right->pinned) {
            return left->pinned;
        }
        if (left->pinned && left->pinOrder != right->pinOrder) {
            return left->pinOrder < right->pinOrder;
        }
        return left->name < right->name;
    });
    return entries;
}

DesktopEntry* App::selectedDesktopEntry()
{
    const std::string query = lowercase(textFieldValue);
    size_t matchIndex = 0;

    if (query.empty()) {
        for (DesktopEntry* entry : orderedDesktopEntries()) {
            if (matchIndex++ == selectedDesktopEntryIndex) {
                return entry;
            }
        }
    } else {
        for (auto& entry : desktopEntries) {
            if (entry.searchText.find(query) == std::string::npos) {
                continue;
            }
            if (matchIndex++ == selectedDesktopEntryIndex) {
                return &entry;
            }
        }
    }

    return nullptr;
}

size_t App::textIndexAtPointer(float x) const
{
    const ui::Rect field = textFieldRect();
    const float visibleTextWidth = field.width - textFieldHorizontalTextInset * 2.0f;
    const float scrollOffset = ui::textFieldScrollOffset(
        textFieldValue,
        textFieldText,
        textCursorIndex,
        visibleTextWidth
    );
    return ui::textIndexAtOffset(
        textFieldValue,
        std::max(x - field.x - textFieldHorizontalTextInset + scrollOffset, 0.0f),
        textFieldText
    );
}

void App::rebuildUi()
{
    uiVertices.clear();
    clampDesktopNavigation();

    ui::Canvas canvas(static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height));
    const std::string query = lowercase(textFieldValue);

    const std::vector<DesktopEntry*> visibleEntries = visibleDesktopEntries();
    const ui::Rect panel = panelRect(visibleEntries.size());

    canvas.box(panel, {.fill = panelFill, .border = panelBorder, .borderWidth = 1.0f});

    const ui::Rect textField = textFieldRect();
    canvas.textField(
        textField,
        textFieldValue,
        true,
        {
            .box = {.fill = textFieldFill, .borderWidth = 0.0f},
            .placeholder = textFieldPlaceholderStyle,
            .text = textFieldText,
        },
        textCursorIndex,
        textSelectionAnchor,
        textFieldPlaceholder
    );

    if (!visibleEntries.empty()) {
        const ui::ListStyle listStyle = desktopListStyle();
        const ui::Rect listRect = desktopListRect();
        float y = listRect.y + listStyle.padding;
        for (size_t i = 0; i < visibleEntries.size(); ++i) {
            DesktopEntry& entry = *visibleEntries[i];
            if (!entry.iconLoaded) {
                entry.icon = loadIconBitmap(entry.iconName);
                entry.iconLoaded = true;
            }

            const ui::Rect itemRect{
                listRect.x + listStyle.padding,
                y,
                listRect.width - listStyle.padding * 2.0f,
                listStyle.itemHeight,
            };
            if (firstVisibleDesktopEntryIndex + i == selectedDesktopEntryIndex) {
                canvas.box(itemRect, {.fill = listStyle.selectedFill, .borderWidth = 0.0f});
            }

            const float iconY = itemRect.y + (itemRect.height - desktopIconSize) * 0.5f;
            const ui::Rect iconRect{itemRect.x + 8.0f, iconY, desktopIconSize, desktopIconSize};
            if (!entry.icon.pixels.empty()) {
                canvas.bitmap(iconRect, entry.icon);
            }

            const float textX = iconRect.x + iconRect.width + desktopIconTextGap;
            const float pinIconX = itemRect.x + itemRect.width - 8.0f - desktopPinIconSize;
            const float textWidth = std::max(
                entry.pinned
                    ? itemRect.x + itemRect.width - textX - desktopPinIconSize - desktopPinTextGap - 8.0f
                    : itemRect.x + itemRect.width - textX - 8.0f,
                0.0f
            );
            drawHighlightedText(
                canvas,
                {
                    textX,
                    itemRect.y,
                    textWidth,
                    itemRect.height,
                },
                entry.name,
                lowercase(entry.name),
                query,
                desktopEntryText
            );
            if (entry.pinned) {
                const float pinIconY = itemRect.y + (itemRect.height - desktopPinIconSize) * 0.5f;
                canvas.bitmap({pinIconX, pinIconY, desktopPinIconSize, desktopPinIconSize}, pinIconBitmap());
            }

            y += listStyle.itemHeight + listStyle.itemGap;
        }
    }

    const auto vertices = canvas.vertices();
    uiVertices.assign(vertices.begin(), vertices.end());
}

VkShaderModule App::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }

    return shaderModule;
}

bool App::isDeviceSuitable(VkPhysicalDevice candidate)
{
    QueueFamilyIndices indices = findQueueFamilies(candidate);
    bool extensionsSupported = checkDeviceExtensionSupport(candidate);
    bool swapchainAdequate = false;

    if (extensionsSupported) {
        SwapchainSupport swapchainSupport = querySwapchainSupport(candidate);
        swapchainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
    }

    return indices.complete() && extensionsSupported && swapchainAdequate;
}

QueueFamilyIndices App::findQueueFamilies(VkPhysicalDevice candidate)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &presentSupport);
        if (presentSupport != VK_FALSE) {
            indices.presentFamily = i;
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

bool App::checkDeviceExtensionSupport(VkPhysicalDevice candidate)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapchainSupport App::querySwapchainSupport(VkPhysicalDevice candidate)
{
    SwapchainSupport details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR App::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return formats[0];
}

VkPresentModeKHR App::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes)
{
    for (const auto& availablePresentMode : presentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR App::chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha)
{
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    if ((supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkExtent2D App::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {
        surfaceWidth,
        surfaceHeight,
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

uint32_t App::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find a suitable memory type");
}

void App::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocateInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void App::dispatchWaylandEvents()
{
    handleKeyboardRepeat();

    if (wl_display_dispatch_pending(display) == -1) {
        running = false;
        return;
    }
    if (!running) {
        return;
    }

    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) == -1) {
            running = false;
            return;
        }
    }

    wl_display_flush(display);

    std::array<pollfd, 2> polls{};
    nfds_t pollCount = 1;
    polls[0].fd = wl_display_get_fd(display);
    polls[0].events = POLLIN;
    if (keyboardRepeatTimerFd != -1) {
        polls[1].fd = keyboardRepeatTimerFd;
        polls[1].events = POLLIN;
        pollCount = 2;
    }

    const int pollResult = poll(polls.data(), pollCount, uiDirty ? 0 : -1);
    if (pollResult <= 0) {
        wl_display_cancel_read(display);
        return;
    }

    if (pollResult > 0 && pollCount > 1 && (polls[1].revents & POLLIN) != 0) {
        handleKeyboardRepeat();
    }

    if ((polls[0].revents & POLLIN) != 0) {
        if (wl_display_read_events(display) == -1) {
            running = false;
            return;
        }
    } else {
        wl_display_cancel_read(display);
    }

    if (wl_display_dispatch_pending(display) == -1) {
        running = false;
        return;
    }

    wl_display_flush(display);
}

} // namespace wal
