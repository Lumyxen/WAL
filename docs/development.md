# Development

## Project Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Build definition, dependencies, and shader compilation. |
| `src/main.cpp` | Entry point and single-instance lock. |
| `src/app.cpp` | Wayland, Vulkan, desktop entry loading, input handling, launch behavior, state, and UI orchestration. |
| `src/config.cpp` | Config file lookup and parsing. |
| `src/io.cpp` | Binary file reading helper. |
| `src/layer_shell.cpp` | Minimal generated-style bindings for `zwlr_layer_shell_v1`. |
| `src/ui.cpp` | Immediate-mode UI canvas, text layout, and geometry emission. |
| `src/shaders/` | GLSL shaders compiled to SPIR-V at build time. |
| `include/wal/` | Public project headers. |
| `config.example.toml` | Example user configuration. |

## Build Flow

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

CMake requires `glslangValidator`. During the build, `src/shaders/ui.vert` and `src/shaders/ui.frag` are compiled into:

```text
build/shaders/ui.vert.spv
build/shaders/ui.frag.spv
```

Those SPIR-V files are converted into a generated C++ header and compiled into the executable.

## Runtime Model

`wal` connects to Wayland, binds the compositor, seat, and layer-shell globals, creates a full-screen overlay layer surface, then initializes Vulkan against the Wayland surface.

The app requests exclusive keyboard interactivity and sets a transparent input region outside the active panel area. It drives rendering from Wayland events, animation state, keyboard repeat timers, and swapchain presentation.

## Desktop Entry Loading

Desktop entries are scanned recursively from XDG application directories. Entries are skipped when:

- the file is not a `.desktop` file
- it does not have a usable `[Desktop Entry]` section
- `Name` is missing
- `Hidden=true`
- `NoDisplay=true`
- `Type` is present and is not `Application`
- an earlier entry already used the same application name

Icons are resolved from absolute paths, XDG icon directories, and pixmaps directories. SVGs are loaded with `librsvg`; raster formats are loaded with `gdk-pixbuf`.

## State

State lives under `$XDG_STATE_HOME/wal` or `$HOME/.local/state/wal`.

Pinned apps are stored by display name in `pinned-apps`. Ranking data is stored in `app-rankings` with a decaying score and update timestamp. The ranking half-life is currently 14 days.

## Configuration Parser

The config parser intentionally accepts a small subset of TOML-like syntax:

- section headers such as `[layout]`
- `key = value` assignments
- quoted strings
- booleans
- decimal numbers
- color strings in `"#RRGGBB"` or `"#RRGGBBAA"` form
- comments beginning with `#` outside quoted strings

Invalid values and unknown keys are ignored, preserving defaults.

## Notes For Changes

- Keep docs and `config.example.toml` aligned when adding config keys.
- Add new build dependencies in both `CMakeLists.txt` and the README.
- If shader filenames change, update `SHADER_SOURCES` in `CMakeLists.txt`.
- Avoid changing state file formats without either backward compatibility or a clear migration path.
