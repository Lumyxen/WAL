#pragma once

#include <wayland-client.h>

#include <cstdint>

namespace wal {

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

enum zwlr_layer_shell_v1_layer : uint32_t {
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND = 0,
    ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM = 1,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP = 2,
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY = 3,
};

enum zwlr_layer_surface_v1_anchor : uint32_t {
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP = 1,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM = 2,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT = 4,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT = 8,
};

enum zwlr_layer_surface_v1_keyboard_interactivity : uint32_t {
    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE = 0,
    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE = 1,
    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND = 2,
};

extern const wl_interface zwlr_layer_shell_v1_interface;
extern const wl_interface zwlr_layer_surface_v1_interface;

struct zwlr_layer_surface_v1_listener {
    void (*configure)(void*, zwlr_layer_surface_v1*, uint32_t, uint32_t, uint32_t);
    void (*closed)(void*, zwlr_layer_surface_v1*);
};

[[nodiscard]] zwlr_layer_surface_v1* zwlr_layer_shell_v1_get_layer_surface(
    zwlr_layer_shell_v1* shell,
    wl_surface* surface,
    wl_output* output,
    uint32_t layer,
    const char* nameSpace
);

void zwlr_layer_shell_v1_destroy(zwlr_layer_shell_v1* shell);
void zwlr_layer_surface_v1_set_size(zwlr_layer_surface_v1* surface, uint32_t width, uint32_t height);
void zwlr_layer_surface_v1_set_anchor(zwlr_layer_surface_v1* surface, uint32_t anchor);
void zwlr_layer_surface_v1_set_exclusive_zone(zwlr_layer_surface_v1* surface, int32_t zone);
void zwlr_layer_surface_v1_set_margin(
    zwlr_layer_surface_v1* surface,
    int32_t top,
    int32_t right,
    int32_t bottom,
    int32_t left
);
void zwlr_layer_surface_v1_set_keyboard_interactivity(zwlr_layer_surface_v1* surface, uint32_t interactivity);
void zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1* surface, uint32_t serial);
void zwlr_layer_surface_v1_destroy(zwlr_layer_surface_v1* surface);

} // namespace wal
