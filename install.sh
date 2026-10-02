#!/usr/bin/env bash
# Material3UI Shell installer for current Arch Linux and Hyprland Lua configs.
set -Eeuo pipefail

readonly PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
readonly CONFIG_ROOT="${XDG_CONFIG_HOME:-$HOME/.config}"
readonly DATA_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}"
readonly BIN_ROOT="$HOME/.local/bin"
ASSUME_YES=false
ENABLE_SERVICES=true
INSTALL_AUR=true

usage() {
  cat <<'USAGE'
Usage: ./install.sh [--yes] [--no-enable-services] [--skip-aur]

  --yes                 Accept optional installation prompts.
  --no-enable-services  Do not enable NetworkManager, Bluetooth, or the update timer.
  --skip-aur            Do not offer the optional HyprMod AUR package.
USAGE
}

log()  { printf '\n==> %s\n' "$*"; }
warn() { printf '\nWarning: %s\n' "$*" >&2; }
die()  { printf '\nError: %s\n' "$*" >&2; exit 1; }

ask() {
  local prompt=$1 answer
  "$ASSUME_YES" && return 0
  read -r -p "$prompt [Y/n] " answer
  [[ -z "$answer" || "$answer" =~ ^[Yy]$ ]]
}

backup_path() {
  local source=$1 backup_root=$2 target
  [[ -e "$source" ]] || return 0
  target="$backup_root/${source#"$HOME/"}"
  mkdir -p "$(dirname -- "$target")"
  cp -a -- "$source" "$target"
}

install_official() {
  sudo pacman -S --needed --noconfirm "$@"
}

ensure_yay() {
  command -v yay >/dev/null 2>&1 && return 0
  log 'Installing Yay for the optional HyprMod package'
  install_official base-devel git
  local build_dir
  build_dir=$(mktemp -d)
  trap 'rm -rf -- "$build_dir"' RETURN
  git clone --depth=1 https://aur.archlinux.org/yay.git "$build_dir/yay"
  (cd "$build_dir/yay" && makepkg -si --needed --noconfirm)
  trap - RETURN
  rm -rf -- "$build_dir"
}

deploy_dotfiles() {
  local backup_root="$CONFIG_ROOT/material3ui/backup-$(date +%Y%m%d-%H%M%S)"
  local wallpaper_file="$CONFIG_ROOT/material3ui/wallpaper"

  mkdir -p "$CONFIG_ROOT" "$DATA_ROOT" "$BIN_ROOT" "$CONFIG_ROOT/material3ui"
  backup_path "$CONFIG_ROOT/hypr/hyprland.lua" "$backup_root"
  backup_path "$CONFIG_ROOT/hypr/hyprland.conf" "$backup_root"
  backup_path "$CONFIG_ROOT/hypr/hyprlock.conf" "$backup_root"
  backup_path "$CONFIG_ROOT/quickshell/material3ui" "$backup_root"
  [[ -d "$backup_root" ]] && log "Existing configuration backed up to $backup_root"

  cp -a -- "$PROJECT_ROOT/dotfiles/.config/." "$CONFIG_ROOT/"
  cp -a -- "$PROJECT_ROOT/dotfiles/.local/share/." "$DATA_ROOT/"
  install -Dm755 "$PROJECT_ROOT/dotfiles/.local/bin/material3uictl" "$BIN_ROOT/material3uictl"
  install -Dm755 "$PROJECT_ROOT/dotfiles/.local/bin/material3ui-system" "$BIN_ROOT/material3ui-system"
  install -Dm755 "$PROJECT_ROOT/dotfiles/.local/bin/material3ui-polkit" "$BIN_ROOT/material3ui-polkit"
  install -Dm755 "$PROJECT_ROOT/dotfiles/.local/bin/material3ui-update-check" "$BIN_ROOT/material3ui-update-check"
  install -Dm644 "$PROJECT_ROOT/VERSION" "$CONFIG_ROOT/material3ui/version"

  if [[ ! -s "$wallpaper_file" ]]; then
    printf '%s\n' "$DATA_ROOT/material3ui/wallpapers/midnight-dunes.png" > "$wallpaper_file"
  fi

  if "$ENABLE_SERVICES"; then
    if systemctl --user daemon-reload && systemctl --user enable --now material3ui-update.timer; then
      :
    else
      warn "Could not enable the user update timer. Run systemctl --user enable --now material3ui-update.timer after logging in."
    fi
  fi
}

while (($#)); do
  case $1 in
    --yes) ASSUME_YES=true ;;
    --no-enable-services) ENABLE_SERVICES=false ;;
    --skip-aur) INSTALL_AUR=false ;;
    --help|-h) usage; exit 0 ;;
    *) die "Unknown option: $1" ;;
  esac
  shift
done

[[ -f /etc/arch-release ]] || die 'Material3UI Shell supports Arch Linux only.'
[[ -f "$PROJECT_ROOT/VERSION" ]] || die 'Run this script from a complete Material3UI Shell checkout.'
[[ -f "$PROJECT_ROOT/dotfiles/.config/hypr/hyprland.lua" ]] || die 'Missing Hyprland Lua configuration.'

printf '%s\n' '╭──────────────────────────────────────╮'
printf '%s\n' '│ Material3UI Shell · Arch installer    │'
printf '%s\n' '╰──────────────────────────────────────╯'
ask 'Install or update Material3UI Shell?' || exit 0

sudo -v
log 'Updating the system and installing Material3UI runtime dependencies'
sudo pacman -Syu --needed --noconfirm \
  hyprland hyprlock xdg-desktop-portal-hyprland xdg-desktop-portal-gtk \
  quickshell matugen networkmanager bluez bluez-utils wireplumber pipewire pipewire-pulse \
  jq git curl brightnessctl playerctl wl-clipboard cliphist grim slurp swappy wf-recorder \
  polkit hyprpolkitagent foot libnotify upower swww mako power-profiles-daemon pulseaudio-utils

if ask 'Install preferred applications (Thunar, LibreWolf, file utilities)?'; then
  install_official thunar thunar-archive-plugin gvfs tumbler file-roller librewolf
fi
if ask 'Install desktop control applications (pavucontrol, blueman, nwg-look)?'; then
  install_official pavucontrol blueman nwg-look qt5ct qt6ct
fi
if ask 'Install recommended fonts and emoji support?'; then
  install_official noto-fonts noto-fonts-emoji ttf-jetbrains-mono-nerd
fi
if "$INSTALL_AUR" && ask 'Install HyprMod from the AUR?'; then
  ensure_yay
  yay -S --needed --noconfirm hyprmod
fi
if "$ENABLE_SERVICES" && ask 'Enable NetworkManager and Bluetooth now?'; then
  sudo systemctl enable --now NetworkManager.service bluetooth.service
fi

deploy_dotfiles
log 'Installation complete'
printf '%s\n' 'Log out, select Hyprland, and sign in. The shell starts from hyprland.lua.'
