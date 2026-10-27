# IPC

The shell listens on `$XDG_RUNTIME_DIR/material3ui/ipc.sock`. `material3ui msg` sends one command and prints the reply:

```bash
material3ui msg <command> [args]
material3ui msg help        # every command, from the running shell
```

Hyprland binds, scripts and other programs all use the same commands. The defaults live in [`shell.lua`](../dotfiles/.config/hypr/material3ui/shell.lua).

**A second shell, for testing.** With `M3_INSTANCE=<name>` set, a shell runs beside the one you use, for example on a nested compositor started inside your session. It gets its own socket (`$XDG_RUNTIME_DIR/material3ui-<name>/`), and leaves what a session has only one of to your shell: Quick Share, the polkit agent, update checks, the clipboard history's watchers and your hooks (its events are still recorded). `material3ui msg` and `material3ui events` reach it when the same variable is set:

```bash
WAYLAND_DISPLAY=wayland-2 M3_INSTANCE=test material3ui &
M3_INSTANCE=test material3ui msg workspaces
```

## Panels

| Command | Opens or toggles |
| :--- | :--- |
| `launcher [text]` | Spotlight, optionally pre-filled: `=` maths, `:` emoji, `#` clipboard, `@` files, `>` run |
| `popup quicksettings [wifi]` | Quick Settings, or straight to its Wi-Fi list |
| `popup notifications` · `dashboard` · `overview` · `sysmon` · `power` | The notification centre, dashboard, workspace overview, system monitor and power menu |
| `popup audio` · `network` · `bluetooth` · `run` | The small status panels and the run dialog |
| `settings [page]` | Settings. Pages: `network`, `vpn`, `bluetooth`, `printers`, `defaults`, `notifications`, `tools`, `wellbeing`, `calendar`, `weather`, `battery`, `storage`, `monitor`, `sound`, `display`, `appearance`, `style`, `bar`, `dock`, `lockscreen`, `accessibility`, `lock`, `firewall`, `users`, `system`, `mouse`, `keybinds`, `hooks`, `packages`, `firmware`, `about` |
| `clipboard` | Clipboard history |
| `screenshot` | The screenshot tool |
| `wallpaper pick` | The wallpaper picker |
| `popup lockeditor` | The lock screen editor |
| `popup clock [alarms\|timers]` | Alarms and timers |

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
| `workspace <n>\|next\|prev` | Goes to a workspace (on niri, the focused monitor's own numbers) |
| `idle inhibit\|allow` | Caffeine: keeps the screen awake |
| `updates [list\|check\|upgrade]` | Pending updates (firmware's too), a check now, or the update itself (asks for the password) |
| `firmware [list\|check\|update [id]]` | fwupd's devices and their updates; updates one device, or all of them |
| `repos [list]` · `repos enable\|disable <repo>` | pacman's repositories, mirrors and held-back packages, and Flatpak's remotes; switches a repository |
| `alarm [list]` | Your alarms, each with its id |
| `alarm add <HH:MM> [days] [label]` | A new alarm. Days: `once` (the default), `daily`, `weekdays`, `weekends`, or a list such as `mon,wed,fri` |
| `alarm on\|off\|remove <id>` | Turns an alarm on or off, or deletes it |
| `alarm stop\|snooze [id]` | Stops or snoozes what's ringing (all of it, without an id) |
| `timer [list]` | Your timers and the time each has left |
| `timer start <length> [label]` | Starts one. Lengths: `5` (minutes), `1:30`, `1h20m`, `90s` |
| `timer pause\|resume\|reset\|remove <id>` | Controls one timer |
| `bar toggle\|show\|hide` | Status bar visibility |
| `bar edit\|done` | Edit the bar's widgets in place, or finish |
| `reload` | Re-reads `config.json` and the palette |
| `quit` | Exits the shell |

## Inspection

| Command | Prints |
| :--- | :--- |
| `ping` · `version` | `pong`, and the shell version |
| `status` | A summary of every service (audio, network, Bluetooth, battery, media…) |
| `outputs` · `workspaces` · `tray` | What the shell sees (`workspaces`: Hyprland's or niri's, each with its monitor) |
| `debug` | Every surface's node tree with positions (used by the UI tests) |
| `layout check` | Anything drawn outside its box (the card or pill around it) or off its surface, on every open surface: `ok: …` when there's none. A test can open each panel and check it, at any text size |

## Hooks and events

Hooks are your own commands, run when something happens: set them in **Settings → Hooks**, or here. Each event takes a list of commands, run one after another through `sh`. What happened reaches them as environment variables (`$M3_EVENT`, and each event's own below), never written into the command, so the text of a notification can't run as one. Their output goes to `~/.local/state/material3ui/hooks.log`.

| Command | Does |
| :--- | :--- |
| `hooks [list]` | Every event, and the commands set for it |
| `hooks set <event> <command>` | Adds a command to an event's list |
| `hooks clear <event>` | Removes an event's commands |
| `hooks fire <event> [NAME=value…]` | Acts as if the event happened: recorded, and its hooks run with these values |
| `hooks run <event>` | Tries an event's hooks now with sample values |
| `hooks events [n]` | The latest events (20 by default) |
| `hooks log [n]` | The end of the hooks' output |

**Following events from a script or a test.** Every event, with or without hooks, is written as a line of JSON to `$XDG_RUNTIME_DIR/material3ui/events.jsonl` (yours alone, and gone at logout; Settings → Hooks → Record events stops it):

```json
{"event":"screenshot_taken","time":"2026-10-25T21:30:02.481","ms":1792943802481,"vars":{"M3_FILE":"/home/me/Pictures/Screenshots/Screenshot_2026-10-25.png"}}
```

So a script, or an agent testing the desktop, can act and then wait for the result. `material3ui events` reads that file for you:

```bash
since=$(date +%s%3N)                  # now, in milliseconds
material3ui msg screenshot screen
material3ui events wait screenshot_taken --since "$since" --timeout 10 | jq -r .vars.M3_FILE
```

`events wait <event|*>` prints the first matching event and exits, or exits with status 1 when none comes within the timeout (30 seconds by default). With `--since`, an event that was already over before the wait began still counts, so there's no race between acting and waiting. `material3ui events` on its own prints every event as it happens.

`simulated: true` marks events made with `hooks fire`. Hooks use Noctalia's names where both have an event, so its hooks carry over.

| Event | When | Variables |
| :--- | :--- | :--- |
| `started` | Once the shell is up, at login | – |
| `session_locked` | The lock screen is up | – |
| `session_unlocked` | Back from the lock screen | – |
| `idle_started` | No input for a couple of minutes | – |
| `idle_ended` | Input again after being away | – |
| `suspending` | Just before the computer sleeps | – |
| `resumed` | The computer woke from sleep | – |
| `logging_out` | Just before (it waits up to 5 seconds for these) | – |
| `rebooting` | Just before (it waits up to 5 seconds for these) | – |
| `shutting_down` | Just before (it waits up to 5 seconds for these) | – |
| `wallpaper_changed` | A new wallpaper is set | `$M3_WALLPAPER_PATH` |
| `colors_changed` | A new palette is made (and the apps' themes with it) | `$M3_THEME_MODE` |
| `theme_mode_changed` | Dark theme turned on or off | `$M3_THEME_MODE`, `$M3_THEME_MODE_PREVIOUS` |
| `battery_charging` | The battery starts charging | `$M3_BATTERY_PERCENT` |
| `battery_discharging` | Unplugged: running on the battery | `$M3_BATTERY_PERCENT` |
| `battery_full` | The battery is full | `$M3_BATTERY_PERCENT` |
| `battery_low` | Below 15% on battery (once each time) | `$M3_BATTERY_PERCENT` |
| `battery_percentage_changed` | Each whole percent up or down | `$M3_BATTERY_PERCENT`, `$M3_BATTERY_STATE` |
| `power_profile_changed` | Power saver, balanced or performance | `$M3_POWER_PROFILE`, `$M3_POWER_PROFILE_PREVIOUS` |
| `network_connected` | Online through Wi-Fi or a cable | `$M3_NETWORK_NAME`, `$M3_NETWORK_TYPE` |
| `network_disconnected` | No network any more | – |
| `wifi_enabled` | Wi-Fi turned on | – |
| `wifi_disabled` | Wi-Fi turned off | – |
| `bluetooth_enabled` | Bluetooth turned on | – |
| `bluetooth_disabled` | Bluetooth turned off | – |
| `vpn_connected` | A VPN comes up | `$M3_VPN_NAME` |
| `vpn_disconnected` | The VPN goes down | – |
| `notification_received` | One arrives (shown or not) | `$M3_NOTIFICATION_APP`, `$M3_NOTIFICATION_SUMMARY`, `$M3_NOTIFICATION_BODY`, `$M3_NOTIFICATION_URGENCY` |
| `dnd_enabled` | Do not disturb turned on | – |
| `dnd_disabled` | Do not disturb turned off | – |
| `track_changed` | Another track starts in a player | `$M3_MEDIA_TITLE`, `$M3_MEDIA_ARTIST`, `$M3_MEDIA_ALBUM`, `$M3_MEDIA_PLAYER` |
| `workspace_changed` | Another workspace comes up | `$M3_WORKSPACE`, `$M3_WORKSPACE_PREVIOUS` |
| `monitor_added` | A monitor is plugged in | `$M3_MONITOR` |
| `monitor_removed` | A monitor is unplugged | `$M3_MONITOR` |
| `screenshot_taken` | One is saved | `$M3_FILE` |
| `recording_started` | A screen recording begins | – |
| `recording_saved` | A screen recording is saved | `$M3_FILE` |
| `focus_enabled` | Distracting apps pause | – |
| `focus_disabled` | Focus mode ends | – |
| `bedtime_started` | On schedule or by hand | – |
| `bedtime_ended` | Bedtime mode is over | – |
| `updates_available` | An automatic check finds new updates (firmware's too) | `$M3_UPDATES_COUNT`, `$M3_UPDATES_NEW` |
| `updates_installed` | Update now finishes | `$M3_UPDATES_COUNT` |
| `alarm_ringing` | An alarm goes off (and again after a snooze) | `$M3_ALARM_TIME`, `$M3_ALARM_LABEL` |
| `timer_finished` | A timer runs out | `$M3_TIMER_LABEL`, `$M3_TIMER_LENGTH` |

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
