# WAL

`wal` is a small Wayland application launcher. It opens as a layer-shell overlay, searches installed `.desktop` applications, renders with Vulkan, and keeps pinned and frequently launched apps near the top.

## Moved locations
New location is in Codeberg [here](https://codeberg.org/Faevon/WAL).

## Features

- Wayland-native overlay using `wlr-layer-shell`
- Vulkan-rendered UI with animated open, close, selection, list, and scroll transitions
- Desktop entry discovery from the XDG application directories
- Application icon loading for PNG, SVG, XPM, and absolute icon paths
- Fuzzy-ish search across application names, generic names, executable text, and acronyms
- Pinning with persisted pin order
- Launch ranking with decaying scores, so recently used apps rise naturally
- Multi-launch mode for selecting and launching several apps at once
- Clipboard support through `wl-copy` and `wl-paste`
- TOML-style configuration for colors, layout, text, animation, and tinting

## Requirements

`wal` is built with CMake and C++23. Runtime support expects a Wayland compositor that exposes `zwlr_layer_shell_v1` and Vulkan presentation support.

Build dependencies:

- CMake 3.20 or newer
- C++23 compiler
- Vulkan loader and headers
- `glslangValidator`
- `pkg-config`
- `wayland-client`
- `xkbcommon`
- `freetype2`
- `fontconfig`
- `gdk-pixbuf-2.0`
- `librsvg-2.0`

Optional runtime dependency:

- `wl-clipboard` for `Ctrl+C`, `Ctrl+X`, and `Ctrl+V`

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the launcher:

```sh
./build/wal
```

The executable uses a single-instance lock. If another `wal` process is already running, a second invocation exits successfully without opening another overlay.

## Configuration

Copy the example config to your XDG config directory:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/wal"
cp config.example.toml "${XDG_CONFIG_HOME:-$HOME/.config}/wal/config.toml"
```

`wal` reads configuration from:

- `$XDG_CONFIG_HOME/wal/config.toml`
- `$HOME/.config/wal/config.toml`
- `wal.toml` when no home config path can be resolved

See [Configuration](docs/configuration.md) for every supported key.

## Usage

Start typing to filter applications. Press `Enter` to launch the selected entry, or `Escape` to close the overlay.

Common shortcuts:

- `Up` / `Down`: move selection
- `Page Up` / `Page Down`: move by a page of visible results
- `Enter`: launch selected app
- `Escape`: close
- `Ctrl+P`: pin or unpin the selected app
- `Shift+Up` / `Shift+Down`: move a pinned app within the pin list
- `Ctrl+O`: toggle multi-launch mode
- `Enter` in multi-launch mode: toggle the selected app
- `Ctrl+Enter` in multi-launch mode: launch selected apps
- `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`: text selection and clipboard actions
- `Left` / `Right`, `Home`, `End`, `Backspace`, `Delete`: edit the query
- `Ctrl+Left` / `Ctrl+Right`: move by word
- `Ctrl+Backspace` / `Ctrl+Delete`: delete by word

Mouse input is also supported. Click a row to select it, double-click a row to launch it, and use the wheel to move through results.

More detail is available in [Usage](docs/usage.md).

## State

Pinned apps and ranking data are stored under:

- `$XDG_STATE_HOME/wal/`
- `$HOME/.local/state/wal/`
- `.wal-state/` when no home state path can be resolved

The current state files are:

- `pinned-apps`
- `app-rankings`

## Development

See [Development](docs/development.md) for the project layout, build notes, shader compilation, and implementation details.

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).

## Special Thanks

Special thanks to the Lucide team for making good SVGs.
