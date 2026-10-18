#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Material3UI Shell installer (Arch Linux + Hyprland 0.55+)
#
#  Flow: preflight → choices → dependency review → packages → config review
#        → deploy (with backups) → summary.
#  Run ./install.sh --help for unattended flags.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

readonly REPO_URL="https://github.com/Abijspy/material3-android-shell.git"
readonly TS="$(date +%Y%m%d-%H%M%S)"
readonly CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
readonly STATE_DIR="$CONFIG_HOME/material3ui"
readonly BACKUP_DIR="$STATE_DIR/backups/$TS"
readonly LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/material3ui"
readonly LOG_FILE="$LOG_DIR/install-$TS.log"
readonly MIN_HYPRLAND="0.55"

# Hyprland modules the user (or the shell) owns: created once, never replaced.
readonly HYPR_PRESERVE=(user.lua user_persistence.lua colors.lua)

# ── Options ──────────────────────────────────────────────────────────────────
ASSUME_YES=0
DRY_RUN=0
SKIP_DEPS=0
TERMINAL=""
FILE_MANAGER=""
BROWSER=""
HYPR_MODE=""
CHANNEL=""
INSTALL_DIR=""
ORIG_ARGS=("$@")

usage() {
    cat <<'EOF'
Usage: install.sh [options]

  -y, --yes                 Accept defaults, no prompts (unattended)
  -n, --dry-run             Show every step without changing anything
      --skip-deps           Do not install packages
      --terminal NAME       alacritty (default) | kitty | foot | ghostty
      --file-manager NAME   nemo (default) | thunar | nautilus | dolphin
      --browser NAME        librewolf (default) | firefox | chromium | zen-browser
      --hypr MODE           full (default): Material3UI owns hyprland.lua
                            inject: append the shell module to your hyprland.lua
                            skip: leave Hyprland config alone
      --channel NAME        latest (default) | release
      --dir PATH            Where to clone when run outside the repo
                            (default ~/.local/src/material3ui)
  -h, --help                Show this help
EOF
}

need_arg() { [[ -n "${2:-}" ]] || { echo "install.sh: $1 needs a value" >&2; exit 2; }; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes) ASSUME_YES=1 ;;
        -n|--dry-run) DRY_RUN=1 ;;
        --skip-deps) SKIP_DEPS=1 ;;
        --terminal) need_arg "$1" "${2:-}"; TERMINAL="$2"; shift ;;
        --file-manager) need_arg "$1" "${2:-}"; FILE_MANAGER="$2"; shift ;;
        --browser) need_arg "$1" "${2:-}"; BROWSER="$2"; shift ;;
        --hypr) need_arg "$1" "${2:-}"; HYPR_MODE="$2"; shift ;;
        --channel) need_arg "$1" "${2:-}"; CHANNEL="$2"; shift ;;
        --dir) need_arg "$1" "${2:-}"; INSTALL_DIR="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option $1" >&2; usage; exit 2 ;;
    esac
    shift
done

# ── UI ───────────────────────────────────────────────────────────────────────
C_MAIN='\033[38;2;202;169;224m'
C_ACCENT='\033[38;2;145;177;240m'
C_DIM='\033[38;2;129;122;150m'
C_GREEN='\033[38;2;166;209;137m'
C_YELLOW='\033[38;2;229;200;144m'
C_RED='\033[38;2;231;130;132m'
C_BOLD='\033[1m'
C_RST='\033[0m'
trap 'echo -ne "$C_RST"' EXIT

STEP=0
readonly STEPS=7
step()    { STEP=$((STEP + 1)); echo; echo -e "${C_MAIN}${C_BOLD} ╭─ [$STEP/$STEPS] $1${C_RST}"; }
say()     { echo -e "${C_MAIN}${C_BOLD} │${C_RST}  $1"; }
dim()     { echo -e "${C_MAIN}${C_BOLD} │${C_RST}  ${C_DIM}$1${C_RST}"; }
ok()      { echo -e "${C_MAIN}${C_BOLD} │${C_RST}  ${C_GREEN}✓${C_RST} $1"; }
warn()    { echo -e "${C_MAIN}${C_BOLD} │${C_RST}  ${C_YELLOW}!${C_RST} $1"; }
done_()   { echo -e "${C_MAIN}${C_BOLD} ╰─${C_RST} ${C_GREEN}$1${C_RST}"; }
die()     { echo -e "\n ${C_RED}${C_BOLD}✗ $1${C_RST}" >&2; [[ -f "$LOG_FILE" ]] && echo "   Log: $LOG_FILE" >&2; exit 1; }

banner() {
    echo -e "${C_MAIN}${C_BOLD}"
    echo '  ███╗   ███╗██████╗ ██╗   ██╗██╗'
    echo '  ████╗ ████║╚════██╗██║   ██║██║'
    echo '  ██╔████╔██║ █████╔╝██║   ██║██║'
    echo '  ██║╚██╔╝██║ ╚═══██╗██║   ██║██║'
    echo '  ██║ ╚═╝ ██║██████╔╝╚██████╔╝██║'
    echo '  ╚═╝     ╚═╝╚═════╝  ╚═════╝ ╚═╝'
    echo -e "${C_RST}  ${C_BOLD}Material3UI Shell installer${C_RST} ${C_DIM}· Hyprland · Quickshell · Matugen${C_RST}"
    [[ "$DRY_RUN" == 1 ]] && echo -e "  ${C_YELLOW}Dry run: nothing will be changed.${C_RST}"
    return 0
}

# Read from the terminal even when piped through curl | bash.
prompt() {
    local reply
    read -r -p "$(echo -e "${C_MAIN}${C_BOLD} │${C_RST}  ${C_YELLOW}?${C_RST} $1 ")" reply < /dev/tty || reply=""
    echo "$reply"
}

# confirm "Question" Y|N → exit status
confirm() {
    local def="$2" hint reply
    [[ "$def" == Y ]] && hint="Y/n" || hint="y/N"
    if [[ "$ASSUME_YES" == 1 ]]; then [[ "$def" == Y ]]; return; fi
    reply="$(prompt "$1 [$hint]")"
    reply="${reply:-$def}"
    [[ "$reply" =~ ^[Yy] ]]
}

# choose VAR "Title" option... (first option is the default; "name:description")
choose() {
    local var="$1" title="$2"; shift 2
    local opts=("$@") i reply
    if [[ -n "${!var}" ]]; then
        for i in "${opts[@]}"; do [[ "${i%%:*}" == "${!var}" ]] && return 0; done
        die "Unsupported value '${!var}' for $title (choose one of: $(printf '%s ' "${opts[@]%%:*}"))"
    fi
    if [[ "$ASSUME_YES" == 1 ]]; then printf -v "$var" '%s' "${opts[0]%%:*}"; return 0; fi
    say "${C_BOLD}$title${C_RST}"
    for i in "${!opts[@]}"; do
        echo -e "${C_MAIN}${C_BOLD} │${C_RST}    ${C_ACCENT}$((i + 1))${C_RST} ${opts[$i]%%:*} ${C_DIM}${opts[$i]#*:}${C_RST}"
    done
    while true; do
        reply="$(prompt "Choice [1]")"
        reply="${reply:-1}"
        if [[ "$reply" =~ ^[0-9]+$ ]] && (( reply >= 1 && reply <= ${#opts[@]} )); then
            printf -v "$var" '%s' "${opts[$((reply - 1))]%%:*}"
            return 0
        fi
        warn "Enter a number between 1 and ${#opts[@]}."
    done
}

run() {
    if [[ "$DRY_RUN" == 1 ]]; then dim "[dry-run] $*"; else "$@"; fi
}

# ── Filesystem helpers ───────────────────────────────────────────────────────
backup() {
    local target="$1" rel
    [[ -e "$target" || -L "$target" ]] || return 0
    rel="${target#"$HOME"/}"
    run mkdir -p "$(dirname "$BACKUP_DIR/$rel")"
    run cp -a "$target" "$BACKUP_DIR/$rel"
    BACKED_UP+=("$rel")
}

# A config is "ours" when it carries the Material3UI marker (or is a v1 file).
is_ours() {
    [[ ! -e "$1" ]] && return 0
    grep -qs -e "# Material3UI Configuration" -e "templates/GeneratedColors.qml" "$1"
}

write_file() {
    local path="$1" content="$2"
    if [[ "$DRY_RUN" == 1 ]]; then dim "[dry-run] write $path"; return; fi
    mkdir -p "$(dirname "$path")"
    printf '%s\n' "$content" > "$path"
}

# ── Package metadata ─────────────────────────────────────────────────────────
pkg_for() {
    case "$1" in
        librewolf) echo librewolf-bin ;;
        zen-browser) echo zen-browser-bin ;;
        *) echo "$1" ;;
    esac
}
desktop_for() {
    case "$1" in
        alacritty) echo Alacritty.desktop ;;
        foot) echo foot.desktop ;;
        ghostty) echo com.mitchellh.ghostty.desktop ;;
        kitty) echo kitty.desktop ;;
        nemo) echo nemo.desktop ;;
        thunar) echo thunar.desktop ;;
        nautilus) echo org.gnome.Nautilus.desktop ;;
        dolphin) echo org.kde.dolphin.desktop ;;
        zen-browser) echo zen.desktop ;;
        *) echo "$1.desktop" ;;
    esac
}

# ═════════════════════════════════════════════════════════════════════════════
banner
BACKED_UP=()
MISSING=()

# Locate the repository: use this checkout, or clone when run via curl.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || echo "")"
if [[ -n "$SCRIPT_DIR" && -f "$SCRIPT_DIR/dotfiles/.config/quickshell/material3ui/shell.qml" ]]; then
    REPO="$SCRIPT_DIR"
else
    [[ "$DRY_RUN" == 0 ]] || die "--dry-run needs a local checkout: git clone $REPO_URL && cd material3-android-shell"
    command -v git >/dev/null 2>&1 || sudo pacman -S --needed --noconfirm git
    INSTALL_DIR="${INSTALL_DIR:-$HOME/.local/src/material3ui}"
    INSTALL_DIR="${INSTALL_DIR/#\~/$HOME}"
    if [[ -d "$INSTALL_DIR/.git" ]]; then
        echo "Updating $INSTALL_DIR"
        run git -C "$INSTALL_DIR" pull --ff-only origin main
    else
        echo "Cloning into $INSTALL_DIR"
        run git clone --depth 1 "$REPO_URL" "$INSTALL_DIR"
    fi
    REPO="$INSTALL_DIR"
    [[ "$DRY_RUN" == 1 || -f "$REPO/install.sh" ]] || die "Clone failed: $REPO"
    # Hand over to the freshly cloned installer so files and script match.
    [[ "$DRY_RUN" == 1 ]] || exec bash "$REPO/install.sh" "${ORIG_ARGS[@]}"
fi
mkdir -p "$LOG_DIR"
exec > >(tee -a "$LOG_FILE") 2>&1

# ── 1. Preflight ─────────────────────────────────────────────────────────────
step "Preflight"
[[ "$EUID" -ne 0 ]] || die "Run the installer as your normal user, not root (sudo is used when needed)."
command -v pacman >/dev/null 2>&1 || die "pacman not found: Material3UI supports Arch-based distributions only."
os_name="$(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}")"
if (. /etc/os-release 2>/dev/null; [[ " ${ID:-} ${ID_LIKE:-} " == *" arch "* ]]); then
    ok "Arch-based system ($os_name)"
else
    warn "$os_name is not detected as Arch-based; continuing because pacman is available."
fi
ok "Source: ${REPO/#$HOME/\~}"

if hypr_ver="$(pacman -Q hyprland 2>/dev/null | awk '{print $2}')" && [[ -n "$hypr_ver" ]]; then
    if [[ "$(vercmp "${hypr_ver%%-*}" "$MIN_HYPRLAND")" -lt 0 ]]; then
        warn "Hyprland $hypr_ver is older than $MIN_HYPRLAND; the Lua config needs $MIN_HYPRLAND+ (it will be upgraded)."
    else
        ok "Hyprland $hypr_ver"
    fi
else
    dim "Hyprland not installed yet; it will be installed with the dependencies."
fi

DOT="$REPO/dotfiles/.config"
dim "Log: $LOG_FILE"
done_ "System looks good."

# ── 2. Choices ───────────────────────────────────────────────────────────────
step "Choose your setup"
choose TERMINAL "Terminal" \
    "alacritty:GPU terminal, themed live from the wallpaper" \
    "kitty:feature-rich terminal" \
    "foot:minimal Wayland terminal" \
    "ghostty:modern GPU terminal"
choose FILE_MANAGER "File manager" \
    "nemo:Cinnamon's file manager" \
    "thunar:lightweight Xfce file manager" \
    "nautilus:GNOME Files" \
    "dolphin:KDE file manager"
choose BROWSER "Web browser" \
    "librewolf:privacy-focused Firefox fork (AUR)" \
    "firefox:Mozilla Firefox" \
    "chromium:open-source Chromium" \
    "zen-browser:Zen Browser (AUR)"
choose HYPR_MODE "Hyprland configuration" \
    "full:Material3UI manages hyprland.lua (existing one is backed up)" \
    "inject:keep your hyprland.lua, add the shell module to it" \
    "skip:do not touch Hyprland config"
choose CHANNEL "Update channel" \
    "latest:rolling updates from main" \
    "release:tagged releases only"

OPT_FONTS=0; OPT_SERVICES=0; OPT_KDE=0; OPT_LIVEWP=0; OPT_FISH=0; OPT_EXTRAS=0
say "${C_BOLD}Optional components${C_RST}"
confirm "Install fonts (Google Sans Flex, Material Symbols, JetBrains Mono)?" Y && OPT_FONTS=1
confirm "Enable NetworkManager and Bluetooth services?" Y && OPT_SERVICES=1
confirm "KDE/Qt Material You theming (Python venv)?" N && OPT_KDE=1
confirm "Live wallpapers (mpvpaper, Wallpaper Engine)?" N && OPT_LIVEWP=1
confirm "fish + starship prompt?" N && OPT_FISH=1
confirm "EasyEffects and Cloudflare WARP integration?" N && OPT_EXTRAS=1

echo
say "Terminal ${C_ACCENT}$TERMINAL${C_RST} · Files ${C_ACCENT}$FILE_MANAGER${C_RST} · Browser ${C_ACCENT}$BROWSER${C_RST} · Hyprland ${C_ACCENT}$HYPR_MODE${C_RST} · Channel ${C_ACCENT}$CHANNEL${C_RST}"
opts=()
[[ "$OPT_FONTS" == 1 ]] && opts+=(fonts)
[[ "$OPT_SERVICES" == 1 ]] && opts+=(services)
[[ "$OPT_KDE" == 1 ]] && opts+=(kde-theming)
[[ "$OPT_LIVEWP" == 1 ]] && opts+=(live-wallpapers)
[[ "$OPT_FISH" == 1 ]] && opts+=(fish)
[[ "$OPT_EXTRAS" == 1 ]] && opts+=(easyeffects warp)
dim "Optional: ${opts[*]:-none}"
done_ "Choices recorded."

# ── 3. Dependency review ─────────────────────────────────────────────────────
step "Dependencies"
PKGS=()
if [[ "$SKIP_DEPS" == 1 ]]; then
    warn "Skipping packages (--skip-deps)."
else
    command -v jq >/dev/null 2>&1 || { say "Installing jq to read the dependency list..."; run sudo pacman -S --needed --noconfirm jq; }
    categories=(core services utilities theming)
    [[ "$OPT_FONTS" == 1 ]] && categories+=(fonts)
    [[ "$OPT_LIVEWP" == 1 ]] && categories+=(livewallpaper)
    for c in "${categories[@]}"; do
        mapfile -t -O "${#PKGS[@]}" PKGS < <(jq -r ".${c}[].name" "$REPO/data/dependencies.json")
    done
    [[ "$OPT_FISH" == 1 ]] && PKGS+=(fish starship)
    [[ "$OPT_EXTRAS" == 1 ]] && PKGS+=(easyeffects cloudflare-warp-bin)
    [[ "$OPT_KDE" == 1 ]] && PKGS+=(python-pip)
    PKGS+=(hypridle gnome-keyring "$(pkg_for "$TERMINAL")" "$(pkg_for "$FILE_MANAGER")" "$(pkg_for "$BROWSER")")
    [[ "$FILE_MANAGER" == nemo ]] && PKGS+=(nemo-fileroller)

    mapfile -t PKGS < <(printf '%s\n' "${PKGS[@]}" | sort -u)
    # pacman -T prints the targets not satisfied locally (honours provides).
    mapfile -t MISSING < <(pacman -T "${PKGS[@]}" || true)
    ok "$(( ${#PKGS[@]} - ${#MISSING[@]} )) of ${#PKGS[@]} packages already installed"
    if [[ ${#MISSING[@]} -gt 0 ]]; then
        say "To install (${#MISSING[@]}):"
        printf '%s\n' "${MISSING[@]}" | column -c 76 | sed "s/^/$(printf "${C_MAIN}${C_BOLD} │${C_RST}    ")/"
        confirm "Install these packages?" Y || { warn "Packages skipped; the shell may not start until they are installed."; MISSING=(); }
    fi
fi
done_ "Dependency review complete."

# ── 4. Packages ──────────────────────────────────────────────────────────────
step "Packages"
if [[ ${#MISSING[@]} -gt 0 ]]; then
    AUR=""
    for h in paru yay; do command -v "$h" >/dev/null 2>&1 && { AUR="$h"; break; }; done
    if [[ -z "$AUR" ]]; then
        say "No AUR helper found; bootstrapping paru."
        run sudo pacman -S --needed --noconfirm base-devel git
        tmp="$(mktemp -d)"
        run git clone --depth 1 https://aur.archlinux.org/paru-bin.git "$tmp/paru-bin"
        if [[ "$DRY_RUN" == 0 ]]; then (cd "$tmp/paru-bin" && makepkg -si --noconfirm); fi
        rm -rf "$tmp"
        AUR=paru
    fi
    ok "AUR helper: $AUR"
    flags=(-S --needed)
    [[ "$ASSUME_YES" == 1 ]] && flags+=(--noconfirm)
    run "$AUR" "${flags[@]}" "${MISSING[@]}" || die "Package installation failed. Resolve the error above and re-run the installer."
    ok "Packages installed"
else
    dim "Nothing to install."
fi

if [[ "$OPT_FONTS" == 1 ]] && ! fc-list 2>/dev/null | grep -qi "Google Sans Flex"; then
    say "Installing Google Sans Flex..."
    tmp="$(mktemp -d)"
    run git clone --depth 1 https://github.com/end-4/google-sans-flex.git "$tmp/gsf"
    run mkdir -p "$HOME/.local/share/fonts/material3ui-google-sans-flex"
    [[ "$DRY_RUN" == 1 ]] || cp -r "$tmp/gsf"/* "$HOME/.local/share/fonts/material3ui-google-sans-flex/"
    rm -rf "$tmp"
    run fc-cache -f
    ok "Google Sans Flex installed"
fi

if [[ "$OPT_KDE" == 1 ]]; then
    VENV="$HOME/.local/share/material3ui/venv"
    say "Creating KDE theming venv in $VENV..."
    run python3 -m venv "$VENV"
    run "$VENV/bin/pip" install -q --upgrade pip
    run "$VENV/bin/pip" install -q "materialyoucolor<3.0.0" kde-material-you-colors pykakasi korean_romanizer
    ok "KDE theming venv ready"
fi

if [[ "$OPT_SERVICES" == 1 ]]; then
    for svc in NetworkManager.service bluetooth.service; do
        if systemctl list-unit-files "$svc" >/dev/null 2>&1; then
            run sudo systemctl enable --now "$svc" && ok "Enabled $svc"
        fi
    done
fi
done_ "Packages ready."

# ── 5. Config review ─────────────────────────────────────────────────────────
step "Configuration review"
HYPR="$CONFIG_HOME/hypr"
if [[ "$HYPR_MODE" == inject ]] && grep -qs "^-- Material3UI Shell — Hyprland" "$HYPR/hyprland.lua"; then
    warn "Your hyprland.lua is the Material3UI v1 config; switching to full mode to replace it."
    HYPR_MODE=full
fi

PLAN=()
plan() { PLAN+=("$1|$2"); }
plan "$CONFIG_HOME/quickshell/material3ui" "shell (replaced)"
is_ours "$CONFIG_HOME/matugen/config.toml" && plan "$CONFIG_HOME/matugen" "theme templates" || warn "Keeping your own matugen config (no Material3UI marker)."
is_ours "$CONFIG_HOME/starship.toml" && [[ "$OPT_FISH" == 1 ]] && plan "$CONFIG_HOME/starship.toml" "prompt"
if [[ "$TERMINAL" == alacritty ]]; then
    is_ours "$CONFIG_HOME/alacritty/alacritty.toml" && plan "$CONFIG_HOME/alacritty/alacritty.toml" "terminal" \
        || warn "Keeping your alacritty.toml; add ~/.config/alacritty/material3ui-theme.toml to its [general] import list."
fi
case "$HYPR_MODE" in
    full)
        plan "$HYPR/hyprland.lua" "Hyprland entry point"
        plan "$HYPR/material3ui" "Hyprland modules (user.lua, user_persistence.lua and colors.lua are kept)"
        plan "$HYPR/hypridle.conf" "idle / lock timers"
        plan "$HYPR/hyprlock.conf" "fallback lock screen" ;;
    inject)
        plan "$HYPR/material3ui" "Hyprland modules"
        plan "$HYPR/hyprland.lua" "append require(\"material3ui.shell\")" ;;
esac
plan "$HOME/.local/bin/material3uictl" "CLI"

for entry in "${PLAN[@]}"; do
    path="${entry%%|*}"
    if [[ -e "$path" ]]; then
        say "${C_YELLOW}~${C_RST} ${path/#$HOME/\~} ${C_DIM}— ${entry#*|}, existing copy backed up${C_RST}"
    else
        say "${C_GREEN}+${C_RST} ${path/#$HOME/\~} ${C_DIM}— ${entry#*|}${C_RST}"
    fi
done
dim "Backups go to ${BACKUP_DIR/#$HOME/\~}"
confirm "Deploy these configs?" Y || die "Aborted before changing any configuration."
done_ "Plan confirmed."

# ── 6. Deploy ────────────────────────────────────────────────────────────────
step "Deploying"
planned() { local e; for e in "${PLAN[@]}"; do [[ "${e%%|*}" == "$1" ]] && return 0; done; return 1; }

# Shell: replace rather than merge, so stale files from older versions
# (v1 kept QML at the profile root) cannot shadow current types.
backup "$CONFIG_HOME/quickshell/material3ui"
run rm -rf "$CONFIG_HOME/quickshell/material3ui"
run mkdir -p "$CONFIG_HOME/quickshell"
run cp -a "$DOT/quickshell/material3ui" "$CONFIG_HOME/quickshell/"
run cp "$REPO/data/dependencies.json" "$CONFIG_HOME/quickshell/material3ui/data/dependencies.json"
ok "Shell profile"

if planned "$CONFIG_HOME/matugen"; then
    backup "$CONFIG_HOME/matugen"
    run mkdir -p "$CONFIG_HOME/matugen"
    run cp -a "$DOT/matugen/." "$CONFIG_HOME/matugen/"
    ok "Matugen templates"
fi

if planned "$CONFIG_HOME/starship.toml"; then
    backup "$CONFIG_HOME/starship.toml"
    run cp "$DOT/starship.toml" "$CONFIG_HOME/starship.toml"
    run mkdir -p "$CONFIG_HOME/fish"
    run touch "$CONFIG_HOME/fish/config.fish"
    if [[ "$DRY_RUN" == 0 ]] && ! grep -qs "starship init fish" "$CONFIG_HOME/fish/config.fish"; then
        printf '\nstarship init fish | source\n' >> "$CONFIG_HOME/fish/config.fish"
    fi
    ok "fish + starship"
fi

if planned "$CONFIG_HOME/alacritty/alacritty.toml"; then
    backup "$CONFIG_HOME/alacritty/alacritty.toml"
    run mkdir -p "$CONFIG_HOME/alacritty"
    run cp "$DOT/alacritty/alacritty.toml" "$CONFIG_HOME/alacritty/alacritty.toml"
    ok "Alacritty"
fi

deploy_hypr_modules() {
    local src="$DOT/hypr/material3ui" dst="$HYPR/material3ui" f name
    run mkdir -p "$dst"
    run rm -f "$dst/material3ui.lua" # v2.0.0 single-file module, now split up
    for f in "$src"/*.lua; do
        name="$(basename "$f")"
        if [[ " ${HYPR_PRESERVE[*]} " == *" $name "* && -e "$dst/$name" ]]; then
            continue
        fi
        run cp "$f" "$dst/$name"
    done
    write_file "$dst/apps.lua" "$(sed \
        -e "s/terminal = \"[^\"]*\"/terminal = \"$TERMINAL\"/" \
        -e "s/browser = \"[^\"]*\"/browser = \"$BROWSER\"/" \
        -e "s/file_manager = \"[^\"]*\"/file_manager = \"$FILE_MANAGER\"/" \
        "$src/apps.lua")"
}

case "$HYPR_MODE" in
    full)
        backup "$HYPR/hyprland.lua"
        backup "$HYPR/material3ui"
        backup "$HYPR/hypridle.conf"
        backup "$HYPR/hyprlock.conf"
        deploy_hypr_modules
        run cp "$DOT/hypr/hyprland.lua" "$HYPR/hyprland.lua"
        run cp "$REPO/extras/hypr/hypridle.conf" "$HYPR/hypridle.conf"
        run cp "$REPO/extras/hypr/hyprlock.conf" "$HYPR/hyprlock.conf"
        [[ -e "$HYPR/hyprlock/colors.conf" ]] || { run mkdir -p "$HYPR/hyprlock"; run cp "$REPO/extras/hypr/hyprlock/colors.conf" "$HYPR/hyprlock/colors.conf"; }
        ok "Hyprland (full)" ;;
    inject)
        backup "$HYPR/hyprland.lua"
        deploy_hypr_modules
        run touch "$HYPR/hyprland.lua"
        [[ "$DRY_RUN" == 1 ]] || sed -i '/require("material3ui\/material3ui")/d' "$HYPR/hyprland.lua"
        for mod in shell colors user_persistence; do
            if ! grep -qs "material3ui[./]$mod\"" "$HYPR/hyprland.lua"; then
                [[ "$DRY_RUN" == 1 ]] || printf 'require("material3ui.%s")\n' "$mod" >> "$HYPR/hyprland.lua"
            fi
        done
        ok "Hyprland (injected shell module)" ;;
    skip) dim "Hyprland config left untouched." ;;
esac

run mkdir -p "$HOME/.local/bin"
run install -m 755 "$REPO/dotfiles/.local/bin/material3uictl" "$HOME/.local/bin/material3uictl"
run ln -sf "$CONFIG_HOME/quickshell/material3ui/scripts/sysmon/material3ui-sysmon" "$HOME/.local/bin/material3ui-sysmon"
ok "material3uictl and material3ui-sysmon"

# Retire Material3UI v1 helpers (the shell now ships its own polkit agent,
# clipboard, wallpaper and update services).
if [[ -e "$HOME/.local/bin/material3ui-system" || -e "$CONFIG_HOME/systemd/user/material3ui-update.timer" ]]; then
    run systemctl --user disable --now material3ui-update.timer 2>/dev/null || true
    run rm -f "$HOME/.local/bin/material3ui-system" "$HOME/.local/bin/material3ui-polkit" \
        "$HOME/.local/bin/material3ui-update-check" \
        "$CONFIG_HOME/systemd/user/material3ui-update.service" \
        "$CONFIG_HOME/systemd/user/material3ui-update.timer"
    ok "Removed Material3UI v1 helpers"
fi

# Default applications
if command -v xdg-mime >/dev/null 2>&1; then
    run xdg-mime default "$(desktop_for "$FILE_MANAGER")" inode/directory
    run xdg-settings set default-web-browser "$(desktop_for "$BROWSER")" 2>/dev/null || warn "Could not set the default browser (is $BROWSER installed?)."
    ok "Default file manager and browser"
fi
if [[ "$FILE_MANAGER" == nemo ]] && command -v gsettings >/dev/null 2>&1; then
    run gsettings set org.cinnamon.desktop.default-applications.terminal exec "$TERMINAL" 2>/dev/null || true
fi

[[ "$CHANNEL" == release ]] && state_channel=stable || state_channel=canary
run mkdir -p "$STATE_DIR"
write_file "$STATE_DIR/install_state.json" "{
  \"inject\": $([[ "$HYPR_MODE" == inject ]] && echo true || echo false),
  \"hypr_mode\": \"$HYPR_MODE\",
  \"install_dir\": \"$REPO\",
  \"channel\": \"$state_channel\",
  \"terminal\": \"$TERMINAL\",
  \"file_manager\": \"$FILE_MANAGER\",
  \"browser\": \"$BROWSER\"
}"
done_ "Configs deployed."

# ── 7. Summary ───────────────────────────────────────────────────────────────
step "Done"
ok "Material3UI Shell $(cat "$REPO/VERSION" 2>/dev/null) installed"
[[ ${#BACKED_UP[@]} -gt 0 ]] && dim "Backed up ${#BACKED_UP[@]} item(s) to ${BACKUP_DIR/#$HOME/\~}"
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) warn "Add ~/.local/bin to your PATH to use material3uictl." ;; esac
say "Personal Hyprland tweaks: ${C_ACCENT}~/.config/hypr/material3ui/user.lua${C_RST}"
say "Keys: ${C_ACCENT}SUPER${C_RST} Spotlight · ${C_ACCENT}SUPER+Return${C_RST} $TERMINAL · ${C_ACCENT}SUPER+E${C_RST} $FILE_MANAGER · ${C_ACCENT}SUPER+B${C_RST} $BROWSER · ${C_ACCENT}SUPER+I${C_RST} settings"
if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    if confirm "Reload Hyprland and start the shell now?" Y; then
        run hyprctl reload >/dev/null
        run "$CONFIG_HOME/quickshell/material3ui/scripts/restartshell.sh"
    fi
else
    say "Log into Hyprland to start Material3UI Shell."
fi
dim "Log: $LOG_FILE"
done_ "Enjoy Material3UI."
