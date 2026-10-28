# Changelog

## v6.0-beta-2 — 2026-10-28

- **Settings now changes niri itself.** On niri, the pages that used to send you to Hyprland Settings have their own controls. What you set is written as niri's own config, `~/.config/niri/material3ui-settings.kdl`, and niri picks it up at once. → [Material3UI on niri](docs/NIRI.md)
  - **Mouse & touchpad:** tap to click, natural scrolling (for the touchpad and for a mouse), ignore while typing, click with fingers or the pad's corners, tap and drag, drag lock, scrolling speed, pointer speed and acceleration.
  - **Keyboard:** your layouts, with **Next layout** to switch between them, the key that switches, repeat delay and rate, and your own shortcuts: a key combination and the command it runs.
  - **Display:** each monitor's resolution, scale, rotation, variable refresh rate, and on or off. They're applied at once and kept for the next login. If niri's own config sets the same monitor, niri's block wins, and the page says so.
  - **The cursor:** its theme (Bibata by default) and size, for niri and every app it starts.
  - **The focus ring:** drawn in the wallpaper's colours, and recoloured when the wallpaper changes.
  - **If niri can't read the shell's settings** (its `material3ui.kdl` isn't included, or niri refused the file), those pages say so at the top.
- **Night light works everywhere.** On niri the shell warms the screens itself, through the compositor's gamma control, so there's nothing else to install; on Hyprland it still uses hyprsunset. Settings and the Quick Settings tile now share one setting, so the tile turns it on at the temperature you chose rather than a fixed 4500 K.
- **Only what works is shown on niri.** Hyprland Settings, its links on other pages, colour filters and the magnifier stay on Hyprland. Accessibility says so on niri ("On Hyprland only") instead of offering switches that would do nothing.
- **Updating from 6.0-beta-1 on niri:** the shell adds the line that includes its settings to your `~/.config/niri/material3ui.kdl` by itself. New copies of [`material3ui.kdl`](dotfiles/.config/niri/material3ui.kdl) already have it.
- **Still on Hyprland only:** how windows look (gaps, borders, corners, shadows, animations, window rules, blur strength), arranging monitors side by side, minimising, picking a window to record, the screen-sharing picker, and the login screen. Set these in niri's `config.kdl` for now.
- **A second shell, for testing, keeps out of your way:** besides Quick Share, the polkit agent, update checks, alarms and screen time, it now leaves notifications and the tray to your main shell too.

## v6.0-beta-1 — 2026-10-28

- **Material3UI runs on niri too (beta).** The shell now works on [niri](https://github.com/YaLTeR/niri) 26.04 or newer, the scrollable-tiling compositor, as well as on Hyprland. It finds out which one it's on by itself, so the same install works in either session. → [Material3UI on niri](docs/NIRI.md)
  - **Workspaces:** each monitor's own, exactly as many as niri has (one more appears as you fill the last). A click goes to that monitor's workspace, and scrolling moves up and down.
  - **The window title, dock and island:** they follow niri's focus and windows. The dock focuses and closes windows, and the island shows workspace switches.
  - **The overview:** <kbd>Super</kbd><kbd>`</kbd> opens niri's own overview of the workspaces, with the shell's wallpaper behind it.
  - **Switching windows:** <kbd>Super</kbd><kbd>Tab</kbd> lists every window, most recently used first.
  - **Screenshots and recording:** a region or the screen as before. A window screenshot is taken by niri, then copied or opened in the editor as Settings says. Recording a window picks a region instead, since niri's windows scroll.
  - **The session:** the lock screen, idle and screens off, logging out, Digital Wellbeing, and hooks such as `workspace_changed` all work on niri.
  - **Setting it up:** copy `dotfiles/.config/niri/material3ui.kdl` into `~/.config/niri/` and include it at the end of your `config.kdl`. It starts the shell and binds its keys as on Hyprland (Spotlight, Quick Settings, the dashboard, the switcher, lock and more). The installer doesn't do this yet.
- **Not on niri yet**, coming in the next betas. Until then, set these in niri's own config:
  - **Settings that change the compositor:** mouse and touchpad, keyboard layouts, the cursor theme and size, displays, keybinds, night light, and window looks (gaps, borders, blur strength). They still write Hyprland's files.
  - **Hyprland only:** Hyprland Settings, minimising windows, the screen-sharing picker (niri's portal shows its own), and the magnifier and colour filters.
  - **The login screen** still runs on Hyprland, so it needs Hyprland installed.
- **One model of the desktop.** Inside, the shell no longer talks to Hyprland directly. It asks one compositor interface for screens, workspaces and windows, and for what to do with them: switch, focus, close, screens off, log out. Hyprland and niri each answer it, and features only one of them has are checked first. Nothing changes on Hyprland: it was checked feature by feature.
- **Fixed: a hidden dock left its blur behind.** On compositors that blur through the background-effect protocol (niri), the blur now moves and fades with what it's behind.
- **Go to a workspace from a script.** `material3ui msg workspace <n>|next|prev` works on either compositor.
- **A second shell, for testing.** `M3_INSTANCE=<name> material3ui` runs one beside yours, for example inside a nested niri, with its own socket. It leaves Quick Share, the polkit agent, update checks, alarms, screen time, clipboard history and your hooks to your main shell. `material3ui msg` reaches it with the same variable (see [IPC](docs/IPC.md)).
- **Pre-release versions.** The update check and What's new now understand versions like 6.0-beta-1 and put them in order: beta-1, then beta-2, then rc-1, then 6.0. The package is `material3ui-6.0beta1`, which pacman sorts the same way. Updates from 5.8 download it as before.

## v5.8.4 — 2026-10-27

- **Firmware updates**, in the new **Settings → Firmware**, through fwupd and the LVFS, where vendors publish firmware for laptops' BIOS and UEFI, SSDs, docks, touchpads and webcams.
  - **Updates:** each device with a newer release shows its version → the new one, the release notes, the size and how urgent it is. Install one with **Update**, or all of them with **Update all**. fwupd asks for the password itself.
  - **What's in the way:** an unplugged charger, a low battery or a closed lid is shown on the update instead of failing it. System firmware goes in as the computer restarts, and a banner offers the restart once it's ready.
  - **Your devices:** every device fwupd sees, with its version and whether it's up to date.
  - **Sources:** the LVFS and its testing list each have a switch.
  - **Without fwupd:** the page explains what it does and offers to install it.
- **Repositories and mirrors**, in a new **Repositories** tab in Settings → Software & updates.
  - **Repositories:** switch on multilib (for Steam and Wine) or the testing repositories, and add your own, optionally fetching and trusting its signing key.
  - **Safe changes:** each change to pacman.conf is checked by pacman first and then saved with one password, and the file before it is kept for **Undo**. Your comments and order stay as they were.
  - **Fastest mirrors:** **Find the fastest** times Arch's best mirrors in your country from your connection, and **Use these** saves the quickest ten.
  - **Downloads and held-back packages:** how many packages download at once, and packages to leave out of updates.
  - **Flatpak:** remotes listed with switches; add Flathub in one tap, or any other remote.
- **Automatic update checks:** every 6 hours, daily (the default) or weekly, firmware included. A notification tells you about new updates, and its **Review** button opens the list. Switches choose whether AUR, Flatpak and firmware updates are included. Installing is always up to you.
- **An Updates bar widget** shows how many updates are waiting. Click it to open Software & updates, or middle-click to start the update.
- **Bibata, the default cursor.** Bibata Modern Classic is used unless you've picked another cursor in Hyprland Settings → Cursor. New installs get it, and that page offers to install it where it's missing. The login screen uses the same cursor as the desktop.
- **For scripts:** `material3ui msg updates`, `firmware` and `repos`, and two hook events, `updates_available` and `updates_installed`.

## v5.8.3 — 2026-10-26

- **Alarms and timers**, as in Android's Clock. Open them from the new bar widget or with `material3ui msg popup clock`.
  - **Alarms:** ring once or on the days you pick, each with a label. Set the time by typing it, scrolling, or with the arrows.
  - **Timers:** count down in rings. Pause one, resume it, or add a minute. Start one with a tap (1 minute to 1 hour) or type a length such as `25`, `1:30` or `1h20m`. A running timer keeps going if the shell restarts.
  - **When one goes off:** a card at the top of the screen offers Snooze and Stop for an alarm, or +1:00 and Stop for a timer. The screens wake, and the sound plays over and over, getting louder each time. When the screen is locked, the card takes the place of the lock screen's widgets, so you can stop it without unlocking.
  - **Sleep:** an alarm or timer that comes due while the computer sleeps goes off when it wakes. An alarm missed by more than 10 minutes shows a "Missed alarm" notification instead of ringing.
  - **Widgets:** an **Alarms & timers** bar widget shows a running timer or the next alarm, and stops what's ringing with a click. The desktop and lock screen widget shows the next alarm with a switch, and your timers.
  - **Settings → Alarms & timers:** your alarms, the sound, how long a snooze lasts, and when an alarm nobody answers gives up.
  - **For scripts:** `material3ui msg alarm …` and `material3ui msg timer …`, and two new hook events, `alarm_ringing` and `timer_finished`.
- **Public holidays in the calendar**, in Settings → Calendar → Holidays. Pick your country (it's guessed at first from your weather location or language). Holidays show in red on the dashboard calendar, with the holiday's name, and in the day's agenda. Festivals that aren't days off can be shown too.
- **Fixed: panels no longer run off the screen.**
  - **Long lists:** the notification centre's Clear history no longer slips below the screen when the history is long; the same fix applies to every panel.
  - **Too tall for the screen:** a panel taller than the screen scrolls, as the dashboard does on a 720-pixel screen.
  - **Large text:** text no longer spills out of buttons or windows at large text sizes.
- **For tests:** `material3ui msg layout check` lists anything drawn outside its card or pill, or off the screen, on every open panel.

## v5.8.2 — 2026-10-25

- **Hooks:** your own commands that run when things happen, in **Settings → Hooks**. There are 41 events: the screen locking and unlocking, sleep and wake, logging out, a new wallpaper or colours, the battery and power mode, the network, Wi-Fi, Bluetooth and VPN, notifications, Do not disturb, the song, workspaces, screens, screenshots and recordings, and Focus and Bedtime.
  - **Commands:** each event takes one or more, run in order. **Run now** tries them with sample values, and each event shows its last result.
  - **Variables:** details of what happened reach your commands as environment variables, such as `$M3_FILE` or `$M3_NOTIFICATION_SUMMARY`. They're never inserted into the command itself, so a notification's text can't run as a command.
  - **Before power actions:** logging out, restarting and shutting down wait up to 5 seconds for their hooks to finish.
  - **Noctalia's names:** events use the same names as Noctalia's hooks where the two match.
- **For scripts and automated tests:**
  - **Event file:** every event is written as a line of JSON to `$XDG_RUNTIME_DIR/material3ui/events.jsonl`.
  - **Waiting:** `material3ui events wait <event> --since <ms>` waits for an event, even one that happened just before it started.
  - **Hooks from a terminal:** `material3ui msg hooks set|fire|run|list|events|log` sets up and runs hooks without opening Settings.
  - **Reference:** see [Hooks and events](docs/IPC.md#hooks-and-events).

## v5.8.1 — 2026-10-25

- **Fixed: slider values in Settings no longer break onto a second line** over their title ("45" above "%"). The fault came in with 5.8.0.
- **Lighter at rest.** Sitting idle, the shell now uses under 0.1% of one CPU core, down from about 4%. Long titles scroll three times and then rest, as on Android, and endless animations keep to their calm 30 fps. The README has the measured numbers and how to repeat them.
- **Synced lyrics.** The line being sung shows in the media player (dashboard, desktop and lock screen), and as a new **Lyrics** bar widget. Songs are looked up once on lrclib.net, a free lyrics library, and kept. Turn it off in Settings → Sound → Synced lyrics, and then nothing is sent.
- **Dock magnification and a taskbar style**, in Settings → Dock:
  - **Magnification** (Off, Small, Large): icons grow as the pointer passes over them.
  - **Taskbar:** the dock runs edge to edge along the bottom, apps from the left.
- **Keyboard navigation in Settings and Hyprland Settings.**
  - **Tab** and **Shift+Tab** move through the controls, and the **arrow keys** move to the nearest one that way.
  - **Enter** or **Space** presses the control, and a ring shows where you are.
  - **Ctrl+F** jumps to the search.
  - Hyprland Settings' **Ctrl+Z** undo now works.

## v5.8.0 — 2026-10-25

- **New bar looks**, in Settings → Status bar → Looks. Pick one with a tap, then fine-tune it:
  - **Material 3**, the default: pills along the top.
  - **Single bar:** one bar across the top, its widgets highlighted.
  - **Caelestia:** a bar down the left edge that runs on round the screen as a thin frame with rounded inner corners. It shows the system's logo, the workspaces, the window's title, the tray, the clock, status and power.
- **The bar on any edge:** top, bottom, left or right. On a side bar the widgets stack: the workspaces run down it, the clock shows hours over minutes, the window's title reads down the bar, and popups and tooltips open beside it. A side bar keeps its own widget arrangement, so your top bar's stays as it was.
- **A Framed style** for any edge, with the frame's thickness and corners in Settings, and a **Logo** widget.
- **The Clock widget opens the calendar** (right-click: notifications), as the island does.
- **Printers**, in Settings → Printers:
  - Add printers found on your network, or one by its address. No drivers needed (IPP Everywhere, AirPrint, Mopria).
  - Set the default, print a test page, pause or remove a printer, and see or cancel what's in the queue.
  - A print dialog (`material3ui msg print <file>`, or "Open with"): printer, copies, pages, two-sided, colour and orientation.
  - A Quick Settings tile, a bar widget and a desktop widget.
  - Most printers need Ghostscript to print documents; if it's missing, Settings offers to install it.
- **Wallpaper colours as on Android.** Each swatch shows the palette it makes, in three colours. Each of your wallpaper's colours comes in four styles: Content, Tonal, Vibrant and Expressive. Basic colours use the same swatches, and the Style row follows your pick.
- **Smoother bar and panels.** Rounded corners no longer show stepped, pixelated edges where the blur met them, and the blur's grain is gone.
- **Text no longer runs under buttons** in Settings rows: long text wraps or shortens to fit, at any text size.

## v5.7.5 — 2026-10-25

- **A friendlier lock screen editor.** Your lock screen stays on the left, and a side sheet on the right follows what you pick:
  - **Nothing picked:** the widget library, grouped into Clock & date, Calendar, Weather, Music and Status. Tap a widget to add it.
  - **An empty cell:** add a widget right there.
  - **A widget:** its details, a pad to move it, **Replace** and **Remove**.
- **No more blank cells.** A widget that's hidden right now, such as the music player with nothing playing, shows as a labelled placeholder that says when it appears.

## v5.7.4 — 2026-10-25

- **Scrolling Digital Wellbeing no longer smears the page.** The chosen day's app rows lagged behind while scrolling and drew over the week chart and the app timers. The notification centre had the same problem; both are fixed.
- **New widget: Screen time: week**, for the desktop and the lock screen. It shows a bar for each day, with the daily average and today's total.

## v5.7.3 — 2026-10-25

- **Tap a day in Digital Wellbeing**, as on Android. In the week chart, tap a day's bar to see that day: its date, screen time, unlocks and notifications, and every app you used with its time and a bar. The chart highlights the chosen day, and the list glides as you switch days.

## v5.7.2 — 2026-10-25

- **Digital Wellbeing settings take effect at once.** Bedtime's greyscale and Do Not Disturb switches used to wait up to 10 seconds to apply, so they looked broken. They're now labelled "Greyscale at bedtime" and "Do not disturb at bedtime", with a hint to use **Start now** to see them.
- **A `wellbeing` command:** `material3ui msg wellbeing [status|bedtime on|off|focus on|off]`.

## v5.7.1 — 2026-10-25

- **New bar widgets** (Settings → Status bar):
  - **Screen time**, today's total.
  - **Focus mode** and **Bedtime**, a click to switch.
  - **VPN**, the one in use; click to connect or disconnect.
  - **Quick Share**, with a transfer's progress; click to send files.
  - **Firewall**, on or off.
- **New desktop widgets:**
  - **Screen time**, a ring of today's apps with the total.
  - **Focus & Bedtime**, a button each.
  - **VPN**, with a switch.
  - **Quick Share**, with Send.

  The lock screen can show Screen time too.

## v5.7.0 — 2026-10-25

- **Digital Wellbeing**, as on Android, in Settings → Digital Wellbeing:
  - **Screen time:** today as a ring of your apps with the total, plus unlocks, notifications and your daily average, and the week as bars. Only time spent unlocked and at the computer counts.
  - **App timers:** a daily limit per app. A minute's warning, then the app closes for the rest of the day.
  - **Focus mode:** pick distracting apps; while Focus is on, they close when opened.
  - **Bedtime mode:** on a schedule or right away, the screen turns greyscale and Do Not Disturb comes on, then both go back.
  - **Screen time reminders:** a nudge to take a break after a stretch on screen, and a daily goal.
  - **Reduce interruptions:** Do Not Disturb, and a switch for each app that notified you this week. Muted apps' notifications don't show; critical ones still do.
  - **Quick Settings tiles** for Focus mode and Bedtime.

## v5.6.3 — 2026-10-25

- **The music player no longer shifts the dashboard.** The player card is now the same height whether or not something is playing, so the system monitor stays put. The dashboard's two columns now end level with each other.
- **A tidier player card.** Elapsed time, the wavy progress bar and the time left share one line, as on Android. The card is a little more compact everywhere: dashboard, desktop, lock screen and Quick Settings.
- **Quick Share's own icon in Quick Settings**, on the tile itself, in the tile editor and in the list of tiles to add.

## v5.6.2 — 2026-10-24

- **Notification history.** Dismissed and cleared notifications are kept for 30 days (up to 300) and survive restarts. The notification centre's **History** tab shows them under **Today**, **Yesterday** and **Earlier**. Tap one to open its app, remove single ones, or clear them all. Turn history off in Settings → Notifications.
- **Notification pile-ups, as on Android 16.** Several notifications from one app stack into one card, the newest on top with the others peeking out underneath and a **+N** chip. Tap it to fan them out into a connected group; **Less** stacks them again.
- **Material 3 Expressive motion in the centre.** Cards glide into place when others arrive or leave, and new ones pop in. Swipe a card sideways to dismiss it: it follows your pointer and flies off, or springs back if you let go early.
- **No pop-ups over the open notification centre.** New notifications appear in the centre instead.

## v5.6.1 — 2026-10-24

- **Android's Settings icon.** The Settings app has Android's icon: a gear with a long shadow on a circle.
- **It follows your wallpaper's colours**, as Android's themed icons do: a deep tint behind a light gear in dark mode, a pale tint behind a deep gear in light mode. The dock, Spotlight and other launchers update as soon as the wallpaper changes.
- **No minimise button** on the Settings and Hyprland Settings windows. Hyprland has no minimised state; maximise and close remain.

## v5.6.0 — 2026-10-24

- **VPN in Settings.** A new VPN page next to Network: your WireGuard and other NetworkManager VPNs, each with an on/off switch, plus **Add a VPN** from a WireGuard `.conf` or OpenVPN `.ovpn` file. Imported VPNs stay off until you turn them on. Cloudflare WARP is supported too when installed. A new VPN tile for Quick Settings turns your last VPN on or off.
- **Firewall in Settings.** A new Firewall page next to Security & privacy, for ufw or firewalld:
  - Turn it on or off, and choose whether incoming connections are blocked.
  - Let apps through in one tap: Quick Share, KDE Connect, LocalSend, Syncthing and SSH.
  - See and delete rules, or add your own by port.
  - No firewall yet? Install ufw from there.
- **Export and import your settings.** Settings → System → Backups saves the shell's settings as a JSON file in Downloads, and imports one back (your current settings are backed up first). Also from the command line: `material3ui msg config export|import`.
- **Material 3 Expressive motion.** Animations now run on springs, so interrupted motion stays smooth:
  - Buttons squish when pressed and bounce back.
  - Switches bounce across.
  - Quick Settings tiles morph between circle and rounded square.
  - Popups pop in.
  - Notifications spring in, and the others glide into place when one comes or goes.
- **Cooler and lighter.** Endless decoration (the wavy music progress, scrolling titles, spinners) now draws at 30 fps instead of at full refresh rate. With music playing, the shell's CPU use drops by almost half.
- **Quick Share's own icon**, in Quick Settings, its popups, Settings and notifications.

## v5.5.3 — 2026-10-23

- **Clipboard previews.** Spotlight's clipboard (`#`) and the clipboard panel show a small preview instead of text:
  - Screenshots and other copied images show a thumbnail, with their type, size and file size (e.g. PNG · 1044×493 · 141 KiB).
  - A picture or video copied from a file manager shows the picture, or a frame from the video.
  - Type `#image`, `#screenshot` or `#video` to find them.
- **New screenshots in the README**, including Quick Share with its QR code.

## v5.5.2 — 2026-10-23

- **Quick Share with a QR code.** In the send popup, choose **Use a QR code** and scan it with the phone's camera. The files go straight to the phone, without Bluetooth and without the phone set to "Everyone". Tested with a real Android phone.
- **Phones can send to you through a firewall.** If ufw or firewalld blocks incoming connections, a phone sees this computer but can't send to it. Settings → Connected devices now warns about this and offers **Allow**, which opens Quick Share's port (47893) after asking for your password.
- **Choose files in the shell's own picker.** "Choose files" and "Add files" open a Material You picker: any files, several at once, with previews for pictures. The old system dialog opened behind the shell's popups.
- **"Log out to finish updating" goes away after you log out.** Update times from pacman's log lost their time zone, so in time zones ahead of UTC an update could look newer than your login and the banner never cleared. This is fixed and covered by a test.
- The file picker's Cancel and Add buttons sit on its card again, and a new Quick Share transfer replaces the open popup instead of closing it.

## v5.5.1 — 2026-10-23

- **Quick Share, built in.** Send and receive files with Android phones and Chromebooks on the same Wi-Fi, with no app to install:
  - **Receiving:** each transfer is asked first, with the sender, the files and the PIN to match with the phone's. Files go to Downloads; links and text go to the clipboard.
  - **Sending:** Settings → Connected devices → Send files, "Open with → Quick Share" in a file manager, or `material3ui msg quickshare send <files>`. Pick a device nearby, then check the PIN and watch the progress.
  - Settings → Connected devices sets whether you're visible and the name others see. A Quick Settings tile switches visibility.
  - Phones find this computer through a Bluetooth signal when sending, so turn Bluetooth on. With it off, a phone shows up only when its Quick Share is set to "Everyone".
- **A calendar.** Add your own events, or subscribe to Google, Outlook, Nextcloud or other calendars by their ICS link. Events can repeat, and you get a reminder notification before each one. The dashboard's month view marks days with events and shows a day's agenda; there's an Agenda widget for the desktop and lock screen. Manage it in Settings → Calendar.
- **Wallpaper colours, as on Android 16.** The accent picker offers your wallpaper's own colours as well as the basic ones.
- **Settings icons in Android 16's colours.** With themed icons on (Look & feel), they shift towards your theme's colours.
- **Emoji in colour.** Emoji in the launcher (and everywhere else) were drawn as flat silhouettes.
- **The media player's title scrolls** instead of running past the card's edge.
- **A clearer updates list.** Software & updates groups pending updates by source and shows the installed version → the new one.
- **Indic and other scripts:** noto-fonts is now among the installer's fonts.

## v5.5.0 — 2026-10-20

- **A built-in screen recorder.** The screenshot tool (Super+G) has a Record tab:
  - What to record: a region, a window or a whole screen.
  - Format: MP4, WebM, MKV or GIF (made with its own palette).
  - Resolution (native, 1080p, 720p, 480p), frame rate and quality.
  - Sound: none, system sound or the microphone.
  - Hardware encoding (VA-API) and a countdown.
  - While it records, the island shows a red timer; click it to stop. Super+Shift+R still starts and stops a recording straight away.
- **Screen sharing with the shell's own picker.** When an app (a browser, a call) asks to share your screen, a Material You dialog asks what it may see: a screen (with a live preview), a window or a region you draw. You can let the app share again without asking. Settings → Tools → Screen sharing sets the picker, remembering and the frame rate.
- **A Material You file picker** for wallpapers and your profile picture: places down the side, folders and picture previews, replacing the old system dialog.
- **Default profile pictures:** twelve Material Expressive shapes to pick from in Settings → Users → Change picture.

## v5.4.6 — 2026-10-20

- **Dock settings no longer reload the shell.** On a fresh or reset setup, opening Settings → Dock reloaded the whole shell, and with the dock switched on it could reload over and over. The dock's pinned apps are now saved once, and a setting that hasn't changed is never written again.

## v5.4.5 — 2026-10-20

- **A lighter blur behind widgets.** Cards on the desktop, lock and login screens now soften the wallpaper instead of smearing it. Adjust it in Look & feel → Widget blur strength.

## v5.4.4 — 2026-10-20

- **Ticks you can see.** Switches show their ✓ in every palette; in some dark palettes it had matched the switch and vanished. The chosen accent colour now carries a ✓ too.
- **App theming switches.** Wallpaper & style → App theming lists every app that takes Material You colours from your wallpaper: Hyprland, GTK and Qt apps, terminals, browsers, editors and more, marking the ones not installed. Switch any of them off and it keeps the colours it has, instead of changing with the wallpaper.

## v5.4.3 — 2026-10-20

- **Hyprland's options live in Hyprland Settings only.** The shell's Settings no longer keeps its own copies of them, which could disagree. Where they used to be, it links straight to the right Hyprland Settings page:
  - Mouse & touchpad: touchpad, pointer and gestures.
  - Look & feel: the blur's strength and look, window opacity, shadows and corners, and window animations.
  - Accessibility: the magnifier's zoom level and more cursor options.
- **Shell-only settings stay where they were.** Blur and opacity for the bar, panels, dock and widgets, the shell's corners and animation speed, scrolling, cursor size and the magnifier shortcuts.

## v5.4.2 — 2026-10-20

- **The dock no longer crashes the shell.** With the pointer over the dock, a settings change could crash the shell, over and over, as it restarted.
- **Backups are safe to restore.** Settings → Backups lists only your settings backups. It used to offer the installer's backups too, and restoring the oldest one deleted the whole Hyprland configuration. A restore now checks the backup holds only settings files before touching anything.
- **Clear confirmations.** Restore, Delete and Reset ask in a strip under the row, saying what will happen, with Cancel. Reset only clears your settings once their backup is saved.
- **The wallpaper you pick is the one you get.** A wallpaper shown before no longer leaves the old one on screen while the colours change to the new one.
- **Keyboard settings open** from System, with links to layouts and typing, and to keybinds.
- **Smoother scrolling** in both Settings apps and every panel, with Look & feel → Scrolling for smooth wheel steps, scroll speed and touchpad glide.

## v5.4.1 — 2026-10-20

- **Restart after updates.** Software & updates shows when an update needs a restart, however it ran: the computer (a new kernel, systemd, glibc, drivers, firmware), a log out (Hyprland's libraries) or just the shell. It names the packages, with Restart now, Log out or Restart shell, and the update's notification says so too.
- **Settings backups with restore.** System → Backups keeps timestamped backups of your shell and Hyprland settings, made when you ask and before every update, restore and reset. Each shows its date, time, reason and size, with Restore (what's there now is backed up first) and Delete. The installer's backups are listed too. Also from a terminal: `material3ui msg backup [list|restore]`.
- **No more flicker when settings change.** The desktop is refreshed in place instead of recreated, the bar and dock no longer make windows re-lay out for nothing, and blur and touchpad changes apply live without reloading Hyprland.

## v5.4.0 — 2026-10-20

- **New clock styles.** Desktop widgets add a stacked two-tone clock, a Material You analog clock (a scalloped dial with rounded hands and an orbiting seconds dot) and a minimal analog clock. The lock and login screens offer Stacked, Two-tone, One line, Analog and Minimal analog.
- **More widget styles** for the desktop, lock and login screens. Calendars: a day tile, a week strip and a month grid. Music: a disc in a wavy progress ring, and a slim bar player. Weather: a big temperature, and a card with the next four days.
- **Text and widget size.** Look & feel sets the text size of the whole shell, the status bar, and the lock and login screens, plus a Widget size that grows widgets whole. In the desktop editor, each widget also has its own size (− and +).
- **Blur behind widgets** on the desktop, lock and login screens, with its own strength.
- **Bar pills:** adjustable corner roundness and border width.
- **Clearer time and date settings** for the island and the Clock widget: a live preview, "Same for both", and plain choices (24/12-hour, seconds, date and its format).
- **The lock editor's preview** shrinks a big clock to fit its cell instead of cutting it off.

## v5.3.3 — 2026-10-20

- **A floating bar.** Settings → Status bar → Style now offers Pills, Full width and **Floating bar**: one bar across the screen that stands off its edges, with round, curved or square ends, its own width (40–100%) and gap from the edge.
- **Pill highlight**: fill the widgets' pills in the surface colour, a tonal or accent colour, as an outline, or not at all, in any bar style (Auto keeps them as before).
- **Blur, your way.** Look & feel adds the blur's smoothness (passes), noise, contrast, brightness and vibrancy. The bar and the dock each get their own blur switch and opacity, in their Settings pages.
- **The window switcher's card is blurred** like the rest of the shell.

## v5.3.2 — 2026-10-20

- **A welcome tour on a new install**, in its own window: pick a dark or light theme and an accent, choose a wallpaper, place the bar and the dock, pick a 12- or 24-hour clock, learn the keys worth knowing, then jump to Settings, Hyprland Settings or the desktop editor. Skip it any time; it's in Settings → About to take again.
- **What's new after every update**: once the new version starts, a popup shows its release notes (and those of any versions you skipped), with a link to all of them.
- **The changelog in Settings.** Software & updates shows what's new in the installed version, and About lists the latest releases (all of them on request), with buttons for What's new and the welcome tour.
- **The window switcher's card gets the shell's blur.**

## v5.3.1 — 2026-10-20

- **Switch windows with Super+Tab**, as on Windows and macOS: hold Super and tap Tab to step through every open window, on every workspace, most recently used first, shown as a row of app icons with the window's title and workspace. Let go of Super to switch to it (its workspace comes with it); Shift+Tab or the arrows go back, Enter or a click picks one, Escape stays put. A quick Super+Tab flips straight back to the previous window.
- **The overview moves to Super+`.**

## v5.3.0 — 2026-10-20

- **Desktop widgets and shortcuts.** Put widgets on the desktop (a big clock, the date, a month calendar, weather with the days ahead, the forecast, a music player, system meters, battery and notifications), and shortcuts to apps and files or folders that open with a click.
- **A desktop editor.** Right-click the desktop → Edit widgets and shortcuts (or Settings → Wallpaper & style): drag each one where you want it, remove it with its corner button, and add more from the toolbar. Places are kept per screen size, so they hold on any resolution.
- **A right-click menu on the desktop**, as on other systems: edit widgets, change or skip the wallpaper, open a terminal, display settings, personalise, and both settings apps.
- **Time and date your way.** The island, the bar's clock and the desktop clock each choose a 24- or 12-hour time, with or without seconds, whether to show the date, and its format, all shown as they read right now.
- **The window title shows its app's icon**, fixed beside the title as it scrolls.

## v5.2.1 — 2026-10-20

- **Scrolling text loops like a ticker.** Long titles now scroll continuously: the text, a gap, and its beginning again, pausing as the start comes round.
- **Choose how each one scrolls.** Settings → Status bar → Scrolling text sets the window title, the island's music and its notifications to loop, go back and forth, or stay still, and the speed for all of them.

## v5.2.0 — 2026-10-20

- **The battery tells the truth.** Plugging in or out shows at once, even on a full battery; a battery held at a charge limit reads "Plugged in, not charging" instead of charging; time left is estimated on laptops whose battery reports no power draw; the charging icon shows how full it is. Hover the battery for the whole status.
- **The island opens the dashboard while music plays.** Clicking the island shows the calendar and weather whatever it's showing; its media buttons still play, pause and skip.
- **Long titles scroll.** The window title in the bar and the island's track and notifications take less room and scroll slowly to show the rest when they don't fit.

## v5.1.1 — 2026-10-19

- **Live tooltips.** A tooltip on the bar follows what it describes while it shows: the volume as you change it (or muting), the Wi-Fi network you join, Bluetooth turning on or off, the battery charging and notifications arriving.

## v5.1.0 — 2026-10-19

- **Tooltips on the dock and the bar.** Rest the pointer on a dock icon to see the app's name (and how many windows it has open), or on the bar to see what each thing is: the workspace, your Wi-Fi network, Bluetooth, the volume, notifications, the battery, the playing track, tray apps and the panel buttons. Material 3's plain tooltips, a moment after you stop, gliding straight to the next one as you move along.

## v5.0.3 — 2026-10-19

- **One password per update.** Updating from Software & updates asks once ("Install updates") for everything: the system, the AUR, system Flatpaks and Material3UI itself with its login screen. A small helper, started by that one prompt, runs only the update's own steps (pacman, Flatpak updates, Material3UI's packages) and exits when the update ends. This update itself still asks a few times, since it brings the helper.

## v5.0.2 — 2026-10-19

- **About shows the logos:** your distribution's beside its name (from its own os-release, so every distro shows its own) and Hyprland's beside the compositor.

## v5.0.1 — 2026-10-19

- **Both settings apps on the dock.** Material3UI Settings and Hyprland Settings are pinned: on new installs, and once on existing ones (unpin either and it stays off). Hyprland Settings is in the app launcher too.
- **Hyprland Settings at the top** of Material3UI Settings' sidebar, just under the search (and Material3UI Settings at the top of Hyprland Settings').
- **The installer no longer offers EasyEffects and Cloudflare WARP.**

## v5.0.0 — 2026-10-19

**Hyprland Settings: one app for the whole compositor.** A second settings app, alongside Material3UI Settings and linked from it, for everything Hyprland does. It brings in the features of [HyprMod](https://github.com/BlueManCZ/hyprmod), rebuilt natively in the shell's own Material 3 Expressive design. Every change applies live, is saved at once, undoes with Ctrl+Z, and resets per option to what your config files had.

- **Every Hyprland option.** All of them, read from the running Hyprland (new versions' options appear on their own), on pages for windows and borders, decoration, layouts, input devices, gestures, cursor, window groups, behaviour, rendering and debug, with switches, sliders over Hyprland's ranges, choices, colour and gradient fields and per-side gaps. Hyprland's config errors show on top; every option is in search.
- **Displays.** Drag monitors into place; resolution and refresh rate, scale, rotation and flipping, variable refresh rate, 10-bit colour, colour modes including HDR (with SDR brightness and saturation) and mirroring. Anything that could leave the screen unusable reverts after 15 seconds unless you press Keep.
- **Animations and curves.** Every animation's length, curve and style, or what it inherits; a curve editor with draggable points, overshoot, Material and classic presets and a live preview.
- **Keybinds.** Press a shortcut to set it (Hyprland's own binds pause while you do). Your binds can do any of about thirty actions or run a command, and mouse drags (move, resize) are binds too.
- **Rules.** Window rules (match apps, or pick an open window, then any of Hyprland's 58 effects), layer rules for bars, launchers and notifications, and workspace rules.
- **Startup apps and environment variables.**
- **Cursor themes,** applied live to Hyprland, GTK and new apps.
- **Plugins.** A catalogue to download and install (hyprexpo, hyprbars, hyprtrails, dynamic cursors, touch gestures and more), built by hyprpm, which installs itself if missing; your hyprpm plugins with switches; any plugin file.
- **Profiles.** Whole setups saved by name and switched in one go, also from a keybind: `material3ui msg hypr profile next`.

Also: the Keybinds page moves to Hyprland Settings (Settings → Keyboard points there), Settings → Display links to Displays, `material3ui msg hypr …` scripts all of it, and a crash when saving rules (and a latent misread of monitors, curves and animations) is fixed.

## v4.4.6 — 2026-10-19

- **Forget networks and devices.** Saved Wi-Fi networks (in Quick Settings and Settings) and paired Bluetooth devices (in Quick Settings; Settings already had it) get a forget button. It asks twice: the first tap turns it red, a second within four seconds forgets. A forgotten Wi-Fi network asks for its password again next time.

## v4.4.5 — 2026-10-19

- **Network & internet, reorganised.** Wi-Fi comes first, then Ethernet. The separate Connection section is gone: the connected network opens like a dropdown (tap it or its chevron) to show live download, upload and link speed, with band, security, addresses, hardware address, interface and data used, and a Disconnect button. On a cable, the adapter in use opens the same way.

## v4.4.4 — 2026-10-19

- **Smoother scrolling everywhere.** Touchpads move lists exactly with your fingers and glide to a stop when you lift them mid-swipe; mouse wheel notches ease into each other instead of stuttering. Sliders and buttons that react to scrolling (volume) still step as before.
- **Settings blurs its edges:** in both panes, content softens into a smooth blur at the top and bottom while there is more to scroll to; the scrollbar stays sharp.

## v4.4.3 — 2026-10-19

- **Wavy loaders, as Material 3 Expressive.** While Software & updates checks or installs, the status icon becomes a wavy arc that spins and breathes on a faint track, and a wavy stretch sweeps along a bar beneath it. The lock and login screens show the same loader in the submit button while your password is checked.
- **Music progress ring:** while music plays, the dynamic island rings its equaliser with the track's progress, wavy while playing and flat when paused. The island's media view and the bar's media widget have it too.

## v4.4.2 — 2026-10-19

- **Lock screen: a correct password is no longer refused.** The first try could fail and the second succeed with the same password: the shell collected the password checker's helper process before PAM could read its answer. It now only collects processes it started itself, which also fixes the same race for other tools that wait on their own helpers.
- **Login screen synced notification** shows plain words instead of terminal colour codes.

## v4.4.1 — 2026-10-19

- **The shell is a pacman package.** Each release now carries `material3ui` (the shell) next to `material3ui-greeter` (the login screen). On the stable channel, updates download the release's package and install it with pacman instead of compiling; on the latest channel they build the checkout and package it, so pacman owns the files either way. The first update takes over the files an earlier install put in place.
- **Package icons:** both packages carry an icon (the logo, and the logo with a padlock for the login screen) without appearing in app lists. Material3UI's own update shows its logo in Software & updates.
- **Eight new wallpapers**, drawn from Material 3 Expressive shapes in light and dark palettes: ten ship with the shell now.
- **Screenshots** in the README.

## v4.4.0 — 2026-10-19

- **Accessibility** (Settings → Accessibility):
  - **Text:** Large and Larger text sizes, and bold text, across the whole shell.
  - **Colour correction:** a filter over the whole screen, apps included: grayscale, inverted colours, red-weak, green-weak and blue-weak corrections, and extra dim.
  - **Magnifier:** zooms in around the pointer. Super+= zooms in, Super+- zooms out and Super+Backspace resets; there is also a slider.
  - Remove animations, Reduce transparency and Cursor size sit alongside them.
- **Login screen, redone.**
  - **Session picker:** a chip in the corner that opens a card of sessions, each with a description and a tick on the one you'll start.
  - **Power menu:** one power button that opens Sleep, Restart and Shut down, each labelled, instead of three bare icons that acted on one tap.
  - **Its own package:** `material3ui-greeter` is now a pacman package, attached to every release from this one on. Installing and updating take the release's package, or build one between releases. greetd comes with it as a dependency.
  - **Updates from Settings:** Settings → Lock screen shows whether the login screen is up to date, with Update (or Install). Shell updates from Settings update it too, through the password dialog.
- **Users** (Settings → Users): your profile picture, name and password, administrators, and new profiles.
- **System monitor** in Settings (Ctrl+Alt+Delete): every process with its state and owner, with stop and restart.
- **Quick Settings:** a two-row header with room for everything, your profile and the battery, and no separate battery and connection cards.
- **Lock screen music player** while something plays.
- **Fixes:** cleaner blur edges; the dock stays out while its right-click menu is open; even names in the editors; widget alignment.

## v4.3.2 — 2026-10-19

- **Music player, as Material 3 Expressive.** The album art fills the card under a tint, with the title and artist, a wavy progress line that moves while playing and lies flat when paused (click or drag it to seek, with elapsed and remaining time), and a play button that's a circle while paused and a rounded square while playing. Full size in the Dashboard, compact in Quick Settings. Cover art from the web (Spotify, browsers) now shows too.
- **Touchpad settings** (Settings → System → Mouse & touchpad): tap to click, natural scrolling, ignore while typing, click with fingers, tap and drag, drag lock and scroll speed, plus pointer speed and acceleration. They apply at once.
- **Dock menu:** right-click an icon for Open or New window, Pin to dock or Unpin, and Close window (or close all of its windows).
- **Quick Settings scrolls** when it's taller than the screen, as on a small screen with music playing.

## v4.3.1 — 2026-10-19

- **Crash-proof shell.**
  - **Restarts itself:** if the shell ever crashes, it comes back within a second with a notification. Five crashes in a minute stop it from looping. Quitting, the restart key and updates still just quit.
  - **Crash log:** the signal, time and a backtrace go to `~/.local/state/material3ui/crash.log`.
  - **Errors stay small:** an unexpected error in a timer, an input or window event, or a D-Bus reply or request is logged and skipped instead of closing the shell.
- **Apps** (Settings → Apps): every installed app with its icon and where it came from (Official, AUR, Flatpak, a local shortcut or installed manually), with **Open** and **Uninstall** (through the polkit dialog; a second tap confirms). Tap an app for its details: version, installed size, its data in your home folder, install date and description. Searchable.
- **Ethernet** (Settings → Network & internet): each wired adapter's state (cable unplugged, disconnected, connecting, or connected with speed and address) with Connect and Disconnect; USB adapters and docks appear as they're plugged in.
- **Update channel** (Settings → Software & updates): Stable (releases) or Latest (git, the newest commits on main) for Material3UI's own updates.

## v4.3.0 — 2026-10-19

- **Quick Settings, as Android 17.**
  - **Header:** a large clock and the date, with round edit, settings, lock and power buttons.
  - **Sliders:** brightness and volume on tall, rounded tracks with the icon at the end; tap the speaker to mute.
  - **Tiles:** large pill tiles with the icon in a chip of its own. Wi-Fi and Bluetooth light the chip when on (it toggles; the rest of the tile opens the device list), other tiles fill when on. Small round icon tiles beside them.
  - **New tiles:** Sound, Dark theme, Airplane mode, Power saver and Screenshot.
  - **Editor** (the pencil): your tiles in a neutral state. Hold and drag one to move it. Tap one to select it: its corner badge removes it, and dragging the handle on its right edge stretches it to large or shrinks it to small. Tap a tile below to add it.
- **Frosted glass:** panels, the bar and tiles are translucent over a stronger blur, as Android's shade. Opacity and blur stay adjustable in Look & feel.
- **Settings, as an app.**
  - **A window of its own:** title bar with minimise, maximise and close, a dock icon you can pin, and it stays open beside Quick Settings and the other panels. Minimised, it comes back from the dock or `material3ui msg settings`.
  - **Android 17 layout and order:** a large title, a search field, and the pages in rounded groups with tonal icon circles and summaries: Network & internet, Connected devices, Apps, Notifications, Battery, Storage, Sound, Display, Wallpaper & style, Accessibility, Security & privacy, System and About, with the shell's own pages beside their nearest kin.
  - **Universal search:** finds settings by name across every page, not just the pages; a result opens its page, scrolls to the setting and highlights it. `material3ui msg settings search:<words>` searches from scripts.
  - **New pages:** Storage (disk use, home folder sizes, cleaning the package cache, thumbnails and trash), Accessibility (remove animations, reduce transparency, cursor size) and System (automatic time, time zone and language pickers, a backed-up reset).
  - **Material 3 Expressive details:** switches with ✓ and ✕ in the thumb, larger rounded groups, texts that wrap instead of cutting off.
- **Editors:** the Status bar editor shows each zone's widgets as chips; tap one for a toolbar to move, group or remove it, and add widgets from the icons below. The lock screen editor takes the same look.
- **Updates through the shell's own dialog:** updates, installs and cleaning the package cache ask for your password in the shell's polkit dialog and run in the background, instead of opening a terminal for sudo. "Use a terminal" remains for conflicts that need an answer.
- **Shell updates:** Check now also looks for a new Material3UI release (or new commits on the latest channel), and Update now installs it.
- **Fixes**
  - **Installer:** LibreWolf installs from Arch's extra repository (librewolf-bin left the AUR).
  - **Software & updates:** the Update now button stays inside its card.
  - **update.sh** restarts the shell correctly with Lua Hyprland configs.

## v4.2.1 — 2026-10-19

- **Smoother Settings and scrolling.**
  - **Layout:** sizes are remembered between changes, so a layout measures each widget about once instead of thousands of times, and animations no longer lay the window out at all.
  - **Scrolling:** wheel steps and scrollbar drags move only the scrolled content instead of laying out the whole window every frame.
  - **Drawing:** rows scrolled out of view are skipped, and text stays cached for 30 seconds, so scrolling back or reopening a panel no longer redraws it from scratch.
  - **Profiling:** `M3_DEBUG_FRAMES=1` logs layout and paint times and any step that blocks for more than 8 ms.
- **Live Wi-Fi signal:** the bar's Wi-Fi bars follow the live link quality, read every 3 seconds (NetworkManager only updated it on rescans).
- **One network manager:** when the installer turns NetworkManager on, it offers to turn off iwd and systemd-networkd. Running them side by side made the Wi-Fi switch between them and the shell show no connection.
- **Bar editor:** widgets move up and down with ↑/↓ on each row (into the next column at either end). Tapping a row selects it, and the row's buttons now fit and line up.

## v4.2.0 — 2026-10-18

- **Bluetooth pairing with codes.** The shell is now the BlueZ pairing agent.
  - **Codes:** it shows a 6-digit code to compare, a code to type on a keyboard, or a box for a PIN shown on the device, and asks before a device may connect.
  - **Nearby devices:** Settings → Bluetooth lists **My devices** and **Nearby devices**; the Bluetooth tile in Quick Settings and the bar's Bluetooth popup show the same list. Tap to pair, connect or disconnect.
  - **Progress:** each device shows "Pairing…" or "Connecting…", and failures say why in plain words.
  - **From the other side:** when a phone starts pairing and no list is open, a pairing dialog opens.
  - **Searching** runs only while a list is open; unnamed devices (beacons) are hidden.
- **Battery & power.**
  - **Settings → Battery & power:** live charge with a level bar, time left, power draw and battery health.
  - **Power modes:** Power saver, Balanced and Performance (power-profiles-daemon), in Settings, Quick Settings and a new popup from the bar's battery widget.
- **Default apps** (Settings → Default apps): browser, files, terminal, email, text, images, video, music, PDF and archives. The browser, file manager and terminal also set Super+B, Super+E and Super+Return.
- **Themes for more apps.** Wallpaper colours now reach Firefox and LibreWolf (on next start), VS Code, Code - OSS, VSCodium, Cursor and Windsurf (pick **Material3UI** as the colour theme), foot, ghostty, WezTerm and zathura. Only installed apps are themed.
- **Wallpaper & colour.**
  - **Gallery:** every wallpaper side by side; a click applies it. The wallpapers the shell ships are always listed.
  - **Fonts:** dropdowns of the installed fonts, searchable, each shown in its own face.
- **Launcher:** apps installed or removed while the shell runs show up without a restart, Flatpak apps included, with their icons.
- **Fixes**
  - **Wallpaper colours:** a new wallpaper recolours the desktop again (Matugen 4 stopped when an image had several source colours).
  - **Hyprland 0.56:** removed `misc.vfr` and `decoration.shadow.ignore_window`, which the config error banner reported.
  - **Short screens:** Settings no longer runs off the bottom or the top of the screen.
  - **Dashboard:** even margins on both sides.
  - **Bar:** the battery widget's hover follows its capsule.

## v4.1.3 — 2026-10-18

- **A new installer.** It asks everything first, shows one plan, confirms once, then runs to the end.
  - **The plan** shows your apps, Hyprland mode and update channel; the packages that are missing; the shell build; and every config it will write (`+` new, `~` replaced and backed up).
  - **Re-runs** start from your previous choices.
  - **sudo** is asked for once and kept alive through the build.
  - **Each step** shows a spinner and its duration, and keeps its output in the log. A failing step prints the end of that log.
- **Undo:** every replaced file is backed up next to a `restore.sh`. `./install.sh --restore [stamp]` undoes an install's config changes (the last one by default, listing older ones).
- **Uninstall:** `./install.sh --uninstall` removes the shell from `/usr`; your configs stay.
- **Dry run:** `--dry-run` prints one line per file. Without a terminal the installer runs as `--yes`.
- **Removed:** the live-wallpaper option. The native shell doesn't play video wallpapers.

## v4.1.2 — 2026-10-18

- **Bar editor in Settings** (Settings → Status bar → Widgets): three columns, Left, Middle and Right.
  - **Adding:** each column has an **Add widget ▾** dropdown listing the widgets not on the bar yet.
  - **Widget rows:** each widget's row has its own buttons:
    - **Select**, to act on several widgets at once.
    - **Group**, which joins the selected widgets of the column (or the next widget, when none is selected) into one capsule on the bar. In a group the button splits it up again.
    - **Delete**.
  - **With a selection:** the column offers **Group n** and **Delete n**.
- **Widget groups:** grouped widgets share one capsule on the bar. In `config.json` a zone lists ids, or arrays of ids for groups. The on-bar edit mode and the widget picker work with groups; moving a widget out of a group takes it out of the capsule.

## v4.1.1 — 2026-10-18

- **Edit the bar in place:** Settings → Status bar → **Edit bar**, a right-click on the bar, or `material3ui msg bar edit` turns on edit mode.
  - **Zones and widgets:** each zone is outlined, and every widget gets ◀ ✕ ▶ to move it (across zones too) or remove it.
  - **Adding:** a "+" in each zone opens a widget picker that explains each widget.
  - **Leaving:** **Done** (or `msg bar done`) ends edit mode.
- **Lock screen editor:** the lock screen at full size, with your wallpaper and live widgets, on a 3 × 4 grid. Open it from Settings → Lock screen → **Open editor**, or with `material3ui msg popup lockeditor`.
  - **Editing:** click an empty cell to add a widget there, or click a widget to move it with arrows (it swaps with any widget in the way) or remove it.
  - **Buttons:** **Default**, **Clock style**, **Try it** (locks the screen) and **Done**.
- **Lock screen grid:** widgets sit on the grid's cells (`lock.grid`), the clock included. Columns align left, centre and right, and rows size to their widgets. Settings → Lock screen also has a compact board of the same grid. Layouts from v4.1.0 carry over, and the login screen follows the grid.



## v4.1.0 — 2026-10-18

- **Weather** (Settings → Weather) comes from Open-Meteo (no API key) and refreshes every 15 minutes.
  - **Location:** your IP finds it ("Use my location"), or you search for any city. Choose °C or °F.
  - **Settings page:** a live card with feels-like, humidity, wind and a 7-day forecast.
  - **Elsewhere:** weather also shows in the bar, on the dashboard and on the lock screen.
  - **Cache:** the last reply is kept, so the weather shows before the network is up.
- **Bar widgets:** the bar is edited in three zones (left, centre, right), and the "Add" list is now a gallery that explains each widget. Right-click empty bar space to open the editor.
  - New widgets: weather, date (opens the dashboard calendar), media, CPU & memory, notifications (a bell with a count), and Quick Settings.
  - Existing widgets: launcher, workspaces, window title, island, clock, network speed, tray, clipboard, screenshot, status icons, battery and power.
- **Editable lock screen** (Settings → Lock screen):
  - **Clock:** stacked or on one line, centred or on the left, light/regular/bold weight, and size.
  - **Widgets:** date, weather, three-day forecast, media with controls, battery, and the number of waiting notifications (never their content). Turn each on or off, and reorder them.
- **The login screen matches the lock screen.** `material3ui-greeter sync` copies the lock screen's clock, widgets and weather settings and the last forecast, alongside the wallpaper and colours. The greeter draws the same view and refreshes the weather itself. The shell syncs a few seconds after any settings change (Settings → Lock screen → Match the lock screen), and there's a "Sync now" button.
- **Dashboard:** a weather card with the next five days; clicking it opens the Weather settings.
- **Spacing:**
  - The Sound and Wi-Fi pages in Settings now sit in grouped cards like every other page, and the Weather forecast card is spaced like a group.
  - The sidebar scrolls to the active page.
  - Monitors with no description no longer show a stray separator.
  - The stacked lock-screen clock is tighter.



## v4.0.0 — 2026-10-18

**The native shell replaces Quickshell.** Material3UI is now one C++ program (`material3ui`) that talks to Wayland, D-Bus, PipeWire and PAM directly, and Qt, QML and Quickshell are no longer used.

Breaking changes:
- **The shell:** the QML shell (`~/.config/quickshell/material3ui`) is removed. `install.sh` and `update.sh` build the shell from `shell/` with meson, install it to `/usr` (with sudo, for the lock screen's PAM file), back up and remove the old profile, and start `material3ui`.
- **IPC:** `material3ui msg <command>` replaces `qs -c material3ui ipc call …` and `material3uictl`, which is removed. Hyprland binds in `shell.lua` use the new commands, and `material3ui msg help` lists them all. See [docs/IPC.md](docs/IPC.md), which maps v3 calls to v4.
- **Login screen:** the greeter runs `material3ui greeter`, and its QML files are gone. The shell syncs the wallpaper and colours to it after every recolour.
- **Removed:**
  - `material3ui-sysmon`: the system monitor reads `/proc` itself.
  - v3-only extras: the pomodoro timer, alarms, online/video wallpapers, QR and image search.
- **Dependencies:** `quickshell-git` and `qt6-*` are dropped. The shell's build tools and libraries are added: meson, ninja, wayland, mesa, pango, cairo, librsvg, gdk-pixbuf2, libxkbcommon, nlohmann-json, libpulse, polkit and pam. `tesseract` is added for OCR.
- **Lock and idle:** hypridle now only routes `loginctl lock-session` to the shell and suspends after an hour. The shell itself does the locking and screen blanking.

Also in this release:
- Quick Settings has a Settings button.
- Screenshot OCR (`msg screenshot ocr`) and screen recording (`msg record`).
- The Matugen theme hooks moved to `~/.local/share/material3ui/scripts`.
- The shipped wallpapers, logo and emoji list moved into `shell/data`.
- The v3 wallpaper setting is picked up on first run.

- **Native shell: blur and customisation.**
  - **Blur** behind the bar, panels, toasts and OSD. On Hyprland it comes from layer rules that the shell writes to `effects_custom.lua`, which `shell.lua` reads. On compositors with `ext-background-effect-v1`, each surface sends a blur region shaped to its rounded cards.
  - **Look & feel page:** blur on/off and strength, bar and panel opacity, corner roundness (Sharp to Extra) and animation speed (Off to Relaxed). Changes apply live, even to the open Settings window.
  - **Status bar page:** top or bottom position. A layout editor arranges modules across left, centre and right. New modules: launcher, clock, network speed, clipboard, screenshot and power.
  - **Accent colour:** pick a Matugen seed colour instead of using the wallpaper's.
- **Native shell: M3 Expressive Settings.**
  - Segmented lists and connected button groups.
  - Shape morph on press, filled icons for the active page, and a fixed page header with a close button.
  - Scrollbars, and pages scroll instead of being cut off on short screens.
  - An app icon and a `material3ui-settings.desktop` launcher entry.
- **Native shell: Wi-Fi details.** The connected network's entry shows signal, band and channel, link rate, security, addresses and live download/upload speeds. This works in Quick Settings and in Settings → Network, which also lists gateway, DNS, IPv6, MAC and data used.
- **Native shell: dock.** Pinned and running apps on the bottom edge, set to auto-hide (the default), always shown, or off.
  - Click focuses an app's window or cycles through several; middle click opens a new window; right click pins or unpins.
  - A Dock page in Settings sets pins, order, icon size and whether running apps show.
- **Native shell: clipboard and screenshots.**
  - **Clipboard history** (`msg clipboard`): search, image thumbnails, delete and clear. The shell starts the cliphist watchers itself when nothing else runs them.
  - **Screenshot tool** (`msg screenshot`): region, window or screen, with a delay, copy to clipboard, editor (satty/swappy) and recent shots.
  - Both have options in Settings → Tools.

- **Native shell phase 7: Settings** (`msg settings [page]`):
  - **Network:** the Wi-Fi picker.
  - **Bluetooth:** power, discovery, and pair/connect/disconnect/forget.
  - **Sound:** volume, balance, channels and devices.
  - **Display:** brightness, night light temperature, monitors.
  - **Appearance:**
    - Wallpaper: thumbnail, picker and transition.
    - Colours: dark theme and Matugen scheme, recoloured live.
    - Bar: style, height and island options.
    - Clock format, fonts, and the desktop clock.
  - **Notifications:** Do Not Disturb and pop-up duration.
  - **Lock & idle:** lock and screen-off timers, lock before sleep.
  - **Keybinds:** a C++ port of the editor, sharing `keybinds.json` and `keybinds_custom.lua` with the QML one.
  - **Packages & Updates:** `checkupdates`, `yay`/`paru` and Flatpak updates, plus search and install for official, AUR (aurweb RPC) and Flatpak.
  - **About:** logo, device and compositor details, credits.
- **Native shell: verified end to end.**
  - **Polkit:** real polkitd authenticates a `wheel` user through the shell's agent: wrong password and retry, then authorized.
  - **Mouse:** pointer-driven tests use a `wlr-virtual-pointer` tool and a `msg debug` layout dump.
  - **JPEG and WebP:** wallpapers decode through gdk-pixbuf's sandboxed loaders. The dev container now relaxes seccomp and SELinux confinement so the sandbox can run.
- **Native shell: hover fix.** New `onHoverWithin` fixes hover over child buttons: the island collapsed as soon as the pointer reached its media buttons, and a toast's timeout restarted while hovering its action buttons.
- **Native C++ shell, phases 4–6** (`shell/`):
  - **Notifications:** a full notification daemon, with toasts and a notification centre.
  - **Quick Settings:** sliders, tiles, now playing, and a Wi-Fi picker with password entry.
  - **Spotlight:** apps, calculator, emoji, clipboard history, files and run, with keyboard navigation.
  - **Indicators and menus:** volume/brightness OSDs, a power menu, and DBusMenu tray menus.
  - **Session:** an ext-session-lock lock screen with PAM, a polkit agent, `material3ui greeter` for greetd, and idle handling (lock, screen off, Caffeine, lock before sleep).
  - **Wallpaper:** a per-output wallpaper layer with fade/wipe/grow transitions, a picker that runs Matugen, and a desktop clock.
  - **Panels:** the dynamic island (media, notifications, workspace switches), a dashboard with a calendar, the system monitor and a workspace overview.
- **Installer:** paru is no longer installed or used. The installer installs packages with yay and bootstraps `yay-bin` when yay is missing, building `yay` from source if the prebuilt binary doesn't run against the system's libalpm. `scripts/install_deps.sh` uses yay too. Packages & Updates still detects paru if you already have it.
- **Installer:** fix `libalpm.so.15: cannot open shared object file` when bootstrapping paru. An AUR helper is used only if it actually starts. When the prebuilt `paru-bin` doesn't run against the system's libalpm, the installer builds `paru` from source, and it runs a full `pacman -Syu` first. The package-updates check also skips a helper that fails to start.
- **License:** Material3UI Shell is now MIT-licensed (previously AGPL-3.0), with the permission of the NAnDoroid-shell authors. See `LICENSE` and `NOTICE`.
- **Native C++ shell, phases 2 and 3** (`shell/`):
  - Input:
    - Keyboard through xkbcommon, with key repeat and focus routing.
    - Fractional scaling (wp-fractional-scale + viewporter).
  - Popups:
    - Anchored under the bar, closed by Esc or a click outside, with Material enter and exit motion.
    - New controls: slider (with a centred balance mode), switch, scroll view and text field.
  - Services:
    - sd-bus on the event loop.
    - Audio through libpulse on PipeWire: volume, left/right balance, per-channel levels and device switching.
    - NetworkManager, BlueZ, UPower (with a sysfs fallback), brightness via logind, MPRIS, and a StatusNotifier tray that is both watcher and host, with theme, PNG, SVG and pixmap icons.
  - Bar:
    - New widgets: now playing, tray, network/Bluetooth/volume icons and UPower battery.
    - New popups: Sound (with balance and channels), Wi-Fi, Bluetooth and Run.
  - IPC: `status`, `popup`, `volume`, `brightness`, `media` and `tray`.
  - Testing: headless tests can start real D-Bus buses, PipeWire with null sinks, and mock system services.
- **Native C++ shell, phase 1** (`shell/`). A Wayland shell drawn directly with EGL/GLES2 and Pango, with no Qt, QML or Quickshell, following Noctalia's architecture with original code:
  - Core: a poll event loop, layer-shell surfaces with frame pacing, an SDF rounded-rect renderer, a scene graph with flex layout, and Material 3 motion.
  - Shared with the QML shell: it reads the same `config.json` and Matugen palette, and recolours live when either changes.
  - Services: a Hyprland socket service and Unix-socket IPC (`material3ui msg`).
  - UI so far: the status bar with animated workspaces, window title, clock and battery.
  - Testing: `shell/tools/dev.sh` builds in an Arch container and screenshots the shell under headless Sway, optionally against a fake Hyprland.
- **Keybind editor** in Settings → System → Keybinds. It lists the shell's Hyprland binds live, grouped and searchable. Click a bind to remap it (modifier chips plus press-a-key capture), disable it or reset it. You can also add custom command binds, and conflicts are flagged. Binds from `user.lua` are listed read-only.
  - The default binds in `shell.lua` and `keybinds.lua` now go through a new `bindings.lua` registry, which applies the edits from the shell-written `keybinds_custom.lua` before binding. Updates never overwrite that file.
- **Channels** in Settings → Audio for the default output (and for stereo microphones):
  - A left/right balance slider that snaps to centre.
  - A volume slider per channel, surround included.
  - A "Centre" reset.
  - Speaker test buttons that play the freedesktop "front left/right" voice on one side only.

## v3.0.0 — 2026-10-18

Material3UI gets its own identity, and a home for system packages.

- New original logo: a Material 3 Expressive eight-lobe "cookie" in an indigo-to-violet tonal gradient, carrying a rounded "M" built from two arches and crowned by a coral Dynamic Island pill. The full-colour and symbolic (tintable) SVGs are generated by `scripts/generate_logo.py` using only SVG Tiny features, so Qt renders them faithfully.
- The NAnDoroid artwork the shell inherited is no longer used as Material3UI's logo; it now appears only on the NAnDoroid card in Settings → About → Credits.
- **Packages & Updates** (Settings → About), with one tab each for Updates, Official, AUR and Flatpak:
  - Updates: official-repo updates via `checkupdates` (pacman-contrib), AUR via `paru`/`yay -Qua`, Flatpak via `flatpak remote-ls --updates`; Check now / Update now (in your terminal), auto-check every 30 min–6 h, AUR and Flatpak toggles, notifications.
  - Official / AUR / Flatpak: search with `pacman -Ss`, the aurweb RPC API and `flatpak search`; installed badges; one-click install in your terminal.
- **Package Updates status bar plugin** for both bar styles (base and M3), listed in the status bar editor and enabled by default for new configs; hidden when up to date unless you choose otherwise. Left click opens the page, right click checks, middle click updates.
- IPC: `packages`, `checkUpdates`, `systemUpdate`, `updateCount` on the `material3ui` target; `material3uictl packages` and `material3uictl updates check|upgrade|count`.
- `pacman-contrib` joins the dependencies; Flatpak is optional.
- Settings → About opens with a logo hero (full-colour logo with a short entrance animation, name and tagline) above the Shell and Distro cards; the Shell card, onboarding, launcher entry and notifications use the new mark.

## v2.3.0 — 2026-10-18

- Add the Material3UI **greetd login screen** (`greeter/`). It mirrors the lock screen's layout, sizes, colours, placeholder text, Material-shape password dots, shake and entrance motion, and renders your lock-screen clock style; it adds a user picker, a session picker (from `wayland-sessions`) and power buttons. It handles multi-step PAM prompts (fingerprint/OTP) and remembers the last user and session; with no greetd running it opens in a preview mode.
- Add `material3ui-greeter` (`install [--enable]`, `sync`, `preview`, `status`, `uninstall`). Install backs up `/etc/greetd/config.toml`, makes Google Sans Flex available system-wide, and grants your user write access to `/var/lib/material3ui-greeter` through an ACL so syncing needs no sudo.
- Add **Auto sync login screen** to Settings → Lockscreen, with status, "Sync now" and "Preview". A new `GreeterSync` service watches the generated colours and the shell config and syncs after a short debounce.
- `install.sh` gains an optional login-screen step (`--greeter`) and disables `hyprpolkitagent`, polkit-gnome and the Plasma agent user services, since the shell is the polkit agent and only one agent can register per session. `update.sh` refreshes an installed greeter when sudo is available without a prompt.

## v2.2.0 — 2026-10-18

- Add `material3ui-sysmon`, Material3UI's own system monitor backend replacing dgop. Pure Python 3 standard library, reading `/proc` and `/sys`: CPU (usage, per-core, frequency, temperature), memory and swap, system counts and load, processes with true per-sample CPU deltas, disk mounts and GPUs (named from `pci.ids`).
- `meta` (one-shot JSON), `top` (table) and `server` (local HTTP on `127.0.0.1:63490`, `/meta` and `/health`) commands.
- The System Monitor's Processes tab and the About page's hardware summary now use it; it ships with the shell, so no extra package is needed. The `ps` fallback remains for systems without `python3`.
- The installer and `update.sh` link it to `~/.local/bin/material3ui-sysmon`.

## v2.1.0 — 2026-10-18

- Rewrite the installer around a reviewed, seven-step flow modelled on DankMaterialShell's: preflight → choices → dependency review → packages → configuration review → deploy → summary. It logs every run, backs up everything it replaces, supports `--yes`, `--dry-run`, `--terminal`, `--file-manager`, `--browser`, `--hypr full|inject|skip`, `--channel`, `--skip-deps` and `--dir`, and hands over to the cloned script when run through `curl`.
- Default apps are now **Alacritty**, **Nemo** and **LibreWolf**. The choice is written to `hypr/material3ui/apps.lua`, exported as `$TERMINAL` / `$BROWSER`, and registered as the XDG defaults; Nemo's terminal follows the chosen terminal.
- Rewrite the Hyprland Lua config as small modules (`env`, `apps`, `monitors`, `appearance`, `colors`, `input`, `rules`, `autostart`, `keybinds`, `shell`, `user`), loaded with dotted `require`s; `user.lua` is yours and never overwritten.
- Hyprland border colours come from the wallpaper through a new Matugen `colors.lua` template; add an Alacritty theme template and config.
- Add media, mic-mute, `Print`, `SUPER+Escape` (power menu) and `SUPER+B` bindings; panel bindings use the unified `material3ui` IPC target.
- The shell launches `$TERMINAL` (falling back to Alacritty) instead of a hardcoded kitty, and pins Alacritty, Nemo and LibreWolf to the dock by default.
- `update.sh` keeps `apps.lua`, `user.lua`, `user_persistence.lua` and `colors.lua`, honours the recorded Hyprland mode, and only updates Alacritty/Matugen/starship configs that carry the Material3UI marker.
- Add `swappy` to the root dependency list so it matches the shell's copy.

## v2.0.0 — 2026-10-18

Material3UI Shell is rebuilt on [NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell) and is now licensed under AGPL-3.0.

- Replace the v1 QML shell with the full NAnDoroid feature set: Dynamic Island, status bar, dock, overview, Spotlight, Quick Settings/Actions, dashboard, notification centre, lock screen, Polkit agent, region tools (screenshot, OCR, QR, search, recording), system monitor, wallpaper browser (local, Wallhaven, live), desktop widgets, lyrics, music recognition, onboarding, translations and OTA updates.
- Add the unified `material3ui` IPC target and rewrite `material3uictl` around it; per-panel targets stay available.
- Add `wallpaper set|next|toggleDarkMode` IPC functions.
- Ship modular Hyprland Lua configs (`hypr/material3ui/` plus `extras/hypr/`), Matugen templates for GTK, KDE, terminals and apps, and the Midnight Dunes wallpaper.
- Installer migrates v1 installs: backs up and replaces the shell profile and v1 `hyprland.lua`, and removes `material3ui-system`, `material3ui-polkit` and the update timer.
- Fix a duplicate `SUPER+V` binding that passed an unsupported argument to `spotlight toggle`.
- Upstream history is kept in `docs/UPSTREAM_CHANGELOG.md`.

## v1.0.1 — 2026-10-02

- Rewrite the Arch installer with safe upgrades, backups, unattended flags, and optional AUR setup.
- Migrate the Hyprland profile from deprecated Hyprlang to the current Lua configuration API.
- Preserve Material3UI startup, wallpaper restoration, blur, workspaces, and media controls in the Lua profile.

## v1.0.0 — 2026-10-02

- Ship the original Midnight Dunes desktop wallpaper as the first-run default.
- Persist and restore the selected wallpaper at login through swww.
- Generate the Material 3 palette from the restored wallpaper.
- Promote Material3UI Shell to its first stable major release.

## v0.0.2 — 2026-09-22

- Rename the shell to Material3UI Shell, including the Quickshell profile, IPC target, commands, installer, and documentation.

## v0.0.1 — 2026-09-22

Initial Material3UI Shell release for Arch Linux and Hyprland.

- Material 3 Quickshell interface with Matugen palette generation and compositor blur.
- Hyprland profile, IPC controls, launcher, control centre, clipboard, notifications, and power menu.
- Service-backed network, Bluetooth, wallpaper, battery, screenshot, recording, and settings actions.
- Interactive Arch-only installer with Thunar and LibreWolf as preferred applications.
