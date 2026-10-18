# IPC

The shell listens on `$XDG_RUNTIME_DIR/material3ui/ipc.sock`. `material3ui msg` sends one command and prints the reply:

```bash
material3ui msg <command> [args]
material3ui msg help        # every command, from the running shell
```

Hyprland binds, scripts and other programs all use the same commands. The defaults live in [`shell.lua`](../dotfiles/.config/hypr/material3ui/shell.lua).

## Panels

| Command | Opens or toggles |
| :--- | :--- |
| `launcher [text]` | Spotlight, optionally pre-filled: `=` maths, `:` emoji, `#` clipboard, `@` files, `>` run |
| `popup quicksettings [wifi]` | Quick Settings, or straight to its Wi-Fi list |
| `popup notifications` · `dashboard` · `overview` · `sysmon` · `power` | The notification centre, dashboard, workspace overview, system monitor and power menu |
| `popup audio` · `network` · `bluetooth` · `run` | The small status panels and the run dialog |
| `settings [page]` | Settings. Pages: `network`, `bluetooth`, `sound`, `display`, `appearance`, `style`, `bar`, `dock`, `tools`, `notifications`, `lock`, `keybinds`, `packages`, `about` |
| `clipboard` | Clipboard history |
| `screenshot` | The screenshot tool |
| `wallpaper pick` | The wallpaper picker |

Running a panel command again closes the panel.

## Actions

| Command | Does |
| :--- | :--- |
| `screenshot region\|window\|screen [delay]` | Captures at once (delay in seconds). It saves to `~/Pictures/Screenshots`, and copies to the clipboard, opens an editor or notifies, depending on Settings → Tools. |
| `screenshot ocr` | Copies the text in a picked region (tesseract) |
| `record [region\|screen]` | Starts a screen recording to `~/Videos/Recordings` (wf-recorder); runs again to stop |
| `volume up\|down\|mute\|set <0-150>\|balance <-100..100>` | Output volume, with the OSD |
| `brightness up\|down\|set <0-100>` | Backlight, with the OSD |
| `media play-pause\|next\|previous\|status` | The active MPRIS player |
| `wallpaper next\|prev\|set <path>` | Changes the wallpaper and recolours the desktop through Matugen |
| `notifications [list\|clear\|dnd on\|dnd off]` | History, clearing and Do Not Disturb |
| `lock` | Locks the session (ext-session-lock) |
| `idle inhibit\|allow` | Caffeine: keeps the screen awake |
| `bar toggle\|show\|hide` | Status bar visibility |
| `reload` | Re-reads `config.json` and the palette |
| `quit` | Exits the shell |

## Inspection

| Command | Prints |
| :--- | :--- |
| `ping` · `version` | `pong`, and the shell version |
| `status` | A summary of every service (audio, network, Bluetooth, battery, media…) |
| `outputs` · `workspaces` · `tray` | What the shell sees |
| `debug` | Every surface's node tree with positions (used by the UI tests) |

## From earlier versions

v3 and earlier ran on Quickshell and used `qs -c material3ui ipc call material3ui <fn>` and `material3uictl`. Both are gone in v4. Map old calls like this:

| v3 | v4 |
| :--- | :--- |
| `material3uictl spotlight` · `launcher` | `material3ui msg launcher` |
| `material3uictl controlCenter` | `material3ui msg popup quicksettings` |
| `material3uictl dashboard` · `overview` · `power` · `notifications` | `material3ui msg popup <name>` |
| `material3uictl systemMonitor` | `material3ui msg popup sysmon` |
| `material3uictl settings` | `material3ui msg settings` |
| `material3uictl clipboard` · `emoji` | `material3ui msg clipboard` · `material3ui msg launcher :` |
| `material3uictl wallpaper <file>` · `next` | `material3ui msg wallpaper set <file>` · `next` |
| `material3uictl region screenshot\|ocr\|record` | `material3ui msg screenshot region` · `screenshot ocr` · `record region` |
| `material3uictl brightness up` | `material3ui msg brightness up` |
| `material3uictl lock` | `material3ui msg lock` |
| `material3uictl reload` | `pkill -x material3ui; material3ui &` |
