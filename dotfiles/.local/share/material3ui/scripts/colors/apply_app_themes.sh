#!/usr/bin/env bash

# Installs the app themes Matugen generated into the apps that are present.
# Called as a post-hook in matugen config.toml: apply_app_themes.sh <app>
# Your own config files are only ever given one include line, once.

XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"
GEN="$XDG_STATE_HOME/quickshell/user/generated/apps"

# Adds a line to a file unless it is already there
ensure_line() {
    local file="$1" line="$2"
    grep -qxF "$line" "$file" 2>/dev/null || printf '%s\n' "$line" >>"$file"
}

# Puts an @import first in a chrome stylesheet (imports must lead the file)
ensure_import() {
    local file="$1" target="$2"
    local line="@import \"$target\";"
    if [[ ! -f "$file" ]]; then
        printf '%s\n' "$line" >"$file"
    elif ! grep -qF "$line" "$file"; then
        printf '%s\n%s\n' "$line" "$(cat "$file")" >"$file"
    fi
}

firefox() {
    local root profile sheet
    # Firefox, LibreWolf (old and XDG locations) and the common forks
    for root in "$HOME/.mozilla/firefox" "$XDG_CONFIG_HOME/mozilla/firefox" "$HOME/.librewolf" \
        "$XDG_CONFIG_HOME/librewolf/librewolf" "$HOME/.floorp" "$HOME/.zen" "$HOME/.waterfox"; do
        [[ -d "$root" ]] || continue
        for profile in "$root"/*/; do
            # Only real profiles: they have prefs.js once the browser ran
            [[ -f "$profile/prefs.js" ]] || continue
            mkdir -p "$profile/chrome"
            # Both stylesheets run this hook; each copies what exists so far
            for sheet in userChrome userContent; do
                [[ -f "$GEN/firefox-$sheet.css" ]] || continue
                cp "$GEN/firefox-$sheet.css" "$profile/chrome/material3ui-$sheet.css"
                ensure_import "$profile/chrome/$sheet.css" "material3ui-$sheet.css"
            done
            ensure_line "$profile/user.js" 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);'
        done
    done
}

vscode() {
    local base ext id="material3ui.material3ui-theme" version="1.0.0" ui
    # The base theme (scrollbars, icons) follows the shell's light/dark mode
    ui=$(python3 - "$XDG_CONFIG_HOME/material3ui/config.json" <<'PY'
import json, sys
try:
    dark = json.load(open(sys.argv[1])).get("background", {}).get("darkmode", True)
except (OSError, ValueError):
    dark = True
print("vs-dark" if dark else "vs")
PY
)
    # VS Code, Code - OSS / VSCodium, Insiders and the common forks
    for base in "$HOME/.vscode" "$HOME/.vscode-oss" "$HOME/.vscode-insiders" "$HOME/.cursor" \
        "$HOME/.windsurf" "$HOME/.antigravity"; do
        [[ -d "$base/extensions" ]] || continue
        ext="$base/extensions/$id-$version"
        mkdir -p "$ext/themes"
        cp "$GEN/vscode-theme.json" "$ext/themes/material3ui.json"
        cat >"$ext/package.json" <<JSON
{
    "name": "material3ui-theme",
    "displayName": "Material3UI",
    "description": "Material You colours from the current wallpaper, kept in sync by Material3UI",
    "publisher": "material3ui",
    "version": "1.0.0",
    "engines": { "vscode": "^1.60.0" },
    "categories": ["Themes"],
    "contributes": {
        "themes": [{ "label": "Material3UI", "uiTheme": "$ui", "path": "./themes/material3ui.json" }]
    }
}
JSON
        # Newer releases only load extensions listed in extensions.json
        python3 - "$base/extensions" "$id" "$version" <<'PY'
import json, os, sys
root, ident, version = sys.argv[1:]
index = os.path.join(root, "extensions.json")
try:
    entries = json.load(open(index))
except (OSError, ValueError):
    entries = []
if any(e.get("identifier", {}).get("id") == ident for e in entries):
    sys.exit()
rel = f"{ident}-{version}"
path = os.path.join(root, rel)
entries.append({
    "identifier": {"id": ident},
    "version": version,
    "location": {"$mid": 1, "fsPath": path, "external": "file://" + path, "path": path, "scheme": "file"},
    "relativeLocation": rel,
    "metadata": {"source": "vsix", "pinned": True},
})
json.dump(entries, open(index, "w"))
PY
    done
}

foot() {
    local dir="$XDG_CONFIG_HOME/foot"
    # Installed or configured; `type -P` so our own functions do not count
    [[ -d "$dir" ]] || type -P foot >/dev/null || return 0
    mkdir -p "$dir"
    cp "$GEN/foot.ini" "$dir/material3ui-colors.ini"
    # Includes must sit before any section header: put it first
    if [[ ! -f "$dir/foot.ini" ]]; then
        printf 'include=%s\n' "$dir/material3ui-colors.ini" >"$dir/foot.ini"
    elif ! grep -qF "material3ui-colors.ini" "$dir/foot.ini"; then
        printf 'include=%s\n%s\n' "$dir/material3ui-colors.ini" "$(cat "$dir/foot.ini")" >"$dir/foot.ini"
    fi
}

ghostty() {
    local dir="$XDG_CONFIG_HOME/ghostty"
    # Installed or configured; `type -P` so our own functions do not count
    [[ -d "$dir" ]] || type -P ghostty >/dev/null || return 0
    mkdir -p "$dir/themes"
    cp "$GEN/ghostty" "$dir/themes/material3ui"
    # A theme the user picked stays theirs
    grep -qE '^\s*theme\s*=' "$dir/config" 2>/dev/null || ensure_line "$dir/config" "theme = material3ui"
    # Running instances reload their config on SIGUSR2
    pkill -USR2 -x ghostty 2>/dev/null
    return 0
}

wezterm() {
    local dir="$XDG_CONFIG_HOME/wezterm"
    # Installed or configured; `type -P` so our own functions do not count
    [[ -d "$dir" ]] || type -P wezterm >/dev/null || return 0
    mkdir -p "$dir/colors"
    cp "$GEN/wezterm.toml" "$dir/colors/material3ui.toml"
}

zathura() {
    local dir="$XDG_CONFIG_HOME/zathura"
    # Installed or configured; `type -P` so our own functions do not count
    [[ -d "$dir" ]] || type -P zathura >/dev/null || return 0
    mkdir -p "$dir"
    cp "$GEN/zathurarc" "$dir/material3ui"
    ensure_line "$dir/zathurarc" "include material3ui"
}

case "$1" in
firefox | vscode | foot | ghostty | wezterm | zathura) "$1" ;;
*)
    echo "usage: $0 firefox|vscode|foot|ghostty|wezterm|zathura" >&2
    exit 2
    ;;
esac
