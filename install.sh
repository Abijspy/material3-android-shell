#!/usr/bin/env bash
# Material3UI Shell interactive installer — Arch Linux only.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ ! -f /etc/arch-release ]]; then
  printf '%s\n' 'Material3UI Shell is intentionally Arch Linux only. Aborting.' >&2
  exit 1
fi

ask() {
  local prompt="$1" answer
  read -r -p "$prompt [Y/n] " answer
  [[ -z "$answer" || "$answer" =~ ^[Yy]$ ]]
}

install_official() { sudo pacman -S --needed "$@"; }

ensure_yay() {
  command -v yay >/dev/null 2>&1 && return 0
  printf '\n%s\n' 'Installing Yay from the Arch User Repository…'
  install_official base-devel git
  build_dir=$(mktemp -d)
  trap 'rm -rf "$build_dir"' RETURN
  git clone https://aur.archlinux.org/yay.git "$build_dir/yay"
  (
    cd "$build_dir/yay"
    makepkg -si --needed --noconfirm
  )
  trap - RETURN
  rm -rf "$build_dir"
}

install_dotfiles() {
  mkdir -p "$HOME/.config" "$HOME/.local/bin" "$HOME/.config/material3ui"
  # Preserve user customisations before replacing only Material3UI-managed files.
  if [[ -e "$HOME/.config/hypr/hyprland.conf" || -e "$HOME/.config/quickshell/material3ui" ]]; then
    backup="$HOME/.config/material3ui/backup-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$backup"
    [[ -e "$HOME/.config/hypr/hyprland.conf" ]] && cp -a "$HOME/.config/hypr/hyprland.conf" "$backup/hyprland.conf"
    [[ -e "$HOME/.config/quickshell/material3ui" ]] && cp -a "$HOME/.config/quickshell/material3ui" "$backup/quickshell"
    printf 'Existing configuration backed up to %s\n' "$backup"
  fi
  cp -R "$root/dotfiles/.config/." "$HOME/.config/"
  cp "$root/dotfiles/.local/bin/material3uictl" "$HOME/.local/bin/material3uictl"
  cp "$root/dotfiles/.local/bin/material3ui-system" "$HOME/.local/bin/material3ui-system"
  cp "$root/dotfiles/.local/bin/material3ui-polkit" "$HOME/.local/bin/material3ui-polkit"
  cp "$root/dotfiles/.local/bin/material3ui-update-check" "$HOME/.local/bin/material3ui-update-check"
  cp "$root/VERSION" "$HOME/.config/material3ui/version"
  chmod +x "$HOME/.local/bin/material3uictl" "$HOME/.local/bin/material3ui-system" "$HOME/.local/bin/material3ui-polkit" "$HOME/.local/bin/material3ui-update-check"
  systemctl --user daemon-reload
  systemctl --user enable --now material3ui-update.timer
}

printf '%s\n' '╭──────────────────────────────────────╮'
printf '%s\n' '│   Material3UI Shell · Arch installer  │'
printf '%s\n' '╰──────────────────────────────────────╯'
printf '\n%s\n' 'This installs Hyprland, Material3UI dependencies, and optional preferred apps.'
if ! ask 'Continue?'; then exit 0; fi

sudo -v
sudo pacman -Sy

core=(
  hyprland hyprlock xdg-desktop-portal-hyprland xdg-desktop-portal-gtk
  quickshell matugen
  networkmanager bluez bluez-utils wireplumber pipewire pipewire-pulse
  jq git curl
  brightnessctl playerctl wl-clipboard cliphist grim slurp swappy wf-recorder
  polkit hyprpolkitagent foot libnotify upower swww mako power-profiles-daemon pulseaudio-utils
)
printf '\n%s\n' 'Installing Material3UI core…'
install_official "${core[@]}"

# Material3UI uses Yay for optional AUR integrations such as HyprMod.
ensure_yay

if ask 'Install preferred applications (Thunar, LibreWolf, file utilities)?'; then
  install_official thunar thunar-archive-plugin gvfs tumbler file-roller librewolf
fi
if ask 'Install everyday desktop applications (pavucontrol, blueman, nwg-look)?'; then
  install_official pavucontrol blueman nwg-look qt5ct qt6ct
fi
if ask 'Install fonts and emoji support (recommended for Material icons)?'; then
  install_official noto-fonts noto-fonts-emoji ttf-jetbrains-mono-nerd
fi
if ask 'Install HyprMod for graphical Hyprland keybind and settings configuration?'; then
  yay -S --needed hyprmod
fi
if ask 'Enable NetworkManager and Bluetooth now?'; then
  sudo systemctl enable --now NetworkManager.service bluetooth.service
fi
if ask 'Install the Material3UI Hyprland, Quickshell, Matugen, and systemd-user configuration now?'; then
  install_dotfiles
fi

printf '\n%s\n' 'Installation complete.'
printf '%s\n' 'Log out, select Hyprland, and sign in. Material3UI starts from hyprland.conf.'
