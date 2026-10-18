# Packages & Updates

Settings → About → **Packages & Updates** (or the status bar plugin, or `material3uictl packages`) has one tab per job:

| Tab | What it does |
| :--- | :--- |
| **Updates** | Pending updates from the official repos (`checkupdates`, which never touches pacman's own databases), the AUR (`paru -Qua` / `yay -Qua`) and Flatpak, with **Check now** and **Update now** (runs `paru -Syu` / `yay -Syu` / `sudo pacman -Syu`, then `flatpak update`, in your terminal). Auto-check interval, AUR/Flatpak inclusion, notifications and the bar plugin are configured here. |
| **Official** | Search the Arch repositories (`pacman -Ss`) and install with `sudo pacman -S`. |
| **AUR** | Search the AUR through its web API (no helper needed to search) and install with your AUR helper. |
| **Flatpak** | Search Flathub and your other remotes and install with `flatpak install`. |

Installs and upgrades open in your terminal so password and confirmation prompts stay in your hands. The **Package Updates** status bar module (on by default for new configs, hidden when you are up to date) shows the count: left click opens the page, right click checks, middle click updates. From a terminal: `material3uictl updates check|upgrade|count`.
