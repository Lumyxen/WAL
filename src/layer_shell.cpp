#include "wal/layer_shell.hpp"

namespace wal {

namespace {

const wl_interface* zwlr_layer_shell_v1_get_layer_surface_types[] = {
    &zwlr_layer_surface_v1_interface,
    &wl_surface_interface,
    &wl_output_interface,
    nullptr,
    nullptr,
};

const wl_interface* zwlr_layer_surface_v1_types[] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const wl_message zwlr_layer_shell_v1_requests[] = {
    {"get_layer_surface", "no?ous", zwlr_layer_shell_v1_get_layer_surface_types},
    {"destroy", "", zwlr_layer_surface_v1_types},
};

const wl_message zwlr_layer_surface_v1_requests[] = {
    {"set_size", "uu", zwlr_layer_surface_v1_types},
    {"set_anchor", "u", zwlr_layer_surface_v1_types},
    {"set_exclusive_zone", "i", zwlr_layer_surface_v1_types},
    {"set_margin", "iiii", zwlr_layer_surface_v1_types},
    {"set_keyboard_interactivity", "u", zwlr_layer_surface_v1_types},
    {"get_popup", "o", zwlr_layer_surface_v1_types},
    {"ack_configure", "u", zwlr_layer_surface_v1_types},
    {"destroy", "", zwlr_layer_surface_v1_types},
    {"set_layer", "u", zwlr_layer_surface_v1_types},
    {"set_exclusive_edge", "u", zwlr_layer_surface_v1_types},
};

const wl_message zwlr_layer_surface_v1_events[] = {
    {"configure", "uuu", zwlr_layer_surface_v1_types},
    {"closed", "", zwlr_layer_surface_v1_types},
};

} // namespace

const wl_interface zwlr_layer_shell_v1_interface = {
    "zwlr_layer_shell_v1",
    5,
    2,
    zwlr_layer_shell_v1_requests,
    0,
    nullptr,
};

const wl_interface zwlr_layer_surface_v1_interface = {
    "zwlr_layer_surface_v1",
    5,
    10,
    zwlr_layer_surface_v1_requests,
    2,
    zwlr_layer_surface_v1_events,
};

zwlr_layer_surface_v1* zwlr_layer_shell_v1_get_layer_surface(
    zwlr_layer_shell_v1* shell,
    wl_surface* surface,
    wl_output* output,
    uint32_t layer,
    const char* nameSpace
)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(shell);
    return reinterpret_cast<zwlr_layer_surface_v1*>(wl_proxy_marshal_flags(
        proxy,
        0,
        &zwlr_layer_surface_v1_interface,
        wl_proxy_get_version(proxy),
        0,
        nullptr,
        surface,
        output,
        layer,
        nameSpace
    ));
}

void zwlr_layer_shell_v1_destroy(zwlr_layer_shell_v1* shell)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(shell);
    wl_proxy_marshal_flags(proxy, 1, nullptr, wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

void zwlr_layer_surface_v1_set_size(zwlr_layer_surface_v1* surface, uint32_t width, uint32_t height)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 0, nullptr, wl_proxy_get_version(proxy), 0, width, height);
}

void zwlr_layer_surface_v1_set_anchor(zwlr_layer_surface_v1* surface, uint32_t anchor)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 1, nullptr, wl_proxy_get_version(proxy), 0, anchor);
}

void zwlr_layer_surface_v1_set_exclusive_zone(zwlr_layer_surface_v1* surface, int32_t zone)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 2, nullptr, wl_proxy_get_version(proxy), 0, zone);
}

void zwlr_layer_surface_v1_set_margin(
    zwlr_layer_surface_v1* surface,
    int32_t top,
    int32_t right,
    int32_t bottom,
    int32_t left
)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 3, nullptr, wl_proxy_get_version(proxy), 0, top, right, bottom, left);
}

void zwlr_layer_surface_v1_set_keyboard_interactivity(zwlr_layer_surface_v1* surface, uint32_t interactivity)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 4, nullptr, wl_proxy_get_version(proxy), 0, interactivity);
}

void zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1* surface, uint32_t serial)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 6, nullptr, wl_proxy_get_version(proxy), 0, serial);
}

void zwlr_layer_surface_v1_destroy(zwlr_layer_surface_v1* surface)
{
    auto* proxy = reinterpret_cast<wl_proxy*>(surface);
    wl_proxy_marshal_flags(proxy, 7, nullptr, wl_proxy_get_version(proxy), WL_MARSHAL_FLAG_DESTROY);
}

} // namespace wal
