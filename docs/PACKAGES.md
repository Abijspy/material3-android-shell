# Packages & Updates

**Settings → Packages & Updates** (`material3ui msg settings packages`) has one tab per job:

| Tab | What it does |
| :--- | :--- |
| **Updates** | Pending updates from three places: the official repos (`checkupdates`, which never touches pacman's own databases), the AUR (`yay -Qua`, or `paru -Qua` if that is your helper) and Flatpak. **Check now** refreshes the list. **Update now** runs the upgrade in your terminal: `yay -Syu` (or `paru -Syu`, or `sudo pacman -Syu` without a helper), then `flatpak update`. |
| **Official** | Search the Arch repositories and install with `sudo pacman -S`. |
| **AUR** | Search the AUR through its web API (no helper needed to search) and install with your AUR helper. |
| **Flatpak** | Search Flathub and your other remotes and install with `flatpak install`. |

Installs and upgrades open in your terminal (`$TERMINAL`, else the one you chose in the installer), so password and confirmation prompts stay in your hands.
