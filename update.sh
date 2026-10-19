#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Material3UI Shell updater
#  Usage: update.sh [all|shell] [stable|canary] [--force]
#  Run by hand to pull, install the shell (the release's material3ui
#  package on the stable channel, else one built from the checkout) and
#  refresh configs. Uses the choices recorded by install.sh in
#  ~/.config/material3ui/install_state.json. Installing the shell needs root:
#  sudo, or M3_SUDO=pkexec; when Settings runs it, M3_ROOT is its update
#  helper (material3ui-root DIR), already authorised: no further password.
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

# A checkout with local changes is a dev setup: don't move it under the user
if [[ -n "$(git -C "$INSTALL_DIR" status --porcelain --untracked-files=no 2>/dev/null)" && "$FORCE" == false ]]; then
    echo "✗ $INSTALL_DIR has local changes (dev setup)."
    echo "  Update aborted; pull manually, or re-run with --force."
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

# The shell installs as the material3ui pacman package: a release's own
# package on the stable channel, else one made from a build of the checkout
PACKAGING="$INSTALL_DIR/packaging"
PKG_DIR="$(mktemp -d)"
trap 'rm -rf "$PKG_DIR"' EXIT
PKG=""
if [[ "$CHANNEL" == stable && -n "${tag:-}" ]]; then
    name="material3ui-${tag#v}-1-$(uname -m).pkg.tar.zst"
    if curl -fsSL -o "$PKG_DIR/$name" "https://github.com/Abijspy/material3-android-shell/releases/download/$tag/$name"; then
        PKG="$PKG_DIR/$name"
        echo "Downloaded $name"
    else
        echo "The $tag release has no package for $(uname -m); building it here."
    fi
fi
if [[ -z "$PKG" ]]; then
    echo "Building the shell..."
    BUILD="$INSTALL_DIR/shell/build-release"
    [[ -d "$BUILD" ]] && setup_flags=(--reconfigure) || setup_flags=()
    meson setup "${setup_flags[@]}" "$BUILD" "$INSTALL_DIR/shell" --prefix=/usr --sysconfdir=/etc --buildtype=release >/dev/null
    meson compile -C "$BUILD" | tail -1
    if [[ -x "$PACKAGING/build-pkg.sh" ]] && command -v makepkg >/dev/null; then
        PKG="$("$PACKAGING/build-pkg.sh" material3ui --build "$BUILD" "$PKG_DIR" 2>/dev/null)" || PKG=""
    fi
fi
# Root: Settings' update helper (one password for the whole update), else
# M3_SUDO (a command, words allowed), else sudo
read -ra ROOT <<<"${M3_ROOT:-}"
read -ra SUDO <<<"${M3_SUDO:-sudo}"
if [[ -n "$PKG" && ${#ROOT[@]} -gt 0 ]]; then
    "${ROOT[@]}" install-pkg "$PKG"
elif [[ -n "$PKG" && -x "$PACKAGING/install-pkg.sh" ]]; then
    "${SUDO[@]}" "$PACKAGING/install-pkg.sh" "$PKG"
else
    # No makepkg (base-devel), or a release from before the packages
    [[ -n "${BUILD:-}" ]] || { echo "No package to install." >&2; exit 1; }
    "${SUDO[@]}" meson install -C "$BUILD" --no-rebuild --quiet
fi
mkdir -p "$HOME/.local/share/material3ui"
rm -rf "$HOME/.local/share/material3ui/scripts"
cp -a "$INSTALL_DIR/dotfiles/.local/share/material3ui/scripts" "$HOME/.local/share/material3ui/"

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

rm -f "$HOME/.local/bin/material3uictl" "$HOME/.local/bin/material3ui-sysmon" # v3 helpers
# v3's Quickshell shell: kept in the backups, out of the way
if [[ -d "$CONFIG_HOME/quickshell/material3ui" ]]; then
    backup="$CONFIG_HOME/material3ui/backups/$(date +%Y%m%d-%H%M%S)/.config/quickshell"
    mkdir -p "$backup" && mv "$CONFIG_HOME/quickshell/material3ui" "$backup/"
    pkill -x quickshell 2>/dev/null || true
    echo "Moved the old Quickshell shell to $backup"
fi
if [[ -d /usr/share/material3ui-greeter ]] && "$PROJECT_ROOT/greeter/material3ui-greeter" outdated; then
    if [[ ${#ROOT[@]} -gt 0 ]]; then
        # Fetched or built as you, installed and set up by the helper
        greeter_pkg="$("$PROJECT_ROOT/greeter/material3ui-greeter" fetch)" &&
            "${ROOT[@]}" install-pkg "$greeter_pkg" && "${ROOT[@]}" greeter-setup && echo "Login screen updated."
    elif [[ "${M3_SUDO:-}" == pkexec ]]; then
        pkexec "$PROJECT_ROOT/greeter/material3ui-greeter" install && echo "Login screen updated."
    elif sudo -n true 2>/dev/null; then
        "$PROJECT_ROOT/greeter/material3ui-greeter" install && echo "Login screen updated."
    else
        echo "Login screen: run '$PROJECT_ROOT/greeter/material3ui-greeter install' to update it (needs sudo)."
    fi
fi

# Swap the running shell for the new build
if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    hyprctl reload >/dev/null || true
    pkill -x material3ui || true
    sleep 0.3
    # Detached, so it outlives this script (and Lua configs need no dispatch syntax)
    setsid -f material3ui >/dev/null 2>&1 </dev/null
    echo "Shell restarted."
fi

echo "Update successful!"
command -v notify-send >/dev/null && notify-send -a "Software & updates" "Material3UI Shell updated" || true
