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

[[ -f "$STATE_FILE" ]] || { echo "install_state.json not found; run install.sh first." >&2; exit 1; }
state() { grep -oP "\"$1\": \"\\K[^\"]*" "$STATE_FILE" || true; }

INSTALL_DIR="$(state install_dir)"; INSTALL_DIR="${INSTALL_DIR:-$PROJECT_ROOT}"
CHANNEL="${CHANNEL:-$(state channel)}"; CHANNEL="${CHANNEL:-stable}"
# Installs from before 7.0 may have set up Hyprland or Umbriel only: niri's
# config isn't theirs to write, and the shell no longer runs on those
NIRI_MODE="$(state niri_mode)"; NIRI_MODE="${NIRI_MODE:-skip}"
NIRI="$CONFIG_HOME/niri"
OLD_COMPOSITORS=""
case ",$(state compositor)," in *,hyprland,*|*,umbriel,*) OLD_COMPOSITORS=1 ;; esac
[[ -n "$(state compositor)" ]] || OLD_COMPOSITORS=1 # before 6.0: Hyprland
# niri uninstalled since: its config isn't written for it any more
if [[ "$NIRI_MODE" != skip ]] && ! command -v niri >/dev/null; then NIRI_MODE=skip; NIRI_GONE=1; fi

case "$MODE" in all|shell) ;; *) echo "Usage: $0 [all|shell] [stable|canary] [--force]" >&2; exit 2 ;; esac

# A checkout with local changes is a dev setup: don't move it under the user
if [[ -n "$(git -C "$INSTALL_DIR" status --porcelain --untracked-files=no 2>/dev/null)" && "$FORCE" == false ]]; then
    echo "✗ $INSTALL_DIR has local changes (dev setup)."
    echo "  Update aborted; pull manually, or re-run with --force."
    exit 1
fi

cd "$INSTALL_DIR"
echo "Fetching updates..."
git fetch --tags --force origin
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
APP_PKGS=()
# The apps are packages of their own (7.1): the ones installed are updated
# with the shell; the first time, all of them (they were in the shell
# before). One you've removed stays removed
APPS_MARKER="$HOME/.local/state/material3ui/apps-split"
WANTED_APPS=()
# The marker lists the apps offered so far (empty: 7.1.0's five): a new one
# is installed once, one you've removed since stays removed
OFFERED=""
if [[ -f "$APPS_MARKER" ]]; then OFFERED=" $(tr '\n' ' ' <"$APPS_MARKER") "; [[ -n "${OFFERED// /}" ]] || OFFERED=" files viewer player tasks terminal "; fi
for app in files viewer player tasks terminal notes pdf text music disks camera backups recorder calculator fonts imagewriter store; do
    if [[ "$OFFERED" != *" $app "* ]] || pacman -Q "material3ui-$app" >/dev/null 2>&1; then WANTED_APPS+=("$app"); fi
done
if [[ "$CHANNEL" == stable && -n "${tag:-}" ]]; then
    # The package's version: the tag's, a pre-release's hyphens dropped (build-pkg.sh)
    ver="${tag#v}"
    name="material3ui-${ver//-/}-1-$(uname -m).pkg.tar.zst"
    if curl -fsSL -o "$PKG_DIR/$name" "https://github.com/Abijspy/material3-android-shell/releases/download/$tag/$name"; then
        PKG="$PKG_DIR/$name"
        echo "Downloaded $name"
        for app in "${WANTED_APPS[@]}"; do
            app_name="material3ui-$app-${ver//-/}-1-$(uname -m).pkg.tar.zst"
            if curl -fsSL -o "$PKG_DIR/$app_name" "https://github.com/Abijspy/material3-android-shell/releases/download/$tag/$app_name"; then
                APP_PKGS+=("$PKG_DIR/$app_name")
            else
                # A release from before the split (its shell has the apps)
                break
            fi
        done
    else
        echo "The $tag release has no package for $(uname -m); building it here."
    fi
fi
if [[ -z "$PKG" ]]; then
    # What the build needs and isn't here yet: a release can add one
    # (as tomlplusplus did in 6.4), and a missing one would stop meson
    deps="$INSTALL_DIR/data/dependencies.json"
    if command -v jq >/dev/null && [[ -f "$deps" ]]; then
        lacking=()
        # The repositories' only (an AUR one, as matugen-bin, isn't pacman's to install)
        while read -r p; do [[ -n "$p" ]] && pacman -Si "$p" >/dev/null 2>&1 && lacking+=("$p"); done < <(pacman -T $(jq -r '.core[].name' "$deps") 2>/dev/null || true)
        if [[ ${#lacking[@]} -gt 0 ]]; then
            echo "Installing what the build needs: ${lacking[*]}"
            read -ra root_ <<<"${M3_ROOT:-}"
            read -ra sudo_ <<<"${M3_SUDO:-sudo}"
            if [[ ${#root_[@]} -gt 0 ]]; then "${root_[@]}" pacman -S --needed --noconfirm "${lacking[@]}" || echo "Couldn't install ${lacking[*]}: the build may stop"
            else "${sudo_[@]}" pacman -S --needed --noconfirm "${lacking[@]}" || echo "Couldn't install ${lacking[*]}: the build may stop"; fi
        fi
    fi
    echo "Building the shell..."
    BUILD="$INSTALL_DIR/shell/build-release"
    [[ -d "$BUILD" ]] && setup_flags=(--reconfigure) || setup_flags=()
    meson setup "${setup_flags[@]}" "$BUILD" "$INSTALL_DIR/shell" --prefix=/usr --sysconfdir=/etc --buildtype=release >/dev/null
    meson compile -C "$BUILD" | tail -1
    if [[ -x "$PACKAGING/build-pkg.sh" ]] && command -v makepkg >/dev/null; then
        PKG="$("$PACKAGING/build-pkg.sh" material3ui --build "$BUILD" "$PKG_DIR" 2>/dev/null)" || PKG=""
        if [[ -n "$PKG" && -d "$PACKAGING/material3ui-files" ]]; then
            for app in "${WANTED_APPS[@]}"; do
                echo "Packaging material3ui-$app..."
                app_pkg="$("$PACKAGING/build-pkg.sh" "material3ui-$app" --build "$BUILD" "$PKG_DIR" 2>/dev/null)" && APP_PKGS+=("$app_pkg")
            done
        fi
    fi
fi
# Root: Settings' update helper (one password for the whole update), else
# M3_SUDO (a command, words allowed), else sudo
read -ra ROOT <<<"${M3_ROOT:-}"
read -ra SUDO <<<"${M3_SUDO:-sudo}"
if [[ -n "$PKG" && ${#ROOT[@]} -gt 0 ]]; then
    if ! "${ROOT[@]}" install-pkg "$PKG" "${APP_PKGS[@]}"; then
        # Helpers before 6.2.1 take over the files a source build left only
        # when no Material3UI package is installed: this checkout's
        # install-pkg.sh takes them whatever (asking for the password again)
        echo "Installing with $PACKAGING/install-pkg.sh instead..."
        pkexec "$PACKAGING/install-pkg.sh" "$PKG" "${APP_PKGS[@]}"
    fi
elif [[ -n "$PKG" && -x "$PACKAGING/install-pkg.sh" ]]; then
    "${SUDO[@]}" "$PACKAGING/install-pkg.sh" "$PKG" "${APP_PKGS[@]}"
else
    # No makepkg (base-devel), or a release from before the packages
    [[ -n "${BUILD:-}" ]] || { echo "No package to install." >&2; exit 1; }
    "${SUDO[@]}" meson install -C "$BUILD" --no-rebuild --quiet
fi
# The apps' packages: a helper from before 7.1 installs only the first
# package it's given, so any app still missing goes in on its own
if [[ ${#APP_PKGS[@]} -gt 0 ]]; then
    missing=()
    for p in "${APP_PKGS[@]}"; do
        name="$(bsdtar -xOf "$p" .PKGINFO 2>/dev/null | sed -n 's/^pkgname = //p')"
        ver="$(bsdtar -xOf "$p" .PKGINFO 2>/dev/null | sed -n 's/^pkgver = //p')"
        [[ "$(pacman -Q "$name" 2>/dev/null | awk '{print $2}')" == "$ver" ]] || missing+=("$p")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "Installing the apps' packages..."
        if [[ ${#ROOT[@]} -gt 0 ]]; then pkexec "$PACKAGING/install-pkg.sh" "${missing[@]}"
        else "${SUDO[@]}" "$PACKAGING/install-pkg.sh" "${missing[@]}"; fi
    fi
    mkdir -p "$(dirname "$APPS_MARKER")" && printf '%s\n' files viewer player tasks terminal notes pdf text music disks camera backups recorder calculator fonts imagewriter store >"$APPS_MARKER"
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

    [[ -z "${NIRI_GONE:-}" ]] || echo "niri isn't installed: its config is left as it is."
    # niri's material3ui.kdl, with your apps: the ones it names now (Settings
    # → Default apps may have changed them since), else those picked at
    # install (config.kdl and material3ui-user.kdl are yours: left alone)
    if [[ "$NIRI_MODE" == include ]]; then
        echo "Updating niri's material3ui.kdl..."
        term="$(state terminal)" files="$(state file_manager)" web="$(state browser)"
        mkdir -p "$NIRI"
        fm="\"${files:-nemo}\""
        [[ "$files" != files ]] || fm='"material3ui" "msg" "files"'
        sed -e "s/\"alacritty\"/\"${term:-alacritty}\"/g" -e "s/\"nemo\"/$fm/g" -e "s/\"librewolf\"/\"${web:-librewolf}\"/g" \
            "$DOT/niri/material3ui.kdl" >"$NIRI/material3ui.kdl.tmp"
        if [[ -f "$NIRI/material3ui.kdl" ]]; then
            python3 -I - "$NIRI/material3ui.kdl" "$NIRI/material3ui.kdl.tmp" <<'PY' || true
import re, sys
old, new = (open(f).read() for f in sys.argv[1:3])
# The apps' variables and the keys that start them, as the current file has them
for key in ("TERMINAL", "BROWSER", r"Mod\+Return", r"Mod\+T", r"Mod\+E", r"Mod\+B"):
    pat = re.compile(r"^[ \t]*" + key + r"[ \t].*$", re.M)
    mine, theirs = pat.search(old), pat.search(new)
    if mine and theirs: new = new[:theirs.start()] + mine.group(0) + new[theirs.end():]
open(sys.argv[2], "w").write(new)
PY
        fi
        mv "$NIRI/material3ui.kdl.tmp" "$NIRI/material3ui.kdl"
    fi
    if [[ -n "$OLD_COMPOSITORS" && "$NIRI_MODE" != include ]]; then
        echo "! Material3UI runs on niri only from 7.0 (Hyprland and Umbriel support is gone)."
        echo "  Run $INSTALL_DIR/install.sh to set niri up, then log into niri."
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
if [[ -n "${NIRI_SOCKET:-}" ]]; then
    # niri reads its config again by itself; detached, so the shell outlives this script
    pkill -x material3ui || true
    sleep 0.3
    setsid -f material3ui >/dev/null 2>&1 </dev/null
    echo "Shell restarted."
fi

echo "Update successful!"
command -v notify-send >/dev/null && notify-send -a "Software & updates" "Material3UI Shell updated" || true
