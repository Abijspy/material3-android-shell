#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Material3UI Shell updater
#  Usage: update.sh [all|shell] [stable|canary] [--force]
#  Called by Settings → About → Update, or by hand. Uses the choices recorded
#  by install.sh in ~/.config/material3ui/install_state.json.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

MODE="${1:-all}"
CHANNEL="${2:-}"
FORCE=false
for arg in "$@"; do [[ "$arg" == --force ]] && FORCE=true; done

CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
STATE_FILE="$CONFIG_HOME/material3ui/install_state.json"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOT="$PROJECT_ROOT/dotfiles/.config"
HYPR="$CONFIG_HOME/hypr"
SHELL_DIR="$CONFIG_HOME/quickshell/material3ui"
HYPR_PRESERVE=(apps.lua user.lua user_persistence.lua colors.lua)

[[ -f "$STATE_FILE" ]] || { echo "install_state.json not found; run install.sh first." >&2; exit 1; }
state() { grep -oP "\"$1\": \"\\K[^\"]*" "$STATE_FILE" || true; }

INSTALL_DIR="$(state install_dir)"; INSTALL_DIR="${INSTALL_DIR:-$PROJECT_ROOT}"
CHANNEL="${CHANNEL:-$(state channel)}"; CHANNEL="${CHANNEL:-stable}"
HYPR_MODE="$(state hypr_mode)"
if [[ -z "$HYPR_MODE" ]]; then
    grep -q '"inject": true' "$STATE_FILE" && HYPR_MODE=inject || HYPR_MODE=full
fi

case "$MODE" in all|shell) ;; *) echo "Usage: $0 [all|shell] [stable|canary] [--force]" >&2; exit 2 ;; esac

# A dev checkout symlinks the shell profile into the repo; copying over it
# would replace the symlink with a plain directory.
if [[ -L "$SHELL_DIR" && "$FORCE" == false ]]; then
    echo "✗ $SHELL_DIR is a symlink -> $(readlink "$SHELL_DIR") (dev setup)."
    echo "  Update aborted; pull manually with: git -C \"$INSTALL_DIR\" pull"
    echo "  Or re-run with --force."
    exit 1
fi

cd "$INSTALL_DIR"
echo "Fetching updates..."
git fetch --tags origin
if [[ "$CHANNEL" == stable ]]; then
    tag="$(git describe --tags "$(git rev-list --tags --max-count=1)" 2>/dev/null || true)"
    if [[ -n "$tag" ]]; then
        echo "Release channel: checking out $tag"
        git checkout -q "$tag"
    else
        echo "No release tags yet; following main."
        git checkout -q main && git pull -q --ff-only origin main
    fi
else
    echo "Latest channel: following main"
    git checkout -q main && git pull -q --ff-only origin main
fi

ours() { [[ ! -e "$1" ]] || grep -qs "# Material3UI Configuration" "$1"; }

echo "Updating shell..."
mkdir -p "$SHELL_DIR"
cp -a "$DOT/quickshell/material3ui/." "$SHELL_DIR/"
cp "$PROJECT_ROOT/data/dependencies.json" "$SHELL_DIR/data/dependencies.json"

if [[ "$MODE" == all ]]; then
    if ours "$CONFIG_HOME/matugen/config.toml"; then
        echo "Updating matugen templates..."
        mkdir -p "$CONFIG_HOME/matugen" && cp -a "$DOT/matugen/." "$CONFIG_HOME/matugen/"
    else
        echo "Skipping matugen (your own config)."
    fi

    if [[ -e "$CONFIG_HOME/starship.toml" ]] && ours "$CONFIG_HOME/starship.toml"; then
        cp "$DOT/starship.toml" "$CONFIG_HOME/starship.toml"
    fi
    if [[ -e "$CONFIG_HOME/alacritty/alacritty.toml" ]] && ours "$CONFIG_HOME/alacritty/alacritty.toml"; then
        cp "$DOT/alacritty/alacritty.toml" "$CONFIG_HOME/alacritty/alacritty.toml"
    fi

    if [[ "$HYPR_MODE" != skip ]]; then
        echo "Updating Hyprland modules ($HYPR_MODE)..."
        mkdir -p "$HYPR/material3ui"
        rm -f "$HYPR/material3ui/material3ui.lua"
        for f in "$DOT/hypr/material3ui/"*.lua; do
            name="$(basename "$f")"
            [[ " ${HYPR_PRESERVE[*]} " == *" $name "* && -e "$HYPR/material3ui/$name" ]] && continue
            cp "$f" "$HYPR/material3ui/$name"
        done
        [[ "$HYPR_MODE" == full ]] && cp "$DOT/hypr/hyprland.lua" "$HYPR/hyprland.lua"
    fi
fi

install -Dm755 "$PROJECT_ROOT/dotfiles/.local/bin/material3uictl" "$HOME/.local/bin/material3uictl"
echo "Update successful!"
