# Software, updates and firmware

**Settings → Software & updates** (`material3ui msg settings packages`) keeps the system up to date and decides where its software comes from. **Settings → Firmware** (`material3ui msg settings firmware`) does the same for your devices' own firmware.

## Software & updates

| Tab | What it does |
| :--- | :--- |
| **Updates** | Pending updates from the official repos (`checkupdates`, which never touches pacman's own databases), the AUR (`yay -Qua`, or `paru -Qua`) and Flatpak, plus Material3UI's own. **Update now** installs them all in the background with one password: the shell's polkit dialog asks once. If pacman has a question a background run can't answer, **Use a terminal** runs the update there instead. |
| **Repositories** | pacman's repositories, mirrors, downloads and held-back packages, and Flatpak's remotes (see below). |
| **Official** | Search the Arch repositories and install. |
| **AUR** | Search the AUR through its web API (no helper needed to search) and install with your AUR helper. Without one, the tab offers **Install yay**, built from the AUR in a terminal, where makepkg asks for your password. |
| **Flatpak** | Search Flathub and your other remotes and install. Without Flatpak, the tab offers **Install Flatpak** in place of the search (Flathub is added with it, one password), and **Add Flathub** when Flatpak has no remote yet. |

When an update needs a restart (a new kernel, systemd, drivers) or a new login (Hyprland's libraries), a banner says so and offers it.

### Automatic updates

The Updates tab's **Automatic updates** group sets how often the shell checks in the background: every 6 hours, daily (the default), weekly, or never. A check only looks: installing is always up to you.

- **Tell me about new ones:** a notification when a check finds updates it hasn't told you about. Its **Review** button opens this page (or Firmware, when only firmware is new).
- **AUR updates**, **Flatpak updates** and **Firmware updates** choose what the checks and **Update now** take in.
- The **Updates** bar widget shows how many are waiting. Click it to open this page, or middle-click to start the update.

Hooks can follow along too: `updates_available` fires when a check finds new updates, and `updates_installed` when **Update now** finishes (see [Hooks and events](IPC.md#hooks-and-events)).

### Repositories

`/etc/pacman.conf` belongs to root, so each change is made on a copy and checked with `pacman-conf` before anything is written. Then one password puts it in place. The file before the change is kept as `/etc/pacman.conf.m3-backup`, and **Undo** swaps the two back (and again, to redo). Comments, order and anything the page doesn't manage stay exactly as they were.

- **Arch's repositories:** core and extra are always on. Switch on **multilib** for 32-bit libraries (Steam, Wine), or the **testing** repositories to get packages before release. A testing repository missing from the file is added above the one it tests, as Arch's own pacman.conf has it.
- **Your own:** add a repository with its name, server address and, optionally, its packager's key fingerprint. The key is fetched and trusted (`pacman-key --recv-keys`, `--lsign-key`) before pacman needs it. Your repositories can be switched off or removed.
- A new or newly enabled repository is used from the next **Update now**.

### Mirrors

The page lists the mirrors in use, in the order pacman tries them, each removable (one always stays). **Find the fastest** downloads archlinux.org's mirror status and takes the best-kept, fully synced HTTPS mirrors in a country. The country is guessed from the mirrors you use now; pick another, or Worldwide. Each one is timed downloading core's database from your connection, and the ten fastest are listed with their speeds. **Use these** writes a new `/etc/pacman.d/mirrorlist` (the old one is kept as `mirrorlist.m3-backup`).

### Downloads and held-back packages

- **At the same time:** pacman's `ParallelDownloads`.
- **Held back:** packages left out of updates until you let them go (pacman's `IgnorePkg`), such as a kernel or a driver you want to keep at a known version.

### Flatpak

Without Flatpak, the page offers to install it, with Flathub added in the same step. With it, every remote is listed (the system's and yours) with a switch and a remove button. **Flathub** can be added in one tap, and any other remote from its `.flatpakrepo` address. Flatpak asks for the password itself for the system's remotes.

## Firmware

Firmware updates come from [fwupd](https://fwupd.org) and the LVFS (Linux Vendor Firmware Service), where vendors publish updates for laptops' BIOS and UEFI, SSDs, docks, touchpads, webcams and more. Without fwupd, the page explains what it does and offers to install it.

- **Updates:** each device with a newer release shows its version → the new version, its release notes, its size and how urgent it is. **Update** installs one, and **Update all** installs them all. fwupd asks for the password through polkit.
- **What to know first:** if fwupd says something stands in the way (the charger unplugged, a low battery, a closed lid), the update says why instead. System firmware goes in as the computer restarts, and a banner offers the restart once it's ready.
- **Your devices:** everything fwupd can see, with its vendor and version, and whether it's up to date, can be updated here, or failed its last update.
- **Where updates come from:** the LVFS, and its testing list for updates before release, each with a switch.

Firmware checks join the automatic ones while **Firmware updates** is on in Software & updates. The list of firmware is refreshed without a password, as fwupd allows by default.

## From a terminal

| Command | Does |
| :--- | :--- |
| `material3ui msg updates [list]` | What's waiting: one line per update, firmware included |
| `material3ui msg updates check` | Checks now |
| `material3ui msg updates upgrade` | Installs them (asks for the password) |
| `material3ui msg firmware [list]` | fwupd's devices: id, name, version, and any update |
| `material3ui msg firmware check` | Refreshes the firmware list and looks again |
| `material3ui msg firmware update [id]` | Updates one device, or all of them |
| `material3ui msg repos [list]` | pacman's repositories, mirrors, parallel downloads, held-back packages, and Flatpak's remotes |
| `material3ui msg repos enable\|disable <repo>` | Switches a repository on or off (asks for the password) |
