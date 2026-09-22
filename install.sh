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

install_dotfiles() {
  mkdir -p "$HOME/.config" "$HOME/.local/bin"
  cp -R "$root/dotfiles/.config/." "$HOME/.config/"
  cp "$root/dotfiles/.local/bin/auractl" "$HOME/.local/bin/auractl"
  cp "$root/dotfiles/.local/bin/aura-system" "$HOME/.local/bin/aura-system"
  cp "$root/dotfiles/.local/bin/material3uictl" "$HOME/.local/bin/material3uictl"
  cp "$root/dotfiles/.local/bin/material3ui-system" "$HOME/.local/bin/material3ui-system"
  chmod +x "$HOME/.local/bin/auractl" "$HOME/.local/bin/aura-system" "$HOME/.local/bin/material3uictl" "$HOME/.local/bin/material3ui-system"
}

printf '%s\n' '╭──────────────────────────────────────╮'
printf '%s\n' '│   Material3UI Shell · Arch installer  │'
printf '%s\n' '╰──────────────────────────────────────╯'
printf '\n%s\n' 'This installs Hyprland, Material3UI dependencies, and optional preferred apps.'
if ! ask 'Continue?'; then exit 0; fi

sudo -v
sudo pacman -Sy

core=(
  hyprland hypridle hyprlock xdg-desktop-portal-hyprland
  quickshell matugen
  networkmanager bluez bluez-utils wireplumber pipewire pipewire-pulse
  brightnessctl playerctl wl-clipboard cliphist grim slurp swappy wf-recorder
  polkit-gnome foot libnotify upower swww mako power-profiles-daemon pulseaudio-utils
)
printf '\n%s\n' 'Installing Material3UI core…'
install_official "${core[@]}"

if ask 'Install preferred applications (Thunar, LibreWolf, file utilities)?'; then
  install_official thunar thunar-archive-plugin gvfs tumbler file-roller librewolf
fi
if ask 'Install everyday desktop applications (pavucontrol, blueman, nwg-look)?'; then
  install_official pavucontrol blueman nwg-look qt5ct qt6ct
fi
if ask 'Install fonts and emoji support (recommended for Material icons)?'; then
  install_official noto-fonts noto-fonts-emoji ttf-jetbrains-mono-nerd
fi
if ask 'Enable NetworkManager and Bluetooth now?'; then
  sudo systemctl enable --now NetworkManager.service bluetooth.service
fi
if ask 'Install Material3UI dotfiles into your home directory now?'; then
  install_dotfiles
fi

printf '\n%s\n' 'Installation complete.'
printf '%s\n' 'Log out, select Hyprland, and sign in. Material3UI starts from hyprland.conf.'
