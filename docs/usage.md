# Usage

`wal` opens an exclusive keyboard layer-shell overlay and presents a search field with matching desktop applications.

## Launching

Build and run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/wal
```

Only one instance is shown at a time. A second invocation exits successfully if the lock is already held.

## Searching

Type into the search field to filter applications. Matching uses:

- application name
- generic name
- executable text from the `.desktop` file
- acronyms derived from those fields

Name matches rank ahead of broader field matches. Empty searches show all applications, ordered by pins, launch ranking, and name.

## Keyboard Shortcuts

Navigation:

| Shortcut | Action |
| --- | --- |
| `Up` | Move selection up |
| `Down` | Move selection down |
| `Page Up` | Move up by one visible page |
| `Page Down` | Move down by one visible page |
| `Enter` | Launch the selected app |
| `Escape` | Close the overlay |

Text editing:

| Shortcut | Action |
| --- | --- |
| `Left` / `Right` | Move cursor |
| `Shift+Left` / `Shift+Right` | Extend selection |
| `Ctrl+Left` / `Ctrl+Right` | Move cursor by word |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Extend selection by word |
| `Home` / `End` | Move to beginning or end |
| `Shift+Home` / `Shift+End` | Select to beginning or end |
| `Backspace` / `Delete` | Delete text |
| `Ctrl+Backspace` / `Ctrl+Delete` | Delete by word |
| `Ctrl+A` | Select all query text |
| `Ctrl+C` | Copy selected query text |
| `Ctrl+X` | Cut selected query text |
| `Ctrl+V` | Paste into query |

Application organization:

| Shortcut | Action |
| --- | --- |
| `Ctrl+P` | Pin or unpin the selected app |
| `Shift+Up` | Move selected pinned app earlier |
| `Shift+Down` | Move selected pinned app later |

Multi-launch:

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` | Toggle multi-launch mode |
| `Enter` | Toggle the selected app in multi-launch mode |
| `Ctrl+Enter` | Launch all selected apps in multi-launch mode |

Clipboard shortcuts use `wl-copy` and `wl-paste -n`, so install `wl-clipboard` if those actions should work.

## Mouse Input

| Input | Action |
| --- | --- |
| Click row | Select row |
| Double-click row | Launch row, or toggle it in multi-launch mode |
| Scroll wheel down | Move selection down |
| Scroll wheel up | Move selection up |
| Drag in text field | Select query text |

## Desktop Entries

Applications are loaded from `applications` directories under:

- `$HOME/.local/share`
- every directory in `$XDG_DATA_DIRS`
- `/usr/local/share` and `/usr/share` when `$XDG_DATA_DIRS` is unset

`wal` reads `.desktop` files recursively, keeps entries from `[Desktop Entry]`, ignores hidden and `NoDisplay` entries, and only includes `Type=Application` entries when a type is provided.

Launch commands are executed through `/bin/sh -c` after desktop-entry field codes such as `%f` or `%u` are stripped. If the `.desktop` entry has a `Path`, the child process changes to that directory before executing the command.

## State Files

State is written to `$XDG_STATE_HOME/wal` or `$HOME/.local/state/wal`.

| File | Purpose |
| --- | --- |
| `pinned-apps` | One pinned application name per line, in pin order |
| `app-rankings` | Decaying launch ranking data |

Launch ranking uses a two-week half-life. Each launch increases an app's score, and older launches gradually matter less.
