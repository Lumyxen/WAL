# Configuration

`wal` loads a simple TOML-style config file at startup. Unknown sections and keys are ignored.

Config lookup order:

1. `$XDG_CONFIG_HOME/wal/config.toml`
2. `$HOME/.config/wal/config.toml`
3. `wal.toml` when no home config path can be resolved

Start from the example:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/wal"
cp config.example.toml "${XDG_CONFIG_HOME:-$HOME/.config}/wal/config.toml"
```

Colors accept `"#RRGGBB"` and `"#RRGGBBAA"` strings. Most numeric values are pixel values unless their key ends in `_ms`.

## animation

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `enabled` | bool | `true` | Enables or disables UI animation globally. |
| `duration_ms` | number | `150` | Convenience duration applied to open fade, open slide, list, scroll, and multi-launch durations. |
| `background_tint_duration_ms` | number | `175` | Background tint fade-in duration. |
| `close_duration_ms` | number | `100` | Close fade-out duration. |
| `open_duration_ms` | number | `150` | Convenience duration applied to open fade and open slide. |
| `open_fade_duration_ms` | number | `150` | Panel fade-in duration. |
| `open_slide_duration_ms` | number | `225` | Panel slide-in duration. |
| `list_duration_ms` | number | `225` | Result list population animation duration. |
| `scroll_duration_ms` | number | `150` | Result list scroll animation duration. |
| `selection_duration_ms` | number | `75` | Selection highlight movement duration. |
| `selection_highlight_duration_ms` | number | `75` | Alias for `selection_duration_ms`. |
| `multi_launch_duration_ms` | number | `150` | Multi-launch marker animation duration. |
| `frame_timeout_ms` | number | `1` | Wayland event timeout while animations are active. |
| `open_offset_px` | number | `14` | Vertical open-slide offset. |

## colours

The parser accepts both `[colours]` and `[colors]`.

| Key | Default | Description |
| --- | --- | --- |
| `background_tint` | `"#1e2326"` | RGB tint color behind the panel. Alpha is controlled by `[window].background_tint`. |
| `panel_fill` | `"#272e33"` | Main panel fill. |
| `panel_border` | `"#4f5b58"` | Main panel border. |
| `text_field_fill` | `"#2e383c"` | Search field fill. |
| `text` | `"#d3c6aa"` | Query and result text. |
| `placeholder` | `"#868d80"` | Search placeholder text. |
| `text_selection` | `"#a7c08059"` | Query text selection highlight. |
| `cursor` | `"#d3c6aa"` | Text cursor. |
| `selected_fill` | `"#414b50f2"` | Selected result row fill. |
| `match_highlight` | `"#3c4841"` | Highlight behind matched result text. |
| `multi_launch_border` | `"#a7c080"` | Multi-launch selection marker border. |
| `multi_launch_fill` | `"#a7c080d9"` | Multi-launch selection marker fill. |
| `icon` | `"#d3c6aa"` | Built-in fallback icon color. |
| `pin_icon` | `"#d3c6aa"` | Pin icon color. |

## layout

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `panel_padding` | number | `18` | Inner panel padding. |
| `panel_width_fraction` | number | `0.4` | Panel width as a fraction of surface width, clamped between `0.1` and `1.0`. |
| `text_field_height` | number | `37` | Search field height. |
| `text_field_horizontal_inset` | number | `8` | Horizontal text inset inside the search field. |
| `list_top_gap` | number | `12` | Space between search field and result list. |
| `list_padding` | number | `0` | Padding inside the list container. |
| `list_item_height` | number | `42` | Result row height. |
| `list_item_gap` | number | `3` | Gap between result rows. |
| `list_edge_fade_distance` | number | `5` | Fade distance at the top and bottom of the visible list. |
| `max_visible_entries` | number | `8` | Maximum visible result rows, clamped to at least `1`. |
| `desktop_icon_size` | number | `30` | Application icon size. |
| `desktop_icon_text_gap` | number | `12` | Space between application icon and result text. |
| `pin_icon_size` | number | `17` | Pin icon size. |
| `pin_icon_text_gap` | number | `10` | Space reserved between result text and pin icon. |
| `row_horizontal_inset` | number | `8` | Horizontal inset for row content. |
| `multi_launch_marker_size` | number | `16` | Multi-launch marker size. |
| `multi_launch_marker_text_gap` | number | `10` | Space between multi-launch marker and row content. |
| `multi_launch_marker_slide_offset` | number | `8` | Marker slide animation offset. |

## text

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `placeholder` | string | `"Search"` | Placeholder shown in the empty search field. |
| `text_field_size` | number | `16` | Search field font size. |
| `entry_size` | number | `15` | Result row font size. |

## window

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `background_tint` | number | `0.72` | Overlay tint opacity, clamped between `0.0` and `1.0`. |
