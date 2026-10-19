#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Material3UI installer · Arch Linux + Hyprland 0.55+
#
#  Asks everything first, shows one plan, then runs without further prompts:
#  packages, the native shell (built from shell/ and installed to /usr), the
#  configs (every replaced file backed up, with a restore.sh beside the
#  backups), and the session (services, login screen, default apps).
#
#    ./install.sh                 interactive
#    ./install.sh --yes           defaults (or your previous choices), no questions
#    ./install.sh --dry-run       print the plan and every step, change nothing
#    ./install.sh --restore       undo the last install's config changes
#    ./install.sh --uninstall     remove the shell from /usr
#
#  Or straight from GitHub (clones to ~/.local/src/material3ui):
#    bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
# ─────────────────────────────────────────────────────────────────────────────
set -Eeuo pipefail

readonly REPO_URL="https://github.com/Abijspy/material3-android-shell.git"
readonly MIN_HYPRLAND="0.55"
TS="$(date +%Y%m%d-%H%M%S)"
readonly TS
readonly CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
readonly STATE_DIR="$CONFIG_HOME/material3ui"
readonly STATE_FILE="$STATE_DIR/install_state.json"
readonly BACKUP_ROOT="$STATE_DIR/backups"
readonly BACKUP_DIR="$BACKUP_ROOT/$TS"
readonly LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/material3ui"
readonly LOG="$LOG_DIR/install-$TS.log"
readonly HYPR="$CONFIG_HOME/hypr"
# Hyprland modules that are yours once created: never replaced
readonly HYPR_KEEP=(user.lua user_persistence.lua colors.lua)

# ── Options ──────────────────────────────────────────────────────────────────
YES=0 DRY=0 SKIP_DEPS=0 MODE=install RESTORE_STAMP=""
TERMINAL="" FILE_MANAGER="" BROWSER="" HYPR_MODE="" CHANNEL="" GREETER="" INSTALL_DIR=""
ORIG_ARGS=("$@")

usage() {
    cat <<'EOF'
Usage: install.sh [options]

  -y, --yes                 Accept defaults (or your previous choices); no questions
  -n, --dry-run             Show the plan and every step; change nothing
      --skip-deps           Don't install packages (build and configure only)
      --terminal NAME       alacritty | kitty | foot | ghostty
      --file-manager NAME   nemo | thunar | nautilus | dolphin
      --browser NAME        librewolf | firefox | chromium | zen-browser
      --hypr MODE           full: Material3UI owns hyprland.lua (yours is backed up)
                            inject: keep your hyprland.lua, add the shell to it
                            skip: leave Hyprland's config alone
      --channel NAME        latest (follow main) | release (tagged releases)
      --greeter             Also install the login screen (greetd)
      --dir PATH            Where to clone when run outside the repo
      --restore [STAMP]     Undo an install's config changes (default: the last)
      --uninstall           Remove the shell from /usr (configs stay; see --restore)
  -h, --help                This help
EOF
}

need() { [[ -n "${2:-}" && "${2:0:1}" != - ]] || { echo "install.sh: $1 needs a value" >&2; exit 2; }; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes) YES=1 ;;
        -n|--dry-run) DRY=1 ;;
        --skip-deps) SKIP_DEPS=1 ;;
        --terminal) need "$1" "${2:-}"; TERMINAL="$2"; shift ;;
        --file-manager) need "$1" "${2:-}"; FILE_MANAGER="$2"; shift ;;
        --browser) need "$1" "${2:-}"; BROWSER="$2"; shift ;;
        --hypr) need "$1" "${2:-}"; HYPR_MODE="$2"; shift ;;
        --channel) need "$1" "${2:-}"; CHANNEL="$2"; shift ;;
        --greeter) GREETER=1 ;;
        --dir) need "$1" "${2:-}"; INSTALL_DIR="$2"; shift ;;
        --restore) MODE=restore; if [[ -n "${2:-}" && "${2:0:1}" != - ]]; then RESTORE_STAMP="$2"; shift; fi ;;
        --uninstall) MODE=uninstall ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done
# No terminal to ask on (CI, a pipe without a tty): take the defaults
if ! { : </dev/tty; } 2>/dev/null; then YES=1; fi

# ── Output ───────────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    B=$'\e[1m' D=$'\e[2m' R=$'\e[0m' P=$'\e[38;2;208;188;255m' A=$'\e[38;2;145;177;240m'
    G=$'\e[38;2;166;209;137m' Y=$'\e[38;2;229;200;144m' E=$'\e[38;2;242;184;181m'
else
    B="" D="" R="" P="" A="" G="" Y="" E=""
fi
section() { printf '\n%s%s▍%s %s%s%s\n' "$P" "$B" "$R" "$B" "$1" "$R"; }
info()    { printf '  %s\n' "$*"; }
note()    { printf '  %s%s%s\n' "$D" "$*" "$R"; }
ok()      { printf '  %s✓%s %s\n' "$G" "$R" "$*"; }
warn()    { printf '  %s!%s %s\n' "$Y" "$R" "$*"; }
fail()    { printf '\n  %s✗ %s%s\n' "$E$B" "$*" "$R" >&2; [[ -s "${LOG:-}" ]] && printf '  %sLog: %s%s\n' "$D" "$LOG" "$R" >&2; exit 1; }
tilde()   { printf '%s' "${1/#$HOME/\~}"; }

ask() { # ask "Question" → the reply ("" when unattended)
    local reply=""
    [[ "$YES" == 1 ]] || read -r -p "  ${A}?${R} $1 " reply </dev/tty || true
    printf '%s' "$reply"
}
confirm() { # confirm "Question" Y|N
    if [[ "$YES" == 1 ]]; then [[ "$2" == Y ]]; return; fi
    local hint reply
    [[ "$2" == Y ]] && hint="Y/n" || hint="y/N"
    reply="$(ask "$1 [$hint]")"
    [[ "${reply:-$2}" =~ ^[Yy] ]]
}
choose() { # choose VAR "Title" "value:description"... (the first is the default)
    local var="$1" title="$2" i reply
    shift 2
    local opts=("$@") values=("${@%%:*}")
    if [[ -n "${!var}" ]]; then
        [[ " ${values[*]} " == *" ${!var} "* ]] || fail "$title: '${!var}' isn't one of: ${values[*]}"
        return 0
    fi
    if [[ "$YES" == 1 ]]; then printf -v "$var" '%s' "${values[0]}"; return 0; fi
    info "${B}$title${R}"
    for i in "${!opts[@]}"; do
        printf '    %s%d%s  %-12s %s%s%s\n' "$A" $((i + 1)) "$R" "${values[$i]}" "$D" "${opts[$i]#*:}" "$R"
    done
    while true; do
        reply="$(ask "Choice [1]")"
        reply="${reply:-1}"
        if [[ "$reply" =~ ^[0-9]+$ ]] && (( reply >= 1 && reply <= ${#opts[@]} )); then
            printf -v "$var" '%s' "${values[$((reply - 1))]}"
            return 0
        fi
        warn "Pick a number from 1 to ${#opts[@]}."
    done
}

# A quiet step: a spinner while it works, its output in the log, and the end
# of the log if it fails
SPIN_PID=""
stop_spin() {
    [[ -n "$SPIN_PID" ]] || return 0
    kill "$SPIN_PID" 2>/dev/null || true
    wait "$SPIN_PID" 2>/dev/null || true
    SPIN_PID=""
    printf '\r\e[2K'
}
task() { # task "Label" command...
    local label="$1" start=$SECONDS rc=0
    shift
    if [[ "$DRY" == 1 ]]; then note "[dry-run] $label: $*"; return 0; fi
    if [[ -t 1 ]]; then
        ( frames='⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'; i=0
          while true; do printf '\r  %s%s%s %s' "$P" "${frames:i++%10:1}" "$R" "$label"; sleep 0.1; done ) &
        SPIN_PID=$!
    fi
    printf '── %s: %s\n' "$label" "$*" >>"$LOG"
    "$@" >>"$LOG" 2>&1 || rc=$?
    stop_spin
    if [[ $rc -ne 0 ]]; then
        printf '  %s✗%s %s\n' "$E" "$R" "$label"
        tail -n 15 "$LOG" | sed "s/^/    /"
        fail "$label failed"
    fi
    ok "$label ${D}$((SECONDS - start))s${R}"
}
# A step that may need the terminal (package prompts): output shown as it runs
live() { # live "Label" command...
    local label="$1"
    shift
    if [[ "$DRY" == 1 ]]; then note "[dry-run] $label: $*"; return 0; fi
    info "${B}$label${R}"
    "$@" 2>&1 | tee -a "$LOG" | sed "s/^/    /" || fail "$label failed"
}
do_() { if [[ "$DRY" == 1 ]]; then note "[dry-run] $*"; else "$@"; fi; }
trap 'rc=$?; stop_spin; printf "\n  %sStopped at line %s (exit %s). Log: %s%s\n" "$E" "$LINENO" "$rc" "${LOG:-}" "$R" >&2; exit $rc' ERR
trap 'stop_spin; printf "%s" "$R"' EXIT

# ── The repository: this checkout, or a fresh clone ──────────────────────────
locate_repo() {
    local here tag
    here="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
    if [[ -n "$here" && -f "$here/shell/meson.build" ]]; then REPO="$here"; return; fi
    [[ "$DRY" == 0 ]] || fail "--dry-run needs a checkout: git clone $REPO_URL && cd material3-android-shell"
    command -v git >/dev/null || sudo pacman -S --needed --noconfirm git
    INSTALL_DIR="${INSTALL_DIR:-$HOME/.local/src/material3ui}"
    INSTALL_DIR="${INSTALL_DIR/#\~/$HOME}"
    if [[ -d "$INSTALL_DIR/.git" ]]; then
        echo "Updating $INSTALL_DIR"
        git -C "$INSTALL_DIR" fetch -q --tags origin
        git -C "$INSTALL_DIR" checkout -q main
        git -C "$INSTALL_DIR" pull -q --ff-only origin main
    else
        echo "Cloning into $INSTALL_DIR"
        git clone -q "$REPO_URL" "$INSTALL_DIR"
    fi
    if [[ "$CHANNEL" == release ]]; then
        tag="$(git -C "$INSTALL_DIR" describe --tags --abbrev=0 "$(git -C "$INSTALL_DIR" rev-list --tags --max-count=1)" 2>/dev/null || true)"
        if [[ -n "$tag" ]]; then git -C "$INSTALL_DIR" checkout -q "$tag"; echo "Release channel: $tag"; fi
    fi
    # Hand over to the cloned installer, so script and files match
    exec bash "$INSTALL_DIR/install.sh" "${ORIG_ARGS[@]}"
}

# Previous choices (install_state.json): a re-run starts from them
previous() { [[ -f "$STATE_FILE" ]] && grep -oP "\"$1\": \"\\K[^\"]*" "$STATE_FILE" 2>/dev/null || true; }

# ── Packages ─────────────────────────────────────────────────────────────────
# LibreWolf is in [extra] now (librewolf-bin left the AUR); Zen is still AUR-only
pkg_for() { case "$1" in zen-browser) echo zen-browser-bin ;; *) echo "$1" ;; esac; }
desktop_for() {
    case "$1" in
        alacritty) echo Alacritty.desktop ;; ghostty) echo com.mitchellh.ghostty.desktop ;;
        nautilus) echo org.gnome.Nautilus.desktop ;; dolphin) echo org.kde.dolphin.desktop ;;
        zen-browser) echo zen.desktop ;; *) echo "$1.desktop" ;;
    esac
}
package_list() {
    local c
    for c in core services utilities theming; do jq -r ".${c}[].name" "$REPO/data/dependencies.json"; done
    if [[ "$OPT_FONTS" == 1 ]]; then jq -r '.fonts[].name' "$REPO/data/dependencies.json"; fi
    if [[ "$OPT_FISH" == 1 ]]; then printf '%s\n' fish starship; fi
    if [[ "$OPT_EXTRAS" == 1 ]]; then printf '%s\n' easyeffects cloudflare-warp-bin; fi
    if [[ "$OPT_KDE" == 1 ]]; then echo python-pip; fi
    if [[ "$GREETER" == 1 ]]; then printf '%s\n' greetd acl; fi
    if [[ "$FILE_MANAGER" == nemo ]]; then echo nemo-fileroller; fi
    printf '%s\n' hypridle gnome-keyring git "$(pkg_for "$TERMINAL")" "$(pkg_for "$FILE_MANAGER")" "$(pkg_for "$BROWSER")"
}
# yay, made to work: libalpm moves on, and a stale binary won't start
ensure_yay() {
    yay --version >/dev/null 2>&1 && return 0
    local tmp
    tmp="$(mktemp -d)"
    sudo pacman -S --needed --noconfirm base-devel git
    git clone -q --depth 1 https://aur.archlinux.org/yay-bin.git "$tmp/yay-bin"
    (cd "$tmp/yay-bin" && makepkg -si --noconfirm) || true
    if ! yay --version >/dev/null 2>&1; then
        # The prebuilt binary is tied to one libalpm: build from source
        if pacman -Qq yay-bin >/dev/null 2>&1; then sudo pacman -Rdd --noconfirm yay-bin; fi
        git clone -q --depth 1 https://aur.archlinux.org/yay.git "$tmp/yay"
        (cd "$tmp/yay" && makepkg -si --noconfirm)
    fi
    rm -rf "$tmp"
    yay --version >/dev/null 2>&1
}

# ── Backups and the restore script ───────────────────────────────────────────
# Whatever is replaced is copied to $BACKUP_DIR first, and restore.sh there
# grows a line for each change: running it puts things back as they were.
RESTORE="$BACKUP_DIR/restore.sh"
CHANGED=0
restore_line() {
    [[ "$DRY" == 1 ]] && return 0
    if [[ ! -f "$RESTORE" ]]; then
        mkdir -p "$BACKUP_DIR"
        printf '#!/usr/bin/env bash\n# Undoes the config changes of the Material3UI install of %s.\nset -e\nB="$(cd "$(dirname "$0")" && pwd)"\n' "$TS" >"$RESTORE"
        chmod +x "$RESTORE"
    fi
    printf '%s\n' "$1" >>"$RESTORE"
}
# Before `dst` is written: keep the old one, and teach restore.sh about it
guard() {
    local dst="$1" rel="${1#"$HOME"/}"
    CHANGED=$((CHANGED + 1))
    if [[ "$DRY" == 1 ]]; then
        [[ -e "$dst" || -L "$dst" ]] && note "[dry-run] back up $(tilde "$dst")"
        return 0
    fi
    if [[ -e "$dst" || -L "$dst" ]]; then
        do_ mkdir -p "$(dirname "$BACKUP_DIR/$rel")"
        do_ cp -a "$dst" "$BACKUP_DIR/$rel"
        restore_line "rm -rf \"\$HOME/$rel\"; mkdir -p \"\$(dirname \"\$HOME/$rel\")\"; cp -a \"\$B/$rel\" \"\$HOME/$rel\""
    else
        restore_line "rm -rf \"\$HOME/$rel\""
    fi
}
put() { # put SRC DST: replace a file or directory, guarded
    guard "$2"
    if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$2")"; return 0; fi
    do_ mkdir -p "$(dirname "$2")"
    do_ rm -rf "$2"
    do_ cp -a "$1" "$2"
}
# Configs carrying our marker are ours to replace; any other is yours
ours() { [[ ! -e "$1" ]] || grep -qs -e "# Material3UI Configuration" -e "templates/GeneratedColors.qml" "$1"; }

# ── --restore and --uninstall ────────────────────────────────────────────────
restore_mode() {
    local dir
    if [[ -n "$RESTORE_STAMP" ]]; then dir="$BACKUP_ROOT/$RESTORE_STAMP"
    else dir="$(find "$BACKUP_ROOT" -mindepth 2 -maxdepth 2 -name restore.sh -printf '%h\n' 2>/dev/null | sort | tail -n 1)"; fi
    [[ -n "$dir" && -x "$dir/restore.sh" ]] || fail "No restorable backup in $(tilde "$BACKUP_ROOT")"
    info "Puts back the configs the install of ${B}$(basename "$dir")${R} changed."
    local others
    others="$(find "$BACKUP_ROOT" -mindepth 2 -maxdepth 2 -name restore.sh -printf '%h\n' 2>/dev/null | sort | xargs -rn1 basename | grep -vx "$(basename "$dir")" | tr '\n' ' ' || true)"
    [[ -n "$others" ]] && note "Older installs: $others(pick one with --restore STAMP)"
    confirm "Restore them?" Y || exit 0
    do_ "$dir/restore.sh"
    ok "Restored. Reload Hyprland (hyprctl reload) to apply."
}
uninstall_mode() {
    local build
    build="$(previous install_dir)/shell/build-release"
    [[ -d "$build" ]] || build="$REPO/shell/build-release"
    [[ -d "$build" ]] || fail "No build to uninstall from ($(tilde "$build")); remove /usr/bin/material3ui by hand."
    info "Removes what the shell installed under /usr and /etc: the binary, its data, the PAM file and the launcher."
    note "Your configs stay; ./install.sh --restore puts back the ones the install replaced."
    [[ "$YES" == 1 ]] || confirm "Uninstall the shell?" N || exit 0
    do_ sudo -v
    pkill -x material3ui 2>/dev/null || true
    task "Removing the shell" sudo ninja -C "$build" uninstall
}

# ═════════════════════════════════════════════════════════════════════════════
printf '\n%s%s  ●  Material3UI%s %s· a native Material 3 shell for Hyprland%s\n' "$P" "$B" "$R" "$D" "$R"
[[ "$DRY" == 1 ]] && printf '  %sDry run: nothing will change.%s\n' "$Y" "$R"
locate_repo
mkdir -p "$LOG_DIR"
: >"$LOG"
case "$MODE" in
    restore) restore_mode; exit 0 ;;
    uninstall) uninstall_mode; exit 0 ;;
esac

# ── 1. Checks ────────────────────────────────────────────────────────────────
section "Checking the system"
[[ "$EUID" -ne 0 ]] || fail "Run as your normal user, not root; sudo is asked for when needed."
command -v pacman >/dev/null || fail "pacman not found: Material3UI supports Arch-based systems only."
os="$(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}")"
if (. /etc/os-release 2>/dev/null; [[ " ${ID:-} ${ID_LIKE:-} " == *" arch "* ]]); then ok "$os"
else warn "$os isn't Arch-based; going on, since pacman is here"; fi
hv="$(pacman -Q hyprland 2>/dev/null | awk '{print $2}' || true)"
if [[ -z "$hv" ]]; then note "Hyprland isn't installed yet; it comes with the packages."
elif [[ "$(vercmp "${hv%%-*}" "$MIN_HYPRLAND")" -lt 0 ]]; then warn "Hyprland $hv is older than $MIN_HYPRLAND (needed for the Lua config); it gets upgraded"
else ok "Hyprland $hv"; fi
VERSION="$(cat "$REPO/VERSION" 2>/dev/null || echo "?")"
ok "Material3UI $VERSION from $(tilde "$REPO")"
[[ -f "$STATE_FILE" ]] && note "Installed before: your previous choices are the defaults."

# ── 2. Choices ───────────────────────────────────────────────────────────────
section "Your setup"
KEEP_PREVIOUS=1
if [[ -f "$STATE_FILE" && "$YES" == 0 && -z "$TERMINAL$FILE_MANAGER$BROWSER$HYPR_MODE" ]]; then
    info "Last time: $(previous terminal) · $(previous file_manager) · $(previous browser) · Hyprland $(previous hypr_mode)"
    confirm "Keep these?" Y || KEEP_PREVIOUS=0
fi
if [[ "$KEEP_PREVIOUS" == 1 ]]; then
    [[ -n "$TERMINAL" ]] || TERMINAL="$(previous terminal)"
    [[ -n "$FILE_MANAGER" ]] || FILE_MANAGER="$(previous file_manager)"
    [[ -n "$BROWSER" ]] || BROWSER="$(previous browser)"
    [[ -n "$HYPR_MODE" ]] || HYPR_MODE="$(previous hypr_mode)"
fi
if [[ -z "$CHANNEL" ]]; then
    case "$(previous channel)" in stable) CHANNEL=release ;; canary) CHANNEL=latest ;; esac
fi
choose TERMINAL "Terminal" "alacritty:GPU terminal, recoloured with the wallpaper" "kitty:feature-rich" "foot:minimal and fast" "ghostty:modern GPU terminal"
choose FILE_MANAGER "File manager" "nemo:Cinnamon's" "thunar:light, from Xfce" "nautilus:GNOME Files" "dolphin:KDE's"
choose BROWSER "Browser" "librewolf:privacy-minded Firefox" "firefox:Mozilla Firefox" "chromium:open-source Chromium" "zen-browser:Zen (AUR)"
choose HYPR_MODE "Hyprland config" "full:Material3UI's hyprland.lua (yours is backed up)" "inject:keep yours, add the shell to it" "skip:don't touch it"
choose CHANNEL "Updates" "latest:follow main" "release:tagged releases only"
# The v1 config can't take an injection: it is replaced instead
if [[ "$HYPR_MODE" == inject ]] && grep -qs "^-- Material3UI Shell — Hyprland" "$HYPR/hyprland.lua"; then
    warn "Your hyprland.lua is Material3UI v1's: using full mode to replace it"
    HYPR_MODE=full
fi

OPT_FONTS=0 OPT_SERVICES=0 OPT_KDE=0 OPT_FISH=0 OPT_EXTRAS=0
info "${B}Extras${R}"
confirm "Fonts (Google Sans Flex, Material Symbols, JetBrains Mono)?" Y && OPT_FONTS=1
confirm "Turn on NetworkManager and Bluetooth?" Y && OPT_SERVICES=1
# The shell talks to NetworkManager. iwd or systemd-networkd running beside
# it (Arch's install defaults) fight it for the Wi-Fi card: the connection
# flips between them and the shell shows none
NET_RIVALS=()
for svc in iwd systemd-networkd; do
    if systemctl is-enabled --quiet "$svc.service" 2>/dev/null || systemctl is-active --quiet "$svc.service" 2>/dev/null; then
        NET_RIVALS+=("$svc")
    fi
done
OPT_NET_ONLY=0
if [[ "$OPT_SERVICES" == 1 && ${#NET_RIVALS[@]} -gt 0 ]]; then
    note "${NET_RIVALS[*]} also manage the network, and fight NetworkManager for the Wi-Fi."
    confirm "Turn ${NET_RIVALS[*]} off so NetworkManager alone manages it? (Wi-Fi reconnects through NetworkManager; pick your network again if it asks)" Y && OPT_NET_ONLY=1
fi
confirm "Material You colours for KDE/Qt apps (a Python venv)?" N && OPT_KDE=1
confirm "fish with the starship prompt?" N && OPT_FISH=1
confirm "EasyEffects and Cloudflare WARP?" N && OPT_EXTRAS=1
if [[ -z "$GREETER" ]]; then
    if confirm "The Material3UI login screen (greetd; replaces your display manager)?" N; then GREETER=1; else GREETER=0; fi
fi

# ── 3. The plan ──────────────────────────────────────────────────────────────
section "The plan"
if ! command -v jq >/dev/null; then task "jq, to read the package list" sudo pacman -S --needed --noconfirm jq; fi
PKGS=() MISSING=()
if [[ "$SKIP_DEPS" == 0 ]]; then
    mapfile -t PKGS < <(package_list | sort -u)
    # pacman -T prints what isn't satisfied locally (it honours provides)
    mapfile -t MISSING < <(pacman -T "${PKGS[@]}" 2>/dev/null || true)
fi
# Configs: what gets written, and what is left as yours
CONFIGS=() KEPT=()
cfg() { CONFIGS+=("$1|$2"); }
cfg "$HOME/.local/share/material3ui/scripts" "theme hooks Matugen runs"
if ours "$CONFIG_HOME/matugen/config.toml"; then cfg "$CONFIG_HOME/matugen" "colour templates"; else KEPT+=("your matugen config"); fi
if [[ "$TERMINAL" == alacritty ]]; then
    if ours "$CONFIG_HOME/alacritty/alacritty.toml"; then cfg "$CONFIG_HOME/alacritty/alacritty.toml" "terminal"
    else KEPT+=("your alacritty.toml (import material3ui-theme.toml for the colours)"); fi
fi
if [[ "$OPT_FISH" == 1 ]] && ours "$CONFIG_HOME/starship.toml"; then cfg "$CONFIG_HOME/starship.toml" "prompt"; fi
case "$HYPR_MODE" in
    full)
        cfg "$HYPR/hyprland.lua" "Hyprland entry point"
        cfg "$HYPR/material3ui" "Hyprland modules (user.lua and your edits kept)"
        cfg "$HYPR/hypridle.conf" "idle"
        cfg "$HYPR/hyprlock.conf" "fallback lock screen" ;;
    inject)
        cfg "$HYPR/material3ui" "Hyprland modules"
        cfg "$HYPR/hyprland.lua" "+ require(\"material3ui.shell\")" ;;
esac
[[ -e "$CONFIG_HOME/quickshell/material3ui" ]] && cfg "$CONFIG_HOME/quickshell/material3ui" "v3 Quickshell shell: removed"

extras=()
[[ "$OPT_FONTS" == 1 ]] && extras+=(fonts)
[[ "$OPT_SERVICES" == 1 ]] && extras+=("network & bluetooth")
[[ "$OPT_KDE" == 1 ]] && extras+=("KDE colours")
[[ "$OPT_FISH" == 1 ]] && extras+=(fish)
[[ "$OPT_EXTRAS" == 1 ]] && extras+=("EasyEffects, WARP")
[[ "$GREETER" == 1 ]] && extras+=("login screen")
row() { printf '  %s%-9s%s %s\n' "$D" "$1" "$R" "$2"; }
row Apps "$TERMINAL · $FILE_MANAGER · $BROWSER"
row Hyprland "$HYPR_MODE"
row Updates "$CHANNEL"
joined="$(IFS=,; printf '%s' "${extras[*]:-none}")"
row Extras "${joined//,/, }"
if [[ "$SKIP_DEPS" == 1 ]]; then row Packages "skipped (--skip-deps)"
elif [[ ${#MISSING[@]} -eq 0 ]]; then row Packages "all ${#PKGS[@]} already installed"
else row Packages "${#MISSING[@]} to install: ${MISSING[*]}" | fold -s -w 100 | sed '2,$s/^/            /'; fi
row Shell "build material3ui $VERSION and install it to /usr"
label=Configs
for e in "${CONFIGS[@]}"; do
    p="${e%%|*}"
    if [[ -e "$p" ]]; then row "$label" "${Y}~${R} $(tilde "$p") ${D}${e#*|}; backed up${R}"
    else row "$label" "${G}+${R} $(tilde "$p") ${D}${e#*|}${R}"; fi
    label=""
done
for k in "${KEPT[@]}"; do row "$label" "${D}= $k${R}"; label=""; done
note "Backups and a restore.sh go to $(tilde "$BACKUP_DIR")"
echo
confirm "Go ahead?" Y || { info "Nothing changed."; exit 0; }

# ── 4. Packages ──────────────────────────────────────────────────────────────
section "Packages"
# One password prompt, kept fresh to the end
if [[ "$DRY" == 0 ]]; then
    sudo -v || fail "sudo is needed for packages and for installing the shell"
    ( while kill -0 "$$" 2>/dev/null; do sudo -n true 2>/dev/null; sleep 50; done ) &
fi
if [[ ${#MISSING[@]} -gt 0 ]]; then
    task "AUR helper (yay)" ensure_yay
    flags=(-S --needed)
    [[ "$YES" == 1 ]] && flags+=(--noconfirm)
    live "Installing ${#MISSING[@]} packages" yay "${flags[@]}" "${MISSING[@]}"
    ok "Packages installed"
else
    note "Nothing to install."
fi
if [[ "$OPT_FONTS" == 1 ]] && ! fc-list 2>/dev/null | grep -qi "Google Sans Flex"; then
    google_sans() {
        local tmp dst="$HOME/.local/share/fonts/material3ui-google-sans-flex"
        tmp="$(mktemp -d)"
        git clone -q --depth 1 https://github.com/end-4/google-sans-flex.git "$tmp/gsf"
        mkdir -p "$dst"
        cp -r "$tmp/gsf"/* "$dst/"
        rm -rf "$tmp"
        fc-cache -f
    }
    task "Google Sans Flex" google_sans
fi
if [[ "$OPT_KDE" == 1 ]]; then
    kde_venv() {
        local venv="$HOME/.local/share/material3ui/venv"
        python3 -m venv "$venv"
        "$venv/bin/pip" install -q --upgrade pip
        "$venv/bin/pip" install -q "materialyoucolor<3.0.0" kde-material-you-colors pykakasi korean_romanizer
    }
    task "KDE colours (Python venv)" kde_venv
fi

# ── 5. The shell ─────────────────────────────────────────────────────────────
section "The shell"
BUILD="$REPO/shell/build-release"
setup=()
[[ -d "$BUILD" ]] && setup=(--reconfigure)
task "Configuring the build" meson setup "${setup[@]}" "$BUILD" "$REPO/shell" --prefix=/usr --sysconfdir=/etc --buildtype=release
task "Building (a minute or two the first time)" meson compile -C "$BUILD"
task "Installing to /usr" sudo meson install -C "$BUILD" --no-rebuild --quiet

# ── 6. Configs ───────────────────────────────────────────────────────────────
section "Configs"
planned() { local e; for e in "${CONFIGS[@]}"; do [[ "${e%%|*}" == "$1" ]] && return 0; done; return 1; }
DOT="$REPO/dotfiles/.config"

put "$REPO/dotfiles/.local/share/material3ui/scripts" "$HOME/.local/share/material3ui/scripts"
ok "Theme hooks"
if planned "$CONFIG_HOME/matugen"; then
    guard "$CONFIG_HOME/matugen"
    if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$CONFIG_HOME/matugen")"; else
        mkdir -p "$CONFIG_HOME/matugen"
        cp -a "$DOT/matugen/." "$CONFIG_HOME/matugen/"
    fi
    ok "Matugen templates"
fi
if planned "$CONFIG_HOME/alacritty/alacritty.toml"; then
    put "$DOT/alacritty/alacritty.toml" "$CONFIG_HOME/alacritty/alacritty.toml"
    ok "Alacritty"
fi
if planned "$CONFIG_HOME/starship.toml"; then
    put "$DOT/starship.toml" "$CONFIG_HOME/starship.toml"
    if ! grep -qs "starship init fish" "$CONFIG_HOME/fish/config.fish"; then
        guard "$CONFIG_HOME/fish/config.fish"
        do_ mkdir -p "$CONFIG_HOME/fish"
        [[ "$DRY" == 1 ]] || printf '\nstarship init fish | source\n' >>"$CONFIG_HOME/fish/config.fish"
    fi
    ok "fish + starship"
fi

hypr_modules() {
    local src="$DOT/hypr/material3ui" dst="$HYPR/material3ui" f name
    guard "$dst"
    if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$dst")/*.lua (keeping ${HYPR_KEEP[*]})"; return 0; fi
    mkdir -p "$dst"
    rm -f "$dst/material3ui.lua" # v2.0.0's single module, since split up
    for f in "$src"/*.lua; do
        name="$(basename "$f")"
        [[ " ${HYPR_KEEP[*]} " == *" $name "* && -e "$dst/$name" ]] && continue
        cp "$f" "$dst/$name"
    done
    # Your apps, written into apps.lua
    sed -e "s/terminal = \"[^\"]*\"/terminal = \"$TERMINAL\"/" \
        -e "s/browser = \"[^\"]*\"/browser = \"$BROWSER\"/" \
        -e "s/file_manager = \"[^\"]*\"/file_manager = \"$FILE_MANAGER\"/" "$src/apps.lua" >"$dst/apps.lua"
}
case "$HYPR_MODE" in
    full)
        hypr_modules
        put "$DOT/hypr/hyprland.lua" "$HYPR/hyprland.lua"
        put "$REPO/extras/hypr/hypridle.conf" "$HYPR/hypridle.conf"
        put "$REPO/extras/hypr/hyprlock.conf" "$HYPR/hyprlock.conf"
        [[ -e "$HYPR/hyprlock/colors.conf" ]] || put "$REPO/extras/hypr/hyprlock/colors.conf" "$HYPR/hyprlock/colors.conf"
        ok "Hyprland (full)" ;;
    inject)
        hypr_modules
        guard "$HYPR/hyprland.lua"
        if [[ "$DRY" == 0 ]]; then
            touch "$HYPR/hyprland.lua"
            sed -i '/require("material3ui\/material3ui")/d' "$HYPR/hyprland.lua"
            for mod in shell colors user_persistence; do
                grep -qs "material3ui[./]$mod\"" "$HYPR/hyprland.lua" || printf 'require("material3ui.%s")\n' "$mod" >>"$HYPR/hyprland.lua"
            done
        fi
        ok "Hyprland (the shell added to your config)" ;;
    skip) note "Hyprland config left as it is." ;;
esac

# Retired: the v3 Quickshell shell and its helpers, and v1's services
if [[ -e "$CONFIG_HOME/quickshell/material3ui" ]]; then
    guard "$CONFIG_HOME/quickshell/material3ui"
    do_ rm -rf "$CONFIG_HOME/quickshell/material3ui"
    do_ pkill -x quickshell || true
    ok "v3 Quickshell shell removed (backed up)"
fi
old=()
for f in "$HOME/.local/bin/"{material3uictl,material3ui-sysmon,material3ui-system,material3ui-polkit,material3ui-update-check}; do
    [[ -e "$f" ]] && old+=("$f")
done
if [[ ${#old[@]} -gt 0 ]]; then
    do_ systemctl --user disable --now material3ui-update.timer 2>/dev/null || true
    do_ rm -f "${old[@]}" "$CONFIG_HOME/systemd/user/material3ui-update.service" "$CONFIG_HOME/systemd/user/material3ui-update.timer"
    ok "Old helpers removed (scripting is now: material3ui msg help)"
fi

# ── 7. Session ───────────────────────────────────────────────────────────────
section "Session"
SESSION_LINES=0
if [[ "$OPT_NET_ONLY" == 1 ]]; then
    # Last, after every download: the network drops while it changes hands
    for svc in "${NET_RIVALS[@]}"; do
        if do_ sudo systemctl disable --now "$svc.service" >>"$LOG" 2>&1; then ok "$svc off (NetworkManager manages the network)"; else warn "Couldn't turn $svc off"; fi
    done
    SESSION_LINES=1
fi
if [[ "$OPT_SERVICES" == 1 ]]; then
    for svc in NetworkManager bluetooth; do
        if systemctl list-unit-files "$svc.service" >/dev/null 2>&1; then
            if do_ sudo systemctl enable --now "$svc.service" >>"$LOG" 2>&1; then ok "$svc on"; else warn "Couldn't enable $svc"; fi
            SESSION_LINES=1
        fi
    done
fi
# The shell is the polkit agent, and only one can register per session
for agent in hyprpolkitagent polkit-gnome-authentication-agent-1 plasma-polkit-agent; do
    if systemctl --user is-enabled --quiet "$agent.service" 2>/dev/null; then
        do_ systemctl --user disable --now "$agent.service"
        ok "$agent off (Material3UI is the polkit agent)"; SESSION_LINES=1
    fi
done
if command -v xdg-mime >/dev/null; then
    do_ xdg-mime default "$(desktop_for "$FILE_MANAGER")" inode/directory
    do_ xdg-settings set default-web-browser "$(desktop_for "$BROWSER")" 2>/dev/null || warn "Couldn't make $BROWSER the default browser"
    if [[ "$FILE_MANAGER" == nemo ]] && command -v gsettings >/dev/null; then
        do_ gsettings set org.cinnamon.desktop.default-applications.terminal exec "$TERMINAL" 2>/dev/null || true
    fi
    ok "Default apps: $FILE_MANAGER, $BROWSER"; SESSION_LINES=1
fi
if [[ "$GREETER" == 1 ]]; then
    # The material3ui-greeter package brings greetd with it
    if do_ "$REPO/greeter/material3ui-greeter" install --enable >>"$LOG" 2>&1; then
        ok "Login screen (greetd starts it at the next boot)"; SESSION_LINES=1
    else
        warn "Login screen setup failed (see the log); retry with: greeter/material3ui-greeter install --enable"
    fi
fi
[[ "$SESSION_LINES" -gt 0 ]] || note "Services, polkit and default apps: nothing to change."
state_channel=canary
[[ "$CHANNEL" == release ]] && state_channel=stable
if [[ "$DRY" == 0 ]]; then
    mkdir -p "$STATE_DIR"
    cat >"$STATE_FILE" <<EOF
{
  "version": "$VERSION",
  "installed": "$TS",
  "inject": $([[ "$HYPR_MODE" == inject ]] && echo true || echo false),
  "hypr_mode": "$HYPR_MODE",
  "install_dir": "$REPO",
  "channel": "$state_channel",
  "terminal": "$TERMINAL",
  "file_manager": "$FILE_MANAGER",
  "browser": "$BROWSER"
}
EOF
fi

# ── 8. Done ──────────────────────────────────────────────────────────────────
section "Done"
if [[ "$DRY" == 1 ]]; then ok "Dry run finished: nothing was changed"; else ok "Material3UI $VERSION is installed"; fi
if [[ "$CHANGED" -gt 0 && "$DRY" == 0 ]]; then note "$CHANGED config paths changed; ./install.sh --restore undoes them"; fi
info "Keys    ${A}Super${R} Spotlight · ${A}Super+I${R} Settings · ${A}Super+N${R} Quick Settings · ${A}Super+Return${R} $TERMINAL"
info "Yours   $(tilde "$HYPR/material3ui/user.lua") for Hyprland tweaks · ${A}material3ui msg help${R} for scripting"
if pacman -Qq quickshell-git >/dev/null 2>&1; then note "quickshell-git isn't used any more: yay -Rns quickshell-git"; fi
if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    if confirm "Reload Hyprland and start the shell now?" Y; then
        do_ pkill -x material3ui || true
        do_ hyprctl reload >/dev/null
        do_ hyprctl dispatch exec material3ui >/dev/null
        ok "Running"
    fi
else
    info "Log into Hyprland to start it."
fi
note "Log: $(tilde "$LOG")"
