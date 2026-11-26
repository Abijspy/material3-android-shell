#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  Material3UI installer · Arch Linux + niri 26.04+
#
#  A full-screen installer in the shell's own Material 3 colours. It asks
#  everything first (↑↓ to move, Space to tick, Enter to go on, Backspace to
#  go back), shows one review, and only then installs, with live progress:
#  niri and the packages, the native shell (built from shell/ and installed
#  to /usr as a pacman package), the configs (every replaced file backed up,
#  with a restore.sh beside the backups), and the session (services, login
#  screen, default apps). Plain prompts with --plain, --yes, a small
#  terminal, or no terminal at all.
#
#    ./install.sh                 the full-screen installer
#    ./install.sh --plain         the same questions, as plain prompts
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
readonly MIN_NIRI="26.04"
TS="$(date +%Y%m%d-%H%M%S)"
readonly TS
readonly CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
readonly STATE_DIR="$CONFIG_HOME/material3ui"
readonly STATE_FILE="$STATE_DIR/install_state.json"
readonly BACKUP_ROOT="$STATE_DIR/backups"
readonly BACKUP_DIR="$BACKUP_ROOT/$TS"
readonly LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/material3ui"
readonly LOG="$LOG_DIR/install-$TS.log"
readonly NIRI="$CONFIG_HOME/niri"
# A fresh config.kdl starts as niri's own default
readonly NIRI_DEFAULT=/usr/share/doc/niri/default-config.kdl

# ── Options ──────────────────────────────────────────────────────────────────
YES=0 DRY=0 SKIP_DEPS=0 PLAIN=0 MODE=install RESTORE_STAMP=""
TERMINAL="" FILE_MANAGER="" BROWSER="" NIRI_MODE="" CHANNEL="" GREETER="" INSTALL_DIR=""
ORIG_ARGS=("$@")

# The choices, "value:description", the first the default
readonly TERMINALS=("material3ui-terminal:the shell's own: Material You colours, tabs" "alacritty:GPU terminal, recoloured with the wallpaper" "kitty:feature-rich" "foot:minimal and fast" "ghostty:modern GPU terminal")
readonly FILE_MANAGERS=("files:the shell's own Files (its own package, installed with the shell)" "nemo:Cinnamon's" "thunar:light, from Xfce" "nautilus:GNOME Files" "dolphin:KDE's")
readonly BROWSERS=("librewolf:privacy-minded Firefox" "firefox:Mozilla Firefox" "chromium:open-source Chromium" "zen-browser:Zen (AUR)")
readonly CHANNELS=("latest:follow main" "release:tagged releases only")
readonly NIRI_MODES=("include:keep your config.kdl (or niri's default), include the shell's at its end" "skip:leave niri's config alone")

usage() {
    cat <<'EOF'
Usage: install.sh [options]

  -y, --yes                 Accept defaults (or your previous choices); no questions
  -n, --dry-run             Show the plan and every step; change nothing
      --plain               Plain prompts instead of the full-screen installer
      --skip-deps           Don't install packages (build and configure only)
      --terminal NAME       material3ui-terminal | alacritty | kitty | foot | ghostty
      --file-manager NAME   nemo | thunar | nautilus | dolphin
      --browser NAME        librewolf | firefox | chromium | zen-browser
      --niri MODE           include: keep your config.kdl (or start from niri's
                            default), include the shell's material3ui.kdl at its end
                            skip: leave niri's config alone
      --channel NAME        latest (follow main) | release (tagged releases)
      --greeter             Also install the login screen (greetd)
      --dir PATH            Where to clone when run outside the repo
      --restore [STAMP]     Undo an install's config changes (default: the last)
      --uninstall           Remove the shell from /usr (configs stay; see --restore)
  -h, --help                This help

Keys in the full-screen installer: ↑↓ move · Space ticks · Enter goes on ·
Backspace or ← goes back · Esc quits (nothing changes before "Install now").
EOF
}

need() { [[ -n "${2:-}" && "${2:0:1}" != - ]] || { echo "install.sh: $1 needs a value" >&2; exit 2; }; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes) YES=1 ;;
        -n|--dry-run) DRY=1 ;;
        --plain) PLAIN=1 ;;
        --skip-deps) SKIP_DEPS=1 ;;
        --terminal) need "$1" "${2:-}"; TERMINAL="$2"; shift ;;
        --file-manager) need "$1" "${2:-}"; FILE_MANAGER="$2"; shift ;;
        --browser) need "$1" "${2:-}"; BROWSER="$2"; shift ;;
        --niri) need "$1" "${2:-}"; NIRI_MODE="$2"; shift ;;
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
# The full-screen installer's state: on screen (UI_ON), and the files its
# install step shares with the screen drawing it
UI=plain UI_ON=0 WARN_FILE="" FAIL_FILE="" STATE="" CUR_SECTION="" RUN_DIR=""
section() {
    CUR_SECTION="$1"
    state_put 0 ""
    printf '\n%s%s▍%s %s%s%s\n' "$P" "$B" "$R" "$B" "$1" "$R"
}
info()    { printf '  %s\n' "$*"; }
note()    { printf '  %s%s%s\n' "$D" "$*" "$R"; }
ok()      { printf '  %s✓%s %s\n' "$G" "$R" "$*"; }
warn() {
    printf '  %s!%s %s\n' "$Y" "$R" "$*"
    # The install step's warnings, for the Done screen's notes
    if [[ -n "$WARN_FILE" ]]; then printf '%s\n' "$*" >>"$WARN_FILE"; fi
}
fail() {
    # Off the full screen first, so the reason stays readable
    if [[ "$UI_ON" == 1 ]]; then ui_leave; fi
    printf '\n  %s✗ %s%s\n' "$E$B" "$*" "$R" >&2
    if [[ -s "${LOG:-}" ]]; then printf '  %sLog: %s%s\n' "$D" "$LOG" "$R" >&2; fi
    exit 1
}
tilde()   { printf '%s' "${1/#$HOME/\~}"; }

ask() { # ask "Question" → the reply ("" when unattended)
    local reply=""
    [[ "$YES" == 1 ]] || read -r -p "  ${A}?${R} $1 " reply </dev/tty || true
    printf '%s' "$reply"
}

# ── Plain prompts (--plain, or a terminal too small for the full screen) ─────
# With a terminal both ways, menus move with the arrows and questions take
# y or n at once; otherwise (a pipe, a log) typed answers
KEYS=0
if [[ "$YES" == 0 && -t 1 ]] && { : </dev/tty; } 2>/dev/null; then KEYS=1; fi
cols() { # the terminal's width (TERM_COLS when a step has measured it)
    [[ -z "${TERM_COLS:-}" ]] || { echo "$TERM_COLS"; return; }
    local s
    s="$(stty size </dev/tty 2>/dev/null || true)"
    s="${s#* }"
    # A terminal that won't say (0, or nothing): 80
    if [[ "$s" =~ ^[0-9]+$ ]] && (( s >= 40 )); then echo "$s"; else echo 80; fi
}
key() { # key → up, down, left, right, home, end, enter, space, tab, esc, eof, or the character typed
    local k rest
    IFS= read -rsn1 k </dev/tty || { echo eof; return; }
    case "$k" in
        $'\e')
            IFS= read -rsn2 -t 0.05 rest </dev/tty || true
            case "$rest" in
                '[A'|OA) echo up ;; '[B'|OB) echo down ;; '[C'|OC) echo right ;; '[D'|OD) echo left ;;
                '[H'|OH) echo home ;; '[F'|OF) echo end ;;
                '[1'|'[7') IFS= read -rsn1 -t 0.05 rest </dev/tty || true; echo home ;;
                '[4'|'[8') IFS= read -rsn1 -t 0.05 rest </dev/tty || true; echo end ;;
                *) echo esc ;;
            esac ;;
        '') echo enter ;;
        ' ') echo space ;;
        $'\t') echo tab ;;
        *) printf '%s' "$k" ;;
    esac
}
# The options, the one under the cursor lit; drawn again in place as it moves
menu_draw() {
    local i desc width
    width=$(($(cols) - 26))
    for i in "${!opts[@]}"; do
        desc="${opts[$i]#*:}"
        (( ${#desc} <= width )) || desc="${desc:0:$((width > 1 ? width - 1 : 0))}…"
        if (( i == cur )); then printf '\e[2K    %s❯ %d  %-13s%s%s\n' "$P$B" $((i + 1)) "${values[$i]}" "$R" "$desc"
        else printf '\e[2K    %s  %d  %-13s%s%s\n' "$D" $((i + 1)) "${values[$i]}" "$desc" "$R"; fi
    done
}
menu() { # menu VAR "Title" "value:description"... (the first is the default)
    local var="$1" title="$2" cur=0 k n
    shift 2
    local opts=("$@") values=("${@%%:*}")
    n=${#opts[@]}
    printf '  %s%s%s  %s↑↓ move · Enter picks · 1–%d jumps%s\n\e[?25l' "$B" "$title" "$R" "$D" "$n" "$R"
    menu_draw
    while true; do
        k="$(key)"
        case "$k" in
            up|k) cur=$(((cur - 1 + n) % n)) ;;
            down|j|tab) cur=$(((cur + 1) % n)) ;;
            home|g) cur=0 ;;
            end|G) cur=$((n - 1)) ;;
            enter|space|right|l|eof) break ;;
            [1-9]) if (( k <= n )); then cur=$((k - 1)); printf '\e[%dA' "$n"; menu_draw; break; fi ;;
            *) continue ;;
        esac
        printf '\e[%dA' "$n"
        menu_draw
    done
    # Folded into the answer: one line in place of the list
    printf '\e[%dA\e[J  %s✓%s %s  %s%s%s  %s%s%s\n\e[?25h' $((n + 1)) "$G" "$R" "$title" "$B" "${values[$cur]}" "$R" "$D" "${opts[$cur]#*:}" "$R"
    printf -v "$var" '%s' "${values[$cur]}"
}
confirm() { # confirm "Question" Y|N
    if [[ "$YES" == 1 ]]; then [[ "$2" == Y ]]; return; fi
    local hint reply
    if [[ "$KEYS" == 1 ]]; then
        local yes=0 k one lead
        [[ "$2" == Y ]] && yes=1
        # On one line when it fits; a long question gets the answers under it
        if (( ${#1} + 32 < $(cols) )); then one=1; lead="  ${A}?${R} $1  "
        else one=0; printf '  %s?%s %s\n' "$A" "$R" "$1"; lead="    "; fi
        printf '\e[?25l'
        while true; do
            if [[ "$yes" == 1 ]]; then printf '\r\e[2K%s%s❯ Yes%s   %sNo%s  %sy/n · ←→ · Enter%s' "$lead" "$P$B" "$R" "$D" "$R" "$D" "$R"
            else printf '\r\e[2K%s%s  Yes%s %s❯ No%s  %sy/n · ←→ · Enter%s' "$lead" "$D" "$R" "$P$B" "$R" "$D" "$R"; fi
            k="$(key)"
            case "$k" in
                y|Y) yes=1; break ;;
                n|N) yes=0; break ;;
                enter|space|eof) break ;;
                left|right|tab|h|l) yes=$((1 - yes)) ;;
            esac
        done
        reply=No
        [[ "$yes" == 1 ]] && reply=Yes
        if [[ "$one" == 1 ]]; then printf '\r\e[2K  %s✓%s %s  %s%s%s\n\e[?25h' "$G" "$R" "$1" "$B" "$reply" "$R"
        else printf '\r\e[2K    %s✓%s %s%s%s\n\e[?25h' "$G" "$R" "$B" "$reply" "$R"; fi
        [[ "$yes" == 1 ]]
        return
    fi
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
    if [[ "$KEYS" == 1 ]]; then menu "$var" "$title" "${opts[@]}"; return 0; fi
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
# A flag's value must be one of the choices: said at once, not halfway
one_of() { # one_of VALUE "Name" "value:description"...
    local v="$1" name="$2"
    shift 2
    local values=("${@%%:*}")
    [[ -z "$v" || " ${values[*]} " == *" $v "* ]] || fail "$name: '$v' isn't one of: ${values[*]}"
}

# ── Progress: Material 3 Expressive's wavy indicator, in braille ──────────
# The run's steps weigh what they take (the build ten times a copy). Done
# is a solid line, the running step a wave travelling along its share, and
# what's left the track. Each step passes its weight as W=… (1 by default)
PROG_DONE=0 PROG_TOTAL=0
# Braille cells with one dot in each column: [left row × 4 + right row]
readonly WAVE_CELLS=(⠉ ⠑ ⠡ ⢁ ⠊ ⠒ ⠢ ⢂ ⠌ ⠔ ⠤ ⢄ ⡈ ⡐ ⡠ ⣀)
# The Linux console's fonts have no Braille or heavy lines: plain ASCII there
if [[ "${TERM:-}" == linux ]]; then
    BAR_DONE="=" BAR_LEFT="-" BAR_CELLS=("~" "-")
else
    BAR_DONE="━" BAR_LEFT="─" BAR_CELLS=()
fi
readonly WAVE_ROWS=(1 0 0 1 2 3 3 2) # a sine, top row 0, sampled twice a cell
bar() { # bar FRAME WEIGHT → the bar, coloured, BAR_W cells wide
    local w=${BAR_W:-24} total=$((PROG_TOTAL > 0 ? PROG_TOTAL : 1)) done run x a b out=""
    done=$((PROG_DONE * w / total))
    (( done <= w )) || done=$w
    run=$(((PROG_DONE + $2) * w / total - done))
    (( run > 0 || $2 == 0 )) || run=1
    (( done + run <= w )) || run=$((w - done))
    out+="$P"
    for ((x = 0; x < done; x++)); do out+="$BAR_DONE"; done
    for ((x = 0; x < run; x++)); do
        if [[ ${#BAR_CELLS[@]} -gt 0 ]]; then out+="${BAR_CELLS[(x + $1) % 2]}"; continue; fi
        a=$(((2 * x - $1) % 8 + 8)) b=$(((2 * x + 1 - $1) % 8 + 8))
        out+="${WAVE_CELLS[WAVE_ROWS[a % 8] * 4 + WAVE_ROWS[b % 8]]}"
    done
    out+="$R$D"
    for ((x = done + run; x < w; x++)); do out+="$BAR_LEFT"; done
    printf '%s%s' "$out" "$R"
}
percent() { local t=$((PROG_TOTAL > 0 ? PROG_TOTAL : 1)); echo $((PROG_DONE * 100 / t > 100 ? 100 : PROG_DONE * 100 / t)); }
progress_add() { PROG_DONE=$((PROG_DONE + $1)); state_put 0 ""; }
# The bar with the step beside it, the label cut to the terminal
progress_line() { # progress_line FRAME WEIGHT "Label" [SECONDS]
    local label="$3" room
    room=$(($(cols) - ${BAR_W:-24} - 18))
    (( ${#label} <= room )) || label="${label:0:$((room > 1 ? room - 1 : 0))}…"
    printf '\r\e[2K  %s %s%3d%%%s  %s%s' "$(bar "$1" "$2")" "$B" "$(percent)" "$R" "$label" "${4:+ ${D}${4}s${R}}"
}
# Where the install stands, for the full screen drawing it: done and total
# weight, the running step's weight, the section and the step
state_put() { # state_put WEIGHT "Label"
    [[ "$UI" == tui && -n "$STATE" ]] || return 0
    printf '%s\t%s\t%s\t%s\t%s\n' "$PROG_DONE" "$PROG_TOTAL" "$1" "$CUR_SECTION" "$2" >"$STATE.tmp"
    mv -f "$STATE.tmp" "$STATE"
}

# A quiet step: the bar while it works, its output in the log, and the end
# of the log if it fails
SPIN_PID=""
stop_spin() {
    [[ -n "$SPIN_PID" ]] || return 0
    kill "$SPIN_PID" 2>/dev/null || true
    wait "$SPIN_PID" 2>/dev/null || true
    SPIN_PID=""
    printf '\r\e[2K'
}
# What went wrong, from this step's part of the log: the errors themselves
# (a parallel build prints them well above its last lines), else its end
show_failure() {
    local part errs
    part="$(awk '/^── /{buf=""} {buf=buf $0 "\n"} END{printf "%s", buf}' "$LOG")"
    errs="$(grep -E -A6 '^FAILED:|error:|Killed signal|No space left|Out of memory|cannot allocate' <<<"$part" | head -n 40 || true)"
    if [[ -n "$errs" ]]; then printf '%s\n' "$errs"; else tail -n 15 <<<"$part"; fi | sed "s/^/    /"
}
task() { # [W=weight] task "Label" command...
    local label="$1" start=$SECONDS rc=0 weight="${W:-1}"
    shift
    if [[ "$DRY" == 1 ]]; then note "[dry-run] $label: $*"; return 0; fi
    state_put "$weight" "$label"
    if [[ -t 1 ]]; then
        ( frame=0 TERM_COLS="$(cols)"
          while true; do progress_line "$frame" "$weight" "$label" $((SECONDS - start)); frame=$((frame + 1)); sleep 0.08; done ) &
        SPIN_PID=$!
    fi
    printf '── %s: %s\n' "$label" "$*" >>"$LOG"
    "$@" >>"$LOG" 2>&1 || rc=$?
    stop_spin
    if [[ $rc -ne 0 ]]; then
        printf '  %s✗%s %s\n' "$E" "$R" "$label"
        # The full screen's failure card shows the same lines
        if [[ -n "$FAIL_FILE" ]]; then show_failure | tee "$FAIL_FILE"; else show_failure; fi
        fail "$label failed"
    fi
    progress_add "$weight"
    ok "$label ${D}$((SECONDS - start))s${R}"
}
# A step that may need the terminal (package prompts): output shown as it
# runs, under the bar as it stands. Behind the full screen nothing may ask,
# so it's a quiet step there (its commands take --noconfirm)
live() { # [W=weight] live "Label" command...
    local label="$1" weight="${W:-1}"
    shift
    if [[ "$UI" == tui ]]; then W=$weight task "$label" "$@"; return; fi
    if [[ "$DRY" == 1 ]]; then note "[dry-run] $label: $*"; return 0; fi
    if [[ -t 1 ]]; then progress_line 0 "$weight" "${B}$label${R}"; echo; else info "${B}$label${R}"; fi
    "$@" 2>&1 | tee -a "$LOG" | sed "s/^/    /" || fail "$label failed"
    progress_add "$weight"
}
do_() { if [[ "$DRY" == 1 ]]; then note "[dry-run] $*"; else "$@"; fi; }
trap 'rc=$?; stop_spin; if [[ "$UI_ON" == 1 ]]; then ui_leave; fi; printf "\n  %sStopped at line %s (exit %s). Log: %s%s\n" "$E" "$LINENO" "$rc" "${LOG:-}" "$R" >&2; exit $rc' ERR
trap 'stop_spin; if [[ "$UI_ON" == 1 ]]; then ui_leave; fi; printf "%s" "$R"; [[ ! -t 1 ]] || printf "\e[?25h"; [[ -z "$RUN_DIR" ]] || rm -rf "$RUN_DIR"' EXIT

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
        git -C "$INSTALL_DIR" fetch -q --tags --force origin
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
# The shell's own Files and terminal are in the shell: no package
pkg_for() { case "$1" in zen-browser) echo zen-browser-bin ;; files | material3ui-terminal) ;; *) echo "$1" ;; esac; }
desktop_for() {
    case "$1" in
        alacritty) echo Alacritty.desktop ;; ghostty) echo com.mitchellh.ghostty.desktop ;;
        nautilus) echo org.gnome.Nautilus.desktop ;; dolphin) echo org.kde.dolphin.desktop ;;
        zen-browser) echo zen.desktop ;; files) echo material3ui-files.desktop ;; *) echo "$1.desktop" ;;
    esac
}
# The packages by what they're for, in the order they install: niri first,
# then what the shell builds and runs with, then the rest
readonly PKG_GROUPS=(compositor shell tools apps extras)
group_label() {
    case "$1" in
        compositor) echo niri ;; shell) echo Shell ;; tools) echo "Tools & theming" ;; apps) echo Apps ;; extras) echo Extras ;;
    esac
}
group_packages() { # group_packages GROUP → its packages, one a line
    local deps="$REPO/data/dependencies.json"
    case "$1" in
        compositor) jq -r '.niri[].name' "$deps" ;;
        build) jq -r '.core[].name' "$deps" ;; # what meson needs: part of "shell"
        shell) jq -r '.core[].name, .services[].name' "$deps" ;;
        tools) jq -r '.utilities[].name, .theming[].name' "$deps" ;;
        apps)
            printf '%s\n' gnome-keyring git "$(pkg_for "$TERMINAL")" "$(pkg_for "$FILE_MANAGER")" "$(pkg_for "$BROWSER")"
            if [[ "$FILE_MANAGER" == nemo ]]; then echo nemo-fileroller; fi ;;
        extras)
            if [[ "$OPT_FONTS" == 1 ]]; then jq -r '.fonts[].name' "$deps"; fi
            if [[ "$OPT_FISH" == 1 ]]; then printf '%s\n' fish starship; fi
            if [[ "$OPT_KDE" == 1 ]]; then echo python-pip; fi
            if [[ "$GREETER" == 1 ]]; then printf '%s\n' greetd acl; fi ;;
    esac
}
# A package's version: installed, or in the sync repositories ("" when none)
installed_version() { pacman -Q "$1" 2>/dev/null | awk '{print $2}' || true; }
repo_version() { LC_ALL=C pacman -Si "$1" 2>/dev/null | sed -n 's/^Version *: //p' | head -n 1 || true; }
older() { [[ -n "$1" && "$(vercmp "${1%%-*}" "$2")" -lt 0 ]]; } # older VERSION MIN
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
        # Copied in beside it, then swapped in: never missing meanwhile
        restore_line "mkdir -p \"\$(dirname \"\$HOME/$rel\")\"; rm -rf \"\$HOME/$rel.m3restore\"; cp -a \"\$B/$rel\" \"\$HOME/$rel.m3restore\"; rm -rf \"\$HOME/$rel\"; mv -T \"\$HOME/$rel.m3restore\" \"\$HOME/$rel\""
    else
        restore_line "rm -rf \"\$HOME/$rel\""
    fi
}
# replace SRC DST: DST becomes a copy of SRC with no moment in between where
# it's missing or half written: niri reads its config again as soon as a
# file of it changes
replace() {
    local tmp
    tmp="$(dirname "$2")/.$(basename "$2").m3new"
    rm -rf "$tmp"
    cp -a "$1" "$tmp"
    # A folder can't be renamed over a full one
    if [[ -d "$tmp" && -d "$2" && ! -L "$2" ]]; then rm -rf "$2"; fi
    mv -fT "$tmp" "$2"
}
put() { # put SRC DST: replace a file or directory, guarded
    guard "$2"
    if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$2")"; return 0; fi
    do_ mkdir -p "$(dirname "$2")"
    do_ replace "$1" "$2"
}
# Configs carrying our marker are ours to replace; any other is yours
ours() { [[ ! -e "$1" ]] || grep -qs -e "# Material3UI Configuration" -e "templates/GeneratedColors.qml" "$1"; }
niri_included() { grep -qsE '^\s*include\s+(optional=#?true\s+)?"(\./)?material3ui\.kdl"' "$NIRI/config.kdl"; }

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
    ok "Restored. niri reads its config again by itself."
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

# ── What's here ──────────────────────────────────────────────────────────────
detect() {
    OS_NAME="$(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}")"
    OS_ARCH=0
    if (. /etc/os-release 2>/dev/null; [[ " ${ID:-} ${ID_LIKE:-} " == *" arch "* ]]); then OS_ARCH=1; fi
    # niri: installed (and new enough?), and what the repositories have
    nv="$(installed_version niri)" nr="$(repo_version niri)"
    VERSION="$(cat "$REPO/VERSION" 2>/dev/null || echo "?")"
    # The shell talks to NetworkManager. iwd or systemd-networkd running
    # beside it (Arch's install defaults) fight it for the Wi-Fi card: the
    # connection flips between them and the shell shows none
    NET_RIVALS=()
    local svc dm
    for svc in iwd systemd-networkd; do
        if systemctl is-enabled --quiet "$svc.service" 2>/dev/null || systemctl is-active --quiet "$svc.service" 2>/dev/null; then
            NET_RIVALS+=("$svc")
        fi
    done
    # The login manager in use, which the login screen would replace
    CURRENT_DM=""
    dm="$(readlink /etc/systemd/system/display-manager.service 2>/dev/null || true)"
    if [[ -n "$dm" ]]; then CURRENT_DM="$(basename "$dm" .service)"; fi
    # Set up by an install from before 7.0, when the shell also ran there
    OLD_COMPS=()
    if [[ -d "$CONFIG_HOME/hypr/material3ui" ]]; then OLD_COMPS+=(Hyprland); fi
    if [[ -f "$CONFIG_HOME/umbriel/material3ui.toml" ]]; then OLD_COMPS+=(Umbriel); fi
}
# niri can't come from the repositories new enough, and isn't here
niri_unavailable() { [[ -z "$nv" ]] && { [[ -z "$nr" ]] || older "$nr" "$MIN_NIRI"; }; }
# niri is here but too old: a full system update brings the new one (Arch
# doesn't do partial upgrades), before anything else installs
niri_outdated() { older "$nv" "$MIN_NIRI"; }
niri_status() { # one line on where niri stands
    if [[ -z "$nv" ]]; then
        if niri_unavailable; then echo "not installed; your repositories don't have $MIN_NIRI or newer"
        else echo "not installed: installs first (${nr%-*})"; fi
    elif niri_outdated; then echo "${nv%-*}, older than $MIN_NIRI: the system update brings ${nr:+${nr%-*}}${nr:-a newer one}"
    else echo "${nv%-*} installed"; fi
}
print_checks() {
    section "Checking the system"
    if [[ "$OS_ARCH" == 1 ]]; then ok "$OS_NAME"; else warn "$OS_NAME isn't Arch-based; going on, since pacman is here"; fi
    if [[ -n "$nv" ]] && ! niri_outdated; then ok "niri ${nv%-*}"
    elif niri_unavailable || niri_outdated; then warn "niri: $(niri_status)"
    else note "niri: $(niri_status)"; fi
    ok "Material3UI $VERSION from $(tilde "$REPO")"
    if [[ ${#OLD_COMPS[@]} -gt 0 ]]; then
        warn "${OLD_COMPS[*]}: the shell runs on niri only now; that config is left as it is"
    fi
    if [[ -f "$STATE_FILE" ]]; then note "Installed before: your previous choices are the defaults."; fi
}

# ── Choices ──────────────────────────────────────────────────────────────────
OPT_FONTS=1 OPT_SERVICES=1 OPT_KDE=0 OPT_FISH=0 OPT_NET_ONLY=1 SYS_UPDATE=1
validate_flags() {
    one_of "$TERMINAL" Terminal "${TERMINALS[@]}"
    one_of "$FILE_MANAGER" "File manager" "${FILE_MANAGERS[@]}"
    one_of "$BROWSER" Browser "${BROWSERS[@]}"
    one_of "$NIRI_MODE" "niri config" "${NIRI_MODES[@]}"
    one_of "$CHANNEL" Updates "${CHANNELS[@]}"
}
# Last time's answers, where no flag gave one
apply_previous() {
    [[ -n "$TERMINAL" ]] || TERMINAL="$(previous terminal)"
    [[ -n "$FILE_MANAGER" ]] || FILE_MANAGER="$(previous file_manager)"
    [[ -n "$BROWSER" ]] || BROWSER="$(previous browser)"
    # niri's mode only from an install that set niri up
    if [[ -z "$NIRI_MODE" && ",$(previous compositor)," == *",niri,"* ]]; then NIRI_MODE="$(previous niri_mode)"; fi
    # Answers from before that aren't on offer now: the default
    offered "$TERMINAL" "${TERMINALS[@]}" || TERMINAL=""
    offered "$FILE_MANAGER" "${FILE_MANAGERS[@]}" || FILE_MANAGER=""
    offered "$BROWSER" "${BROWSERS[@]}" || BROWSER=""
    offered "$NIRI_MODE" "${NIRI_MODES[@]}" || NIRI_MODE=""
    return 0
}
offered() { # offered VALUE "value:description"...: one of them (or none given)
    local v="$1"
    shift
    local values=("${@%%:*}")
    [[ -z "$v" || " ${values[*]} " == *" $v "* ]]
}
previous_channel() {
    if [[ -z "$CHANNEL" ]]; then
        case "$(previous channel)" in stable) CHANNEL=release ;; canary) CHANNEL=latest ;; esac
    fi
    return 0
}
last_summary() { printf '%s · %s · %s · niri %s' "$(previous terminal)" "$(previous file_manager)" "$(previous browser)" "$(previous niri_mode)"; }

plain_decisions() {
    section "Your setup"
    local keep=1
    if [[ -f "$STATE_FILE" && "$YES" == 0 && -z "$TERMINAL$FILE_MANAGER$BROWSER$NIRI_MODE" ]]; then
        info "Last time: $(last_summary)"
        confirm "Keep these?" Y || keep=0
    fi
    if [[ "$keep" == 1 ]]; then apply_previous; fi
    previous_channel
    SYS_UPDATE=0
    if niri_outdated && [[ "$SKIP_DEPS" == 0 ]]; then
        note "niri ${nv%-*} is too old for the shell (it needs $MIN_NIRI)."
        if confirm "Update the system first (sudo pacman -Syu)?" Y; then SYS_UPDATE=1
        else warn "Without the update the shell won't run on niri ${nv%-*}"; fi
    fi
    if niri_unavailable; then
        warn "niri ${nr:+${nr%-*} }can't come from your repositories (the shell needs $MIN_NIRI or newer): install it yourself"
    fi
    choose NIRI_MODE "niri config" "${NIRI_MODES[@]}"
    choose TERMINAL "Terminal" "${TERMINALS[@]}"
    choose FILE_MANAGER "File manager" "${FILE_MANAGERS[@]}"
    choose BROWSER "Browser" "${BROWSERS[@]}"
    choose CHANNEL "Updates" "${CHANNELS[@]}"

    OPT_FONTS=0 OPT_SERVICES=0 OPT_KDE=0 OPT_FISH=0 OPT_NET_ONLY=0
    info "${B}Extras${R}"
    confirm "Fonts (Google Sans Flex, Material Symbols, JetBrains Mono)?" Y && OPT_FONTS=1
    confirm "Turn on NetworkManager and Bluetooth?" Y && OPT_SERVICES=1
    if [[ "$OPT_SERVICES" == 1 && ${#NET_RIVALS[@]} -gt 0 ]]; then
        note "${NET_RIVALS[*]} also manage the network, and fight NetworkManager for the Wi-Fi."
        confirm "Turn ${NET_RIVALS[*]} off so NetworkManager alone manages it? (Wi-Fi reconnects through NetworkManager; pick your network again if it asks)" Y && OPT_NET_ONLY=1
    fi
    confirm "Material You colours for KDE/Qt apps (a Python venv)?" N && OPT_KDE=1
    confirm "fish with the starship prompt?" N && OPT_FISH=1
    if [[ -z "$GREETER" ]]; then
        if confirm "The Material3UI login screen (greetd; replaces your display manager)?" N; then GREETER=1; else GREETER=0; fi
    fi
    return 0
}

# ── The plan ─────────────────────────────────────────────────────────────────
ensure_jq() {
    command -v jq >/dev/null && return 0
    [[ "$DRY" == 0 ]] || fail "--dry-run reads the package list with jq: sudo pacman -S jq, then run it again"
    section "Getting ready"
    # Password first, in the open: a task's spinner would draw over sudo's prompt
    sudo -v || fail "sudo is needed for packages and for installing the shell"
    task "jq, to read the package list" sudo pacman -S --needed --noconfirm jq
}
# What each group still needs (pacman -T prints what isn't satisfied
# locally; it honours provides), the configs written and those left as
# yours. Run again whenever the choices change
declare -A MISS=() TOTAL=()
compute_plan() {
    MISS=() TOTAL=() PKG_COUNT=0 MISSING_COUNT=0 CONFIGS=() KEPT=() NIRI_RIVALS=""
    local g p seen=" " list m
    if [[ "$SKIP_DEPS" == 0 ]]; then
        for g in "${PKG_GROUPS[@]}"; do
            list=()
            while read -r p; do
                [[ -n "$p" && "$seen" != *" $p "* ]] || continue
                seen+="$p "
                list+=("$p")
            done < <(group_packages "$g")
            TOTAL[$g]=${#list[@]}
            PKG_COUNT=$((PKG_COUNT + ${#list[@]}))
            [[ ${#list[@]} -gt 0 ]] || continue
            MISS[$g]="$(pacman -T "${list[@]}" 2>/dev/null | tr '\n' ' ' || true)"
            read -ra m <<<"${MISS[$g]}"
            MISSING_COUNT=$((MISSING_COUNT + ${#m[@]}))
        done
    fi
    cfg "$HOME/.local/share/material3ui/scripts" "theme hooks Matugen runs"
    if ours "$CONFIG_HOME/matugen/config.toml"; then cfg "$CONFIG_HOME/matugen" "colour templates"; else KEPT+=("your matugen config"); fi
    if [[ "$TERMINAL" == alacritty ]]; then
        if ours "$CONFIG_HOME/alacritty/alacritty.toml"; then cfg "$CONFIG_HOME/alacritty/alacritty.toml" "terminal"
        else KEPT+=("your alacritty.toml (import material3ui-theme.toml for the colours)"); fi
    fi
    if [[ "$OPT_FISH" == 1 ]] && ours "$CONFIG_HOME/starship.toml"; then cfg "$CONFIG_HOME/starship.toml" "prompt"; fi
    if [[ "$NIRI_MODE" == include ]]; then
        cfg "$NIRI/material3ui.kdl" "niri: the shell, its keys and your apps"
        [[ -e "$NIRI/material3ui-user.kdl" ]] || cfg "$NIRI/material3ui-user.kdl" "yours, for niri changes updates keep"
        if [[ ! -e "$NIRI/config.kdl" ]]; then cfg "$NIRI/config.kdl" "niri's default config + include \"material3ui.kdl\""
        elif niri_included; then KEPT+=("your niri config.kdl (it includes material3ui.kdl already)")
        else cfg "$NIRI/config.kdl" "+ include \"material3ui.kdl\""; fi
        # What the shell does itself (a bar, notifications, polkit, wallpaper, idle)
        NIRI_RIVALS="$(grep -oE '^\s*spawn-(sh-)?at-startup\s+"(waybar|mako|dunst|swaync|fnott|swaybg|swayidle|[^"]*polkit[^"]*)"' "$NIRI/config.kdl" 2>/dev/null |
            grep -oE '"[^"]*"' | tr -d '"' | xargs -rn1 basename | sort -u | tr '\n' ' ' || true)"
    fi
    [[ -e "$CONFIG_HOME/quickshell/material3ui" ]] && cfg "$CONFIG_HOME/quickshell/material3ui" "v3 Quickshell shell: removed"
    local c
    for c in "${OLD_COMPS[@]}"; do KEPT+=("your $c config (the shell runs on niri only now)"); done
    return 0
}
cfg() { CONFIGS+=("$1|$2"); }
# One line of what's in the plan, as the review shows it
packages_summary() {
    local g m parts=()
    if [[ "$SKIP_DEPS" == 1 ]]; then echo "skipped (--skip-deps)"; return; fi
    if [[ "$MISSING_COUNT" -eq 0 ]]; then echo "all $PKG_COUNT already installed"; return; fi
    for g in "${PKG_GROUPS[@]}"; do
        read -ra m <<<"${MISS[$g]:-}"
        [[ ${#m[@]} -eq 0 ]] || parts+=("$(group_label "$g") ${#m[@]}")
    done
    printf '%s to install, of %s (%s)' "$MISSING_COUNT" "$PKG_COUNT" "$(printf '%s · ' "${parts[@]}" | sed 's/ · $//')"
}
configs_summary() {
    local e n=0 backed=0
    for e in "${CONFIGS[@]}"; do
        n=$((n + 1))
        [[ ! -e "${e%%|*}" ]] || backed=$((backed + 1))
    done
    printf '%s written, %s of them backed up first' "$n" "$backed"
    [[ ${#KEPT[@]} -eq 0 ]] || printf ' · %s left as yours' "${#KEPT[@]}"
}
extras_list() {
    local extras=()
    [[ "$OPT_FONTS" == 1 ]] && extras+=(fonts)
    [[ "$OPT_SERVICES" == 1 ]] && extras+=("network & bluetooth")
    [[ "$OPT_NET_ONLY" == 1 && "$OPT_SERVICES" == 1 && ${#NET_RIVALS[@]} -gt 0 ]] && extras+=("${NET_RIVALS[*]} off")
    [[ "$OPT_KDE" == 1 ]] && extras+=("KDE colours")
    [[ "$OPT_FISH" == 1 ]] && extras+=(fish)
    [[ "$GREETER" == 1 ]] && extras+=("login screen")
    local joined
    joined="$(printf '%s, ' "${extras[@]}")"
    joined="${joined%, }"
    printf '%s' "${joined:-none}"
}
print_plan() {
    section "The plan"
    row() { printf '  %s%-9s%s %s\n' "$D" "$1" "$R" "$2"; }
    row Apps "$TERMINAL · $FILE_MANAGER · $BROWSER"
    row Desktop "niri ($(niri_status); config: $NIRI_MODE)"
    row Updates "$CHANNEL"
    row Extras "$(extras_list)"
    if [[ "$SYS_UPDATE" == 1 ]]; then row Update "the whole system first (sudo pacman -Syu): niri ${nv%-*} too old"; fi
    local label g m e p k
    if [[ "$SKIP_DEPS" == 1 ]]; then row Packages "skipped (--skip-deps)"
    elif [[ "$MISSING_COUNT" -eq 0 ]]; then row Packages "all $PKG_COUNT already installed"
    else
        # In the order they install: niri, then the rest
        label=Packages
        for g in "${PKG_GROUPS[@]}"; do
            [[ "${TOTAL[$g]:-0}" -gt 0 ]] || continue
            read -ra m <<<"${MISS[$g]:-}"
            if [[ ${#m[@]} -gt 0 ]]; then
                row "$label" "$(printf '%-16s' "$(group_label "$g")")${#m[@]} to install: ${m[*]}" | fold -s -w 100 | sed '2,$s/^/                            /'
            else
                row "$label" "$(printf '%-16s' "$(group_label "$g")")${D}all ${TOTAL[$g]} installed${R}"
            fi
            label=""
        done
    fi
    row Shell "build material3ui $VERSION and install it to /usr"
    label=Configs
    for e in "${CONFIGS[@]}"; do
        p="${e%%|*}"
        if [[ -e "$p" ]]; then row "$label" "${Y}~${R} $(tilde "$p") ${D}${e#*|}; backed up${R}"
        else row "$label" "${G}+${R} $(tilde "$p") ${D}${e#*|}${R}"; fi
        label=""
    done
    for k in "${KEPT[@]}"; do row "$label" "${D}= $k${R}"; label=""; done
    if [[ -n "$NIRI_RIVALS" ]]; then
        warn "Your config.kdl also starts ${NIRI_RIVALS% }, which the shell replaces: take it out of config.kdl"
    fi
    note "Backups and a restore.sh go to $(tilde "$BACKUP_DIR")"
}

# The bar's whole: each step that will run, by its weight (the same W=…
# its call passes)
compute_weights() {
    NEED_GSF=0
    if [[ "$OPT_FONTS" == 1 ]] && ! fc-list 2>/dev/null | grep -qi "Google Sans Flex"; then NEED_GSF=1; fi
    PROG_DONE=0
    PROG_TOTAL=$((1 + 1 + 10 + 1 + 2 + 2 + 1)) # build check, configure, build, package, install, configs, session
    [[ "$SYS_UPDATE" == 0 ]] || PROG_TOTAL=$((PROG_TOTAL + 8))
    if [[ "$MISSING_COUNT" -gt 0 ]]; then
        local m
        PROG_TOTAL=$((PROG_TOTAL + 1)) # the AUR helper
        read -ra m <<<"${MISS[compositor]:-}"
        [[ ${#m[@]} -eq 0 ]] || PROG_TOTAL=$((PROG_TOTAL + 4))
        [[ "$MISSING_COUNT" -eq ${#m[@]} ]] || PROG_TOTAL=$((PROG_TOTAL + 8))
    fi
    [[ "$NEED_GSF" == 0 ]] || PROG_TOTAL=$((PROG_TOTAL + 1))
    [[ "$OPT_KDE" == 0 ]] || PROG_TOTAL=$((PROG_TOTAL + 3))
    return 0
}

# One password prompt, kept fresh to the end
KEEPALIVE=0
sudo_keepalive() {
    [[ "$DRY" == 0 && "$KEEPALIVE" == 0 ]] || return 0
    KEEPALIVE=1
    ( while kill -0 "$$" 2>/dev/null; do sudo -n true 2>/dev/null; sleep 50; done ) &
}

# ═════════════════════════════════════════════════════════════════════════════
#  The install itself: the same steps behind the full screen or in plain view
# ═════════════════════════════════════════════════════════════════════════════
run_install() {
    local flags m g update compositor rest
    # ── Packages ──
    section "Packages"
    if [[ "$DRY" == 0 && "$UI" != tui ]]; then
        sudo -v || fail "sudo is needed for packages and for installing the shell"
        sudo_keepalive
    fi
    if [[ "$SYS_UPDATE" == 1 ]]; then
        update=(-Syu)
        [[ "$YES" == 1 || "$UI" == tui ]] && update+=(--noconfirm)
        W=8 live "Updating the system, for niri $MIN_NIRI" sudo pacman "${update[@]}"
        [[ "$DRY" == 1 ]] || ok "System updated"
    fi
    if [[ "$MISSING_COUNT" -gt 0 ]]; then
        task "AUR helper (yay)" ensure_yay
        flags=(-S --needed)
        [[ "$YES" == 1 || "$UI" == tui ]] && flags+=(--noconfirm)
        # niri on its own first, then what the shell needs and the rest
        read -ra compositor <<<"${MISS[compositor]:-}"
        if [[ ${#compositor[@]} -gt 0 ]]; then
            W=4 live "niri: ${compositor[*]}" yay "${flags[@]}" "${compositor[@]}"
            [[ "$DRY" == 1 ]] || ok "niri installed"
        fi
        rest=()
        for g in "${PKG_GROUPS[@]}"; do
            [[ "$g" != compositor ]] || continue
            read -ra m <<<"${MISS[$g]:-}"
            rest+=("${m[@]}")
        done
        if [[ ${#rest[@]} -gt 0 ]]; then
            W=8 live "The shell's dependencies and your apps: ${#rest[@]} packages" yay "${flags[@]}" "${rest[@]}"
            [[ "$DRY" == 1 ]] || ok "Dependencies installed"
        fi
    elif [[ "$SKIP_DEPS" == 1 ]]; then
        note "Skipped (--skip-deps)."
    else
        note "Nothing to install."
    fi
    # niri, which the shell runs on: here, and new enough
    if [[ "$DRY" == 1 ]]; then note "[dry-run] check niri is installed and new enough"
    else
        local v
        v="$(installed_version niri)"
        if [[ -z "$v" ]]; then warn "niri isn't installed: install niri before logging into it"
        elif older "$v" "$MIN_NIRI"; then warn "niri ${v%-*} is older than $MIN_NIRI: update the system (sudo pacman -Syu) before logging into it"
        else ok "niri ${v%-*}"; fi
    fi
    if [[ "$NEED_GSF" == 1 ]]; then
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
        W=3 task "KDE colours (Python venv)" kde_venv
    fi

    # ── The shell ──
    section "The shell"
    # What the build needs, before building: a clear word, not a compiler error
    local build_pkgs lacking is
    mapfile -t build_pkgs < <(group_packages build)
    if [[ "$DRY" == 1 ]]; then note "[dry-run] check the build's packages are installed"
    else
        mapfile -t lacking < <(pacman -T "${build_pkgs[@]}" 2>/dev/null || true)
        if [[ ${#lacking[@]} -gt 0 ]]; then
            [[ ${#lacking[@]} -eq 1 ]] && is="isn't" || is="aren't"
            fail "The build needs ${lacking[*]}, which $is installed: sudo pacman -S --needed ${lacking[*]}, then run ./install.sh again"
        fi
        ok "The build's ${#build_pkgs[@]} packages are here"
    fi
    progress_add 1
    BUILD="$REPO/shell/build-release"
    local setup=()
    [[ -d "$BUILD" ]] && setup=(--reconfigure)
    task "Configuring the build" meson setup "${setup[@]}" "$BUILD" "$REPO/shell" --prefix=/usr --sysconfdir=/etc --buildtype=release
    W=10 task "Building (a minute or two the first time)" meson compile -C "$BUILD"
    # As the material3ui package, so pacman knows its files and an update's
    # package replaces them cleanly; plain meson install when it can't be made
    PKG_FILE=""
    APP_FILES=()
    PKG_OUT="$(mktemp -d)"
    package_shell() { PKG_FILE="$("$REPO/packaging/build-pkg.sh" material3ui --build "$BUILD" "$PKG_OUT")" || PKG_FILE=""; }
    # Files, the viewer, Videos, the Task Manager and the terminal: a
    # package each, from the same build (pacman -R one you don't want)
    package_apps() {
        local app f
        for app in files viewer player tasks terminal notes pdf text music disks camera backups recorder calculator fonts imagewriter store podcasts contacts mail; do
            f="$("$REPO/packaging/build-pkg.sh" "material3ui-$app" --build "$BUILD" "$PKG_OUT")" && APP_FILES+=("$f")
        done
    }
    task "Packaging it for pacman" package_shell
    [[ -z "$PKG_FILE" ]] || task "Packaging the apps" package_apps
    if [[ -n "$PKG_FILE" || "$DRY" == 1 ]]; then
        W=2 task "Installing the material3ui packages" sudo "$REPO/packaging/install-pkg.sh" "${PKG_FILE:-material3ui-$VERSION.pkg.tar.zst}" "${APP_FILES[@]}"
        # update.sh then updates the apps that stay installed
        [[ "$DRY" == 1 ]] || { mkdir -p "$HOME/.local/state/material3ui" && printf '%s\n' files viewer player tasks terminal notes pdf text music disks camera backups recorder calculator fonts imagewriter store podcasts contacts mail >"$HOME/.local/state/material3ui/apps-split"; }
        rm -rf "$PKG_OUT"
    else
        warn "Couldn't make the package (see the log): installing the build directly"
        W=2 task "Installing to /usr" sudo meson install -C "$BUILD" --no-rebuild --quiet
    fi

    # ── Configs ──
    section "Configs"
    planned() { local e; for e in "${CONFIGS[@]}"; do [[ "${e%%|*}" == "$1" ]] && return 0; done; return 1; }
    local DOT="$REPO/dotfiles/.config"
    put "$REPO/dotfiles/.local/share/material3ui/scripts" "$HOME/.local/share/material3ui/scripts"
    ok "Theme hooks"
    if planned "$CONFIG_HOME/matugen"; then
        guard "$CONFIG_HOME/matugen"
        if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$CONFIG_HOME/matugen")"; else
            mkdir -p "$CONFIG_HOME/matugen"
            cp -a "$DOT/matugen/." "$CONFIG_HOME/matugen/"
            # Templates for compositors the shell no longer runs on
            rm -rf "$CONFIG_HOME/matugen/templates/hyprland" "$CONFIG_HOME/matugen/templates/hyprlunavsc.json"
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

    # niri: material3ui.kdl (your apps in it), included at the end of config.kdl
    niri_kdl() {
        # The shell's own Files: Mod+E asks the shell to open it
        local fm="\"$FILE_MANAGER\""
        [[ "$FILE_MANAGER" != files ]] || fm='"material3ui" "msg" "files"'
        sed -e "s/\"alacritty\"/\"$TERMINAL\"/g" -e "s/\"nemo\"/$fm/g" -e "s/\"librewolf\"/\"$BROWSER\"/g" \
            "$DOT/niri/material3ui.kdl"
    }
    if [[ "$NIRI_MODE" == include ]]; then
        guard "$NIRI/material3ui.kdl"
        if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$NIRI/material3ui.kdl") (with $TERMINAL, $FILE_MANAGER and $BROWSER)"
        else mkdir -p "$NIRI"; niri_kdl >"$NIRI/.material3ui.kdl.m3new" && mv -f "$NIRI/.material3ui.kdl.m3new" "$NIRI/material3ui.kdl"; fi
        if planned "$NIRI/material3ui-user.kdl"; then
            guard "$NIRI/material3ui-user.kdl"
            if [[ "$DRY" == 1 ]]; then note "[dry-run] write $(tilde "$NIRI/material3ui-user.kdl")"; else
                cat >"$NIRI/material3ui-user.kdl" <<'EOF'
// Yours: niri settings that win over Material3UI's, included last by
// material3ui.kdl. Updates replace that file, never this one. For example,
// a key of your own in place of one of the shell's:
//
// binds {
//     Mod+D { spawn "fuzzel"; }
// }
EOF
            fi
        fi
        local niri_ok=1 niri_check
        if planned "$NIRI/config.kdl"; then
            if [[ ! -e "$NIRI/config.kdl" && ! -f "$NIRI_DEFAULT" && "$DRY" == 0 ]]; then
                # Installed without its docs (NoExtract): niri writes its default at its first start
                warn "niri's default config isn't here ($NIRI_DEFAULT): start niri once, then run ./install.sh again to include the shell"
                niri_ok=0
            else
                guard "$NIRI/config.kdl"
                if [[ "$DRY" == 1 ]]; then note "[dry-run] add include \"material3ui.kdl\" to $(tilde "$NIRI/config.kdl")"; else
                    # niri's default, minus the bar it starts: the shell is the bar
                    [[ -e "$NIRI/config.kdl" ]] || sed 's|^spawn-at-startup "waybar"|// & (Material3UI is the bar)|' "$NIRI_DEFAULT" >"$NIRI/config.kdl"
                    printf '\n// Material3UI: the shell, its keys and its Settings. Last, so its keys win\ninclude "material3ui.kdl"\n' >>"$NIRI/config.kdl"
                fi
            fi
        fi
        # niri reads its config again by itself: check what it will find
        if [[ "$DRY" == 0 && "$niri_ok" == 1 ]] && command -v niri >/dev/null; then
            if niri_check="$(niri validate -c "$NIRI/config.kdl" 2>&1)"; then ok "niri (material3ui.kdl included)"
            else
                warn "niri finds an error in its config; ./install.sh --restore puts the old one back:"
                printf '%s\n' "$niri_check" | sed 's/\x1b\[[0-9;]*m//g' | grep -v -e DEBUG -e " INFO " | tail -n 8 | sed 's/^/    /' || true
            fi
        elif [[ "$niri_ok" == 1 ]]; then ok "niri (material3ui.kdl included)"; fi
    else
        note "niri config left as it is: include \"material3ui.kdl\" yourself (docs/NIRI.md)"
    fi

    # Retired: the v3 Quickshell shell and its helpers, and v1's services
    if [[ -e "$CONFIG_HOME/quickshell/material3ui" ]]; then
        guard "$CONFIG_HOME/quickshell/material3ui"
        do_ rm -rf "$CONFIG_HOME/quickshell/material3ui"
        do_ pkill -x quickshell || true
        ok "v3 Quickshell shell removed (backed up)"
    fi
    local old=() f
    for f in "$HOME/.local/bin/"{material3uictl,material3ui-sysmon,material3ui-system,material3ui-polkit,material3ui-update-check}; do
        [[ -e "$f" ]] && old+=("$f")
    done
    if [[ ${#old[@]} -gt 0 ]]; then
        do_ systemctl --user disable --now material3ui-update.timer 2>/dev/null || true
        do_ rm -f "${old[@]}" "$CONFIG_HOME/systemd/user/material3ui-update.service" "$CONFIG_HOME/systemd/user/material3ui-update.timer"
        ok "Old helpers removed (scripting is now: material3ui msg help)"
    fi
    progress_add 2 # the configs

    # ── Session ──
    section "Session"
    local lines=0 svc agent
    if [[ "$OPT_NET_ONLY" == 1 && "$OPT_SERVICES" == 1 ]]; then
        # Last, after every download: the network drops while it changes hands
        for svc in "${NET_RIVALS[@]}"; do
            if do_ sudo systemctl disable --now "$svc.service" >>"$LOG" 2>&1; then ok "$svc off (NetworkManager manages the network)"; else warn "Couldn't turn $svc off"; fi
        done
        lines=1
    fi
    if [[ "$OPT_SERVICES" == 1 ]]; then
        for svc in NetworkManager bluetooth; do
            if systemctl list-unit-files "$svc.service" >/dev/null 2>&1; then
                if do_ sudo systemctl enable --now "$svc.service" >>"$LOG" 2>&1; then ok "$svc on"; else warn "Couldn't enable $svc"; fi
                lines=1
            fi
        done
    fi
    # The shell is the polkit agent, and only one can register per session
    for agent in hyprpolkitagent polkit-gnome-authentication-agent-1 plasma-polkit-agent; do
        if systemctl --user is-enabled --quiet "$agent.service" 2>/dev/null; then
            do_ systemctl --user disable --now "$agent.service"
            ok "$agent off (Material3UI is the polkit agent)"; lines=1
        fi
    done
    if command -v xdg-mime >/dev/null; then
        do_ xdg-mime default "$(desktop_for "$FILE_MANAGER")" inode/directory
        # Pictures open in the shell's own image viewer
        do_ xdg-mime default material3ui-viewer.desktop image/png image/jpeg image/webp image/gif image/bmp image/svg+xml image/tiff image/avif
        do_ xdg-mime default material3ui-player.desktop video/mp4 video/x-matroska video/webm video/quicktime video/x-msvideo video/mpeg video/ogg video/x-flv video/3gpp video/x-ms-wmv video/mp2t
        do_ xdg-settings set default-web-browser "$(desktop_for "$BROWSER")" 2>/dev/null || warn "Couldn't make $BROWSER the default browser"
        if [[ "$FILE_MANAGER" == nemo ]] && command -v gsettings >/dev/null; then
            do_ gsettings set org.cinnamon.desktop.default-applications.terminal exec "$TERMINAL" 2>/dev/null || true
        fi
        ok "Default apps: $([[ "$FILE_MANAGER" == files ]] && echo "Files (built in)" || echo "$FILE_MANAGER"), $BROWSER, the image viewer for pictures"; lines=1
    fi
    if [[ "$GREETER" == 1 ]]; then
        # The material3ui-greeter package brings greetd with it
        if do_ "$REPO/greeter/material3ui-greeter" install --enable >>"$LOG" 2>&1; then
            ok "Login screen (greetd starts it at the next boot)"; lines=1
        else
            warn "Login screen setup failed (see the log); retry with: greeter/material3ui-greeter install --enable"
        fi
    fi
    [[ "$lines" -gt 0 ]] || note "Services, polkit and default apps: nothing to change."
    local state_channel=canary
    [[ "$CHANNEL" == release ]] && state_channel=stable
    if [[ "$DRY" == 0 ]]; then
        mkdir -p "$STATE_DIR"
        cat >"$STATE_FILE" <<EOF
{
  "version": "$VERSION",
  "installed": "$TS",
  "compositor": "niri",
  "niri_mode": "$NIRI_MODE",
  "install_dir": "$REPO",
  "channel": "$state_channel",
  "terminal": "$TERMINAL",
  "file_manager": "$FILE_MANAGER",
  "browser": "$BROWSER"
}
EOF
    fi
    return 0
}

# ── Done ─────────────────────────────────────────────────────────────────────
print_done() { # print_done [bar]: with the finished bar (a plain run)
    section "Done"
    if [[ "${1:-}" == bar && "$DRY" == 0 && -t 1 ]]; then
        PROG_DONE=$PROG_TOTAL
        progress_line 0 0 "${G}installed${R}" "$SECONDS"
        echo
    fi
    if [[ "$DRY" == 1 ]]; then ok "Dry run finished: nothing was changed"; else ok "Material3UI $VERSION is installed"; fi
    if [[ "$CHANGED" -gt 0 && "$DRY" == 0 ]]; then note "$CHANGED config paths changed; ./install.sh --restore undoes them"; fi
    info "Keys    ${A}Super+Space${R} Spotlight · ${A}Super+I${R} Settings · ${A}Super+N${R} Quick Settings · ${A}Super+Return${R} $TERMINAL"
    local yours=()
    [[ "$NIRI_MODE" != include ]] || yours+=("$(tilde "$NIRI/material3ui-user.kdl") for niri tweaks")
    yours+=("${A}material3ui msg help${R} for scripting")
    info "Yours   $(printf '%s · ' "${yours[@]}" | sed 's/ · $//')"
    if pacman -Qq quickshell-git >/dev/null 2>&1; then note "quickshell-git isn't used any more: yay -Rns quickshell-git"; fi
    if [[ -z "${NIRI_SOCKET:-}" ]]; then info "Log into niri to start it."; fi
    note "Log: $(tilde "$LOG")"
}
start_shell() {
    do_ pkill -x material3ui || true
    # niri has read material3ui.kdl by itself already
    do_ niri msg action spawn -- material3ui >/dev/null
    ok "Running"
}

# ═════════════════════════════════════════════════════════════════════════════
#  The full-screen installer
# ═════════════════════════════════════════════════════════════════════════════
# Material 3's dark baseline, the shell's own colours before a wallpaper
# recolours it. On the Linux console its eight slots are repainted with
# them for the run, and put back after
#            role       hex     console slot
readonly M3=(surface    141218  0
             error      f2b8b5  1
             good       a6d189  2
             tertiary   efb8c8  3
             outline    49454f  4
             primary    d0bcff  5
             dim        938f99  6
             text       e6e0e9  7
             container  4a4458  4)
IS_CONSOLE=0
[[ "${TERM:-}" == linux ]] && IS_CONSOLE=1
declare -A M3_HEX=() M3_SLOT=()
for ((i = 0; i < ${#M3[@]}; i += 3)); do M3_HEX[${M3[i]}]=${M3[i + 1]}; M3_SLOT[${M3[i]}]=${M3[i + 2]}; done
tfg() { if [[ "$IS_CONSOLE" == 1 ]]; then printf '\e[3%dm' "${M3_SLOT[$1]}"
        else local h=${M3_HEX[$1]}; printf '\e[38;2;%d;%d;%dm' "0x${h:0:2}" "0x${h:2:2}" "0x${h:4:2}"; fi; }
tbg() { if [[ "$IS_CONSOLE" == 1 ]]; then printf '\e[4%dm' "${M3_SLOT[$1]}"
        else local h=${M3_HEX[$1]}; printf '\e[48;2;%d;%d;%dm' "0x${h:0:2}" "0x${h:2:2}" "0x${h:4:2}"; fi; }
EL=$'\e[K'
setup_theme() {
    P="$(tfg primary)" T="$(tfg tertiary)" E="$(tfg error)" G="$(tfg good)" D="$(tfg dim)" O="$(tfg outline)" X="$(tfg text)"
    A="$P" Y="$T"
    BP="$(tbg primary)" BC="$(tbg container)" ON="$(tfg surface)"
    if [[ "$IS_CONSOLE" == 1 ]]; then
        # The kernel console's font has only CP437-style glyphs
        SYM_DONE="√" SYM_CUR="►" SYM_TODO="·" SYM_BUL="•" SYM_FAIL="x" SYM_ARROW="»" SYM_ON="■" SYM_OFF="·" SYM_DOT="•" SYM_WARN="!"
        BX=(┌ ┐ └ ┘ ─ │) SPIN=('|' '/' '-' '\') BKSP_KEY="bksp"
    else
        SYM_DONE="✓" SYM_CUR="●" SYM_TODO="○" SYM_BUL="•" SYM_FAIL="✗" SYM_ARROW="›" SYM_ON="◉" SYM_OFF="○" SYM_DOT="●" SYM_WARN="!"
        BX=(╭ ╮ ╰ ╯ ─ │) SPIN=(⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏) BKSP_KEY="⌫"
    fi
}
ui_enter() {
    UI_ON=1
    printf '\e[?1049h' >/dev/tty
    if [[ "$IS_CONSOLE" == 1 ]]; then
        local role i
        for role in surface error good tertiary outline primary dim text; do
            i=${M3_SLOT[$role]}
            # The bright slot the same: the console shows bold in it
            printf '\e]P%X%s\e]P%X%s' "$i" "${M3_HEX[$role]}" "$((i + 8))" "${M3_HEX[$role]}" >/dev/tty
        done
    else
        # xterm-like terminals take the surface and text colours for the run
        printf '\e]11;#%s\a\e]10;#%s\a' "${M3_HEX[surface]}" "${M3_HEX[text]}" >/dev/tty
    fi
    cls
    hide_cursor
}
ui_leave() {
    UI_ON=0
    stty echo icanon </dev/tty 2>/dev/null || true
    printf '\e[0m\e[?25h' >/dev/tty 2>/dev/null || true
    if [[ "$IS_CONSOLE" == 1 ]]; then printf '\e]R\e[H\e[2J' >/dev/tty 2>/dev/null || true
    else printf '\e]110\a\e]111\a' >/dev/tty 2>/dev/null || true; fi
    printf '\e[?1049l' >/dev/tty 2>/dev/null || true
}

# ── Drawing ──
COLS=80 ROWS=24
out()  { printf '%s' "$*" >/dev/tty; }
at()   { printf '\e[%d;%dH' "$1" "$2" >/dev/tty; }
cls()  { printf '\e[0m\e[H\e[2J' >/dev/tty; }
vlen() { local s; s="$(printf '%s' "$1" | sed 's/\x1b\[[0-9;]*m//g')"; echo "${#s}"; }
rep()  { local s="" i; for ((i = 0; i < $2; i++)); do s+="$1"; done; printf '%s' "$s"; }
pill() { printf '%s%s%s %s %s' "$2" "${3:-$ON}" "$B" "$1" "$R"; } # pill TEXT BG [FG]
hide_cursor() { printf '\e[?25l' >/dev/tty; }
show_cursor() { printf '\e[?25h' >/dev/tty; }
fit() { # fit TEXT MAX: cut to MAX columns
    local t=$1 m=$2
    if (( ${#t} <= m )); then printf '%s' "$t"; return; fi
    if [[ "$IS_CONSOLE" == 1 ]]; then printf '%s..' "${t:0:m-2}"; else printf '%s…' "${t:0:m-1}"; fi
}

# The steps, under the header: done, this one, still to come
readonly STEPS=(Welcome niri Apps Extras Review Install)
header() { # header STEP
    local cur="$1" line="" name seen=0 gap="  " total=0 right
    for name in "${STEPS[@]}"; do total=$((total + ${#name} + 4)); done
    if (( total > COLS - 4 )); then gap=" "; fi
    at 2 3; out "${P}${B}${SYM_DOT}${R} ${X}${B}Material3UI${R} ${D}${VERSION}${R}"
    right="$(fit "installer · $OS_NAME" $((COLS / 2)))"
    at 2 $((COLS - ${#right} - 2)); out "${D}${right}${R}"
    at 3 3; out "${O}$(rep "${BX[4]}" $((COLS - 4)))${R}"
    for name in "${STEPS[@]}"; do
        if [[ "$name" == "$cur" ]]; then line+="${P}${SYM_CUR}${R} ${X}${B}${name}${R}${gap}"; seen=1
        elif [[ "$seen" == 0 ]]; then line+="${G}${SYM_DONE}${R} ${D}${name}${R}${gap}" # every one, past the last step
        else line+="${O}${SYM_TODO}${R} ${D}${name}${R}${gap}"; fi
    done
    at 4 3; out "$line"
}
footer() { # footer KEY LABEL [KEY LABEL ...]
    local line=""
    while (( $# )); do line+="${P}${B}$1${R} ${D}$2${R}    "; shift 2; done
    out $'\e7'; at "$ROWS" 3; out "$line$EL"; out $'\e8'
}
nav_footer() { footer "↑↓" move "$@" "$BKSP_KEY" back esc quit; }
screen() { # screen STEP "Title" "Subtitle"
    cls; header "$1"
    at 6 3; out "${X}${B}$2${R}"
    if [[ -n "${3:-}" ]]; then at 7 3; out "${D}$(fit "$3" $((COLS - 6)))${R}"; fi
    at 9 1
}
card() { # card ROW "line"...: in an outlined box
    local row=$1 w=$((COLS - 6)) l pad
    shift
    at "$row" 3; out "${O}${BX[0]}$(rep "${BX[4]}" $((w - 2)))${BX[1]}${R}"
    for l in "$@"; do
        row=$((row + 1)); pad=$((w - 4 - $(vlen "$l"))); (( pad >= 0 )) || pad=0
        at "$row" 3; out "${O}${BX[5]}${R} ${l}$(rep " " "$pad") ${O}${BX[5]}${R}"
    done
    row=$((row + 1))
    at "$row" 3; out "${O}${BX[2]}$(rep "${BX[4]}" $((w - 2)))${BX[3]}${R}"
}
err_line() { at 8 3; out "${E}${SYM_FAIL} $1${R}${EL}"; }

# ── Keys ──
# up down left right home end enter esc space tab backspace delete other,
# or "char:X" for a character typed
read_key() {
    local k r1="" r2="" r3=""
    IFS= read -rsn1 k </dev/tty || { echo esc; return; }
    case $k in
        $'\e')
            IFS= read -rsn1 -t 0.05 r1 </dev/tty || { echo esc; return; } # a lone Esc
            if [[ $r1 == "[" || $r1 == "O" ]]; then
                IFS= read -rsn1 -t 0.05 r2 </dev/tty || true
                case $r2 in
                    A) echo up ;; B) echo down ;; C) echo right ;; D) echo left ;;
                    H) echo home ;; F) echo end ;;
                    [0-9])
                        IFS= read -rsn1 -t 0.05 r3 </dev/tty || true
                        case "$r2$r3" in
                            1~|7~) echo home ;; 4~|8~) echo end ;; 3~) echo delete ;;
                            *) while IFS= read -rsn1 -t 0.01 _ </dev/tty; do :; done; echo other ;;
                        esac ;;
                    *) echo other ;;
                esac
            else
                echo other
            fi ;;
        "") echo enter ;;
        " ") echo space ;;
        $'\t') echo tab ;;
        $'\x7f'|$'\b') echo backspace ;;
        *) printf 'char:%s\n' "$k" ;;
    esac
}
cancelled() {
    ui_leave
    echo "Installer closed. Nothing was changed."
    exit 0
}
# An info screen: Enter goes on, Backspace back (2), Esc quits
press_continue() { # press_continue ROW [LABEL] [BACK=1]
    local back=${3:-1}
    at "$1" 3; out "$(pill "${2:-Continue}" "$BP")"
    if [[ "$back" == 1 ]]; then footer enter continue "$BKSP_KEY" back esc quit
    else footer enter continue esc quit; fi
    hide_cursor
    while true; do
        case "$(read_key)" in
            enter) return 0 ;;
            esc) cancelled ;;
            backspace|left) if [[ "$back" == 1 ]]; then return 2; fi ;;
        esac
    done
}

# ── Lists ──
# Items are "name|description|value[|1 when greyed out]"; the choice is
# left in ANSWER. Returns 0 chosen, 2 back; Esc quits
ANSWER=""
_items_parse() { # fills _NAME _DESC _VAL _OFF and _W (the name column)
    _NAME=() _DESC=() _VAL=() _OFF=() _W=0
    local it n d v x i max
    for it in "$@"; do
        IFS='|' read -r n d v x <<<"$it"
        _NAME+=("$n") _DESC+=("$d") _VAL+=("$v") _OFF+=("${x:-0}")
        (( ${#n} <= _W )) || _W=${#n}
    done
    # Descriptions: what fits after "› ◉ name  " on this screen
    max=$((COLS - 4 - 4 - _W - 2 - 1))
    for i in "${!_DESC[@]}"; do _DESC[i]="$(fit "${_DESC[i]}" "$max")"; done
}
tmenu() { # tmenu ROW CURRENT item...
    local row=$1 cur=$2 n i sel=0 k line top=0 h
    shift 2
    _items_parse "$@"
    n=${#_VAL[@]}
    # Scrolls when the list is taller than the room above the footer
    h=$((ROWS - row - 1))
    if (( n > h )); then h=$((h - 1)); fi # a line for "↓ 2 more"
    (( h <= n )) || h=$n; (( h >= 2 )) || h=2
    for i in "${!_VAL[@]}"; do if [[ "${_VAL[i]}" == "$cur" ]]; then sel=$i; fi; done
    hide_cursor
    while true; do
        (( sel >= top )) || top=$sel
        (( sel < top + h )) || top=$((sel - h + 1))
        for ((i = top; i < top + h && i < n; i++)); do
            if (( _OFF[i] )); then
                line="$( (( i == sel )) && printf '%s' "${P}${SYM_ARROW}${R}" || printf ' ')"
                line+=" ${O}$(printf "%-${_W}s" "${_NAME[i]}")  ${_DESC[i]}${R}"
            elif (( i == sel )); then
                line="${P}${B}${SYM_ARROW}${R} ${P}${B}$(printf "%-${_W}s" "${_NAME[i]}")${R}  ${X}${_DESC[i]}${R}"
            else
                line="  ${X}$(printf "%-${_W}s" "${_NAME[i]}")${R}  ${D}${_DESC[i]}${R}"
            fi
            at $((row + i - top)) 3; out "${line}${EL}"
        done
        if (( n > h )); then
            local more=""
            (( top == 0 )) || more+="↑ ${top} more  "
            (( top + h >= n )) || more+="↓ $((n - top - h)) more"
            at $((row + h)) 5; out "${D}${more}${R}${EL}"
        fi
        k="$(read_key)"
        case $k in
            up) sel=$(((sel - 1 + n) % n)) ;;
            down|tab) sel=$(((sel + 1) % n)) ;;
            home) sel=0 ;;
            end) sel=$((n - 1)) ;;
            enter|right)
                if (( _OFF[sel] )); then err_line "${_NAME[sel]}: ${_DESC[sel]}"; continue; fi
                at 8 3; out "$EL"
                ANSWER=${_VAL[sel]}
                return 0 ;;
            backspace|left) return 2 ;;
            esc) cancelled ;;
        esac
    done
}
# Ticks. MIN=1: Enter with none ticked takes the highlighted one. ANSWER:
# the ticked values, in list order, space-separated
checklist() { # checklist ROW "ticked values" MIN item...
    local row=$1 ticked=" $2 " min=$3 n i sel=0 k line box count on=()
    shift 3
    _items_parse "$@"
    n=${#_VAL[@]}
    for i in "${!_VAL[@]}"; do
        if [[ "$ticked" == *" ${_VAL[i]} "* && ${_OFF[i]} -eq 0 ]]; then on[i]=1; else on[i]=0; fi
    done
    local note_row=$((row + n + 1))
    hide_cursor
    while true; do
        for i in "${!_VAL[@]}"; do
            if (( on[i] )); then box="${P}${B}${SYM_ON}${R}"; else box="${O}${SYM_OFF}${R}"; fi
            if (( _OFF[i] )); then
                line="$( (( i == sel )) && printf '%s' "${P}${SYM_ARROW}${R}" || printf ' ')"
                line+=" ${O}${SYM_OFF} $(printf "%-${_W}s" "${_NAME[i]}")  ${_DESC[i]}${R}"
            elif (( i == sel )); then
                line="${P}${B}${SYM_ARROW}${R} ${box} ${P}${B}$(printf "%-${_W}s" "${_NAME[i]}")${R}  ${X}${_DESC[i]}${R}"
            elif (( on[i] )); then
                line="  ${box} ${X}$(printf "%-${_W}s" "${_NAME[i]}")${R}  ${D}${_DESC[i]}${R}"
            else
                line="  ${box} ${D}$(printf "%-${_W}s" "${_NAME[i]}")  ${_DESC[i]}${R}"
            fi
            at $((row + i)) 3; out "${line}${EL}"
        done
        k="$(read_key)"
        case $k in
            up) sel=$(((sel - 1 + n) % n)) ;;
            down|tab) sel=$(((sel + 1) % n)) ;;
            home) sel=0 ;;
            end) sel=$((n - 1)) ;;
            space|char:x)
                if (( _OFF[sel] )); then at "$note_row" 3; out "${E}${SYM_FAIL} ${_NAME[sel]}: ${_DESC[sel]}${R}${EL}"
                else on[sel]=$((1 - on[sel])); fi ;;
            enter|right)
                count=0
                for i in "${!_VAL[@]}"; do (( on[i] == 0 )) || count=$((count + 1)); done
                if (( min > 0 && count == 0 )); then on[sel]=1; fi
                ANSWER=""
                for i in "${!_VAL[@]}"; do (( on[i] == 0 )) || ANSWER+="${_VAL[i]} "; done
                ANSWER=${ANSWER% }
                return 0 ;;
            backspace|left) return 2 ;;
            esc) cancelled ;;
        esac
    done
}
to_items() { # to_items "value:description"... → ITEMS, as the lists take them
    ITEMS=()
    local o
    for o in "$@"; do ITEMS+=("${o%%:*}|${o#*:}|${o%%:*}"); done
}

# ── Pages ──
# Each returns 0 next, 2 back, 3 not shown (passed in the same direction),
# 4 to the first choice again, 5 straight to the review
p_welcome() {
    screen Welcome "" ""
    # MATERIAL in primary, 3UI in tertiary
    at 6 5; out "${P}${B}█▀▄▀█ ▄▀█ ▀█▀ █▀▀ █▀█ █ ▄▀█ █  ${R}  ${T}${B}▀██ █ █ █${R}"
    at 7 5; out "${P}${B}█ ▀ █ █▀█  █  ██▄ █▀▄ █ █▀█ █▄▄${R}  ${T}${B}▄▄█ █▄█ █${R}"
    at 9 5; out "${X}A native Material 3 Expressive shell for niri.${R}"
    local niri_col="$G" lines row dm
    if niri_unavailable; then niri_col="$E"; elif [[ -z "$nv" ]] || niri_outdated; then niri_col="$T"; fi
    lines=(
        "${P}${SYM_BUL}${R} ${X}$(printf '%-19s' "Material3UI $VERSION")${R}${D}built here, installed to /usr with pacman${R}"
        "${P}${SYM_BUL}${R} ${X}$(printf '%-19s' niri)${R}${niri_col}$(fit "$(niri_status)" $((COLS - 34)))${R}"
        "${P}${SYM_BUL}${R} ${X}$(printf '%-19s' 'Your configs')${R}${D}every file replaced is backed up first${R}"
    )
    if [[ ${#OLD_COMPS[@]} -gt 0 ]]; then
        lines+=("${T}${SYM_WARN}${R} ${X}$(printf '%-19s' "$(fit "${OLD_COMPS[*]}" 19)")${R}${D}not supported now: its config stays as it is${R}")
    fi
    card 11 "${lines[@]}"
    row=$((11 + ${#lines[@]} + 2))
    if [[ -n "$CURRENT_DM" ]]; then dm="$(pill "$CURRENT_DM" "$BC" "$X")"; else dm="$(pill "no login manager" "$BC" "$X")"; fi
    at "$row" 5; out "${D}Detected${R}  $(pill "$(fit "$OS_NAME" 30)" "$BP") ${dm}"
    at $((row + 1)) 5; out "${D}Nothing changes until you choose Install now on the review.${R}"
    if [[ -f "$STATE_FILE" ]]; then
        footer "↑↓" move enter select esc quit
        tmenu $((row + 3)) same \
            "Install again|last time's choices: $(last_summary)|same" \
            "Change my choices|go through them, last time's picked|change" || return 6
        [[ "$ANSWER" == same ]] && return 5
        return 0
    fi
    press_continue $((row + 3)) "Get started" 0
}
p_sysupdate() {
    if [[ "$SKIP_DEPS" == 1 ]] || ! niri_outdated; then SYS_UPDATE=0; return 3; fi
    screen niri "Update the system first?" "niri ${nv%-*} is older than $MIN_NIRI, which the shell needs"
    card 9 "${D}Arch doesn't do partial upgrades: the new niri comes with a full${R}" \
           "${D}system update (sudo pacman -Syu), before anything else installs.${R}"
    nav_footer enter select
    tmenu 14 "$([[ "$SYS_UPDATE" == 1 ]] && echo yes || echo no)" \
        "Update first|sudo pacman -Syu, then the rest|yes" \
        "Don't update|the shell won't run on niri ${nv%-*}|no" || return 2
    if [[ "$ANSWER" == yes ]]; then SYS_UPDATE=1; else SYS_UPDATE=0; fi
}
p_niri() {
    local now
    if [[ ! -e "$NIRI/config.kdl" ]]; then now="No config.kdl yet: it starts as niri's own default, with the shell in it"
    elif niri_included; then now="Your config.kdl includes material3ui.kdl already: it stays as it is"
    else now="Your config.kdl is kept (backed up), with one include line at its end"; fi
    screen niri "Your niri config" "How the shell joins ~/.config/niri/config.kdl"
    card 9 "${X}$(fit "$now" $((COLS - 10)))${R}" \
           "${D}material3ui.kdl holds the shell's keys and your apps. Your own${R}" \
           "${D}changes go in material3ui-user.kdl, which updates never replace.${R}"
    nav_footer enter select
    tmenu 15 "${NIRI_MODE:-include}" \
        "Include the shell|material3ui.kdl, included at the end of config.kdl|include" \
        "Leave it alone|include it yourself (docs/NIRI.md)|skip" || return 2
    NIRI_MODE=$ANSWER
}
p_term() {
    screen Apps "Terminal" "Super+Return opens it; Settings → Default apps changes it later"
    nav_footer enter select
    to_items "${TERMINALS[@]}"
    tmenu 9 "$TERMINAL" "${ITEMS[@]}" || return 2
    TERMINAL=$ANSWER
}
p_fm() {
    screen Apps "File manager" "Super+E opens it, and folders open in it"
    nav_footer enter select
    to_items "${FILE_MANAGERS[@]}"
    tmenu 9 "$FILE_MANAGER" "${ITEMS[@]}" || return 2
    FILE_MANAGER=$ANSWER
}
p_browser() {
    screen Apps "Browser" "Super+B opens it, and links open in it"
    nav_footer enter select
    to_items "${BROWSERS[@]}"
    tmenu 9 "$BROWSER" "${ITEMS[@]}" || return 2
    BROWSER=$ANSWER
}
p_extras() {
    local dm="run by greetd" on=""
    case "$CURRENT_DM" in "") ;; greetd) dm="on your greetd" ;; *) dm="replacing $CURRENT_DM" ;; esac
    [[ "$OPT_FONTS" == 0 ]] || on+=" fonts"
    [[ "$OPT_SERVICES" == 0 ]] || on+=" services"
    [[ "$OPT_KDE" == 0 ]] || on+=" kde"
    [[ "$OPT_FISH" == 0 ]] || on+=" fish"
    [[ "$GREETER" != 1 ]] || on+=" greeter"
    screen Extras "Extras" "Space ticks or unticks one; Enter goes on"
    nav_footer space tick enter continue
    checklist 9 "$on" 0 \
        "Fonts|Google Sans Flex, Material Symbols, JetBrains Mono|fonts" \
        "Network & Bluetooth|turn on NetworkManager and Bluetooth|services" \
        "KDE/Qt colours|Material You colours for Qt apps (a Python venv)|kde" \
        "fish|with the starship prompt|fish" \
        "Login screen|Material3UI's greeter, $dm|greeter" || return 2
    on=" $ANSWER "
    if [[ "$on" == *" fonts "* ]]; then OPT_FONTS=1; else OPT_FONTS=0; fi
    if [[ "$on" == *" services "* ]]; then OPT_SERVICES=1; else OPT_SERVICES=0; fi
    if [[ "$on" == *" kde "* ]]; then OPT_KDE=1; else OPT_KDE=0; fi
    if [[ "$on" == *" fish "* ]]; then OPT_FISH=1; else OPT_FISH=0; fi
    if [[ "$on" == *" greeter "* ]]; then GREETER=1; else GREETER=0; fi
}
p_net() {
    [[ "$OPT_SERVICES" == 1 && ${#NET_RIVALS[@]} -gt 0 ]] || return 3
    screen Extras "${NET_RIVALS[*]} also manage the network" "They fight NetworkManager for the Wi-Fi: the connection flips between them"
    nav_footer enter select
    tmenu 9 "$([[ "$OPT_NET_ONLY" == 1 ]] && echo yes || echo no)" \
        "Turn them off|NetworkManager alone (pick your Wi-Fi again if it asks)|yes" \
        "Keep them|the shell may show no connection|no" || return 2
    if [[ "$ANSWER" == yes ]]; then OPT_NET_ONLY=1; else OPT_NET_ONLY=0; fi
}
p_channel() {
    screen Extras "Updates" "What Settings → Software & updates follows"
    nav_footer enter select
    to_items "${CHANNELS[@]}"
    tmenu 9 "$CHANNEL" "${ITEMS[@]}" || return 2
    CHANNEL=$ANSWER
}
p_review() {
    # Only for a niri too old (the page asking may have been passed by)
    if [[ "$SKIP_DEPS" == 1 ]] || ! niri_outdated; then SYS_UPDATE=0; fi
    compute_plan
    screen Review "Ready to install" "Nothing has changed yet; whatever is replaced is backed up first"
    local w=$((COLS - 22)) lines
    lines=(
        "${D}Desktop   ${R}${X}$(fit "niri · $(niri_status) · config: $NIRI_MODE" "$w")${R}"
        "${D}Apps      ${R}${X}$TERMINAL · $FILE_MANAGER · $BROWSER${R}"
        "${D}Extras    ${R}${X}$(fit "$(extras_list)" "$w")${R}"
        "${D}Updates   ${R}${X}$CHANNEL${R}"
    )
    if [[ "$SYS_UPDATE" == 1 ]]; then lines+=("${D}System    ${R}${T}$(fit "sudo pacman -Syu first: niri ${nv%-*} is too old" "$w")${R}"); fi
    lines+=(
        "${D}Packages  ${R}${X}$(fit "$(packages_summary)" "$w")${R}"
        "${D}Shell     ${R}${X}$(fit "build Material3UI $VERSION, install it to /usr" "$w")${R}"
        "${D}Configs   ${R}${X}$(fit "$(configs_summary)" "$w")${R}"
    )
    if [[ -n "$NIRI_RIVALS" ]]; then lines+=("${T}${SYM_WARN} Note    ${R}${X}$(fit "config.kdl also starts ${NIRI_RIVALS% }: take it out" "$w")${R}"); fi
    card 8 "${lines[@]}"
    nav_footer enter select
    local go="Install now|start the installation|install"
    [[ "$DRY" == 0 ]] || go="Show every step|dry run: print what it would do, change nothing|install"
    tmenu $((ROWS - 5)) install \
        "$go" \
        "See every package and file|the full plan, then back here|plan" \
        "Change my choices|back to the first question|change" \
        "Quit|leave without changing anything|quit" || return 2
    case $ANSWER in
        install) return 0 ;;
        plan)
            # Off the full screen to read it, then back
            ui_leave
            if command -v less >/dev/null; then print_plan 2>&1 | less -R || true
            else print_plan; printf '\n  %sPress a key to go back%s' "$D" "$R"; read -rsn1 _ </dev/tty || true; fi
            ui_enter
            return 6 ;;
        change) return 4 ;;
        *) cancelled ;;
    esac
}

readonly PAGES=(p_welcome p_sysupdate p_niri p_term p_fm p_browser p_extras p_net p_channel p_review)
tui_decisions() {
    # Last time's answers, else the first of each, as the starting picks
    apply_previous
    previous_channel
    TERMINAL="${TERMINAL:-${TERMINALS[0]%%:*}}" FILE_MANAGER="${FILE_MANAGER:-${FILE_MANAGERS[0]%%:*}}"
    BROWSER="${BROWSER:-${BROWSERS[0]%%:*}}" CHANNEL="${CHANNEL:-${CHANNELS[0]%%:*}}" NIRI_MODE="${NIRI_MODE:-include}"
    GREETER="${GREETER:-0}"
    local i=0 dir=1 rc first=1 review=$((${#PAGES[@]} - 1))
    while (( i < ${#PAGES[@]} )); do
        if "${PAGES[i]}"; then rc=0; else rc=$?; fi
        case $rc in
            0) dir=1; i=$((i + 1)) ;;
            2) dir=-1; i=$((i - 1)) ;;
            3) i=$((i + dir)) ;;
            4) dir=1; i=$first ;;
            5) dir=1; i=$review ;;
            6) ;; # the same page again
        esac
        if (( i < 0 )); then i=0; dir=1; fi
    done
    return 0
}

# ── The install, on screen ──
readonly RUN_STEPS=(Packages "The shell" Configs Session)
log_tail() { # the last N readable log lines, cut to the screen
    { tail -c 6000 "$LOG" 2>/dev/null | tr '\r' '\n' | sed 's/\x1b\[[0-9;?]*[A-Za-z]//g' |
        grep -v -e '^[[:space:]]*$' -e '^── ' | tail -n "$1" | cut -c1-"$((COLS - 10))" |
        if [[ "$IS_CONSOLE" == 1 ]]; then sed 's/✓/√/g; s/✗/x/g; s/—/-/g; s/…/.../g'; else cat; fi; } || true
}
elapsed() { printf '%d:%02d' $(($1 / 60)) $(($1 % 60)); }
render_install() { # render_install PID
    local pid=$1 frame=0 pdone=0 ptotal=1 weight=0 sect="" label="" i row line running cur_idx nlog lrow start=$SECONDS
    local w=$((COLS - 22))
    hide_cursor
    stty -echo -icanon </dev/tty 2>/dev/null || true # keys pressed now must not print
    screen Install "Installing Material3UI" "This takes a few minutes. Keep this terminal open"
    footer "$SYM_BUL" "installing; keys do nothing until it's done"
    while true; do
        if [[ -f "$STATE" ]]; then IFS=$'\t' read -r pdone ptotal weight sect label <"$STATE" || true; fi
        running=0
        if kill -0 "$pid" 2>/dev/null; then running=1; fi
        cur_idx=-1
        for i in "${!RUN_STEPS[@]}"; do if [[ "${RUN_STEPS[i]}" == "$sect" ]]; then cur_idx=$i; fi; done
        if [[ "$sect" == Done ]]; then cur_idx=${#RUN_STEPS[@]}; fi
        row=9
        for i in "${!RUN_STEPS[@]}"; do
            if (( i < cur_idx )); then line="${G}${SYM_DONE}${R}  ${D}${RUN_STEPS[i]}${R}"
            elif (( i == cur_idx )); then
                if (( running )); then line="${P}${SPIN[frame % ${#SPIN[@]}]}${R}  ${X}${B}$(printf '%-11s' "${RUN_STEPS[i]}")${R}${D}$(fit "$label" $((COLS - 24)))${R}"
                else line="${E}${SYM_FAIL}${R}  ${E}$(printf '%-11s' "${RUN_STEPS[i]}")$(fit "$label" $((COLS - 24)))${R}"; fi
            else line="${O}${SYM_TODO}${R}  ${D}${RUN_STEPS[i]}${R}"; fi
            at "$row" 5; out "${line}${EL}"
            row=$((row + 1))
        done
        # Material 3 Expressive's wavy bar, the running step's share moving
        PROG_DONE=${pdone:-0} PROG_TOTAL=${ptotal:-1}
        at $((row + 1)) 5
        out "$(BAR_W=$w bar "$frame" "${weight:-0}") ${X}${B}$(printf '%3d%%' "$(percent)")${R}  ${D}$(elapsed $((SECONDS - start)))${R}${EL}"
        lrow=$((row + 3))
        nlog=$((ROWS - 1 - lrow)); (( nlog <= 6 )) || nlog=6
        if (( nlog > 0 )); then
            while IFS= read -r line; do
                at "$lrow" 5; out "${D}${line}${R}${EL}"
                lrow=$((lrow + 1))
            done < <(log_tail "$nlog")
        fi
        (( running )) || break
        frame=$((frame + 1))
        sleep 0.1
    done
    # What was typed meanwhile is dropped
    while IFS= read -rsn1 -t 0.01 _ </dev/tty; do :; done
    stty echo icanon </dev/tty 2>/dev/null || true
}
scr_failed() {
    screen Install "" ""
    at 6 3; out "${E}${B}${SYM_FAIL} The install stopped${R}"
    at 7 3; out "${D}$(fit "The steps after it didn't run; ./install.sh --restore undoes the configs" $((COLS - 6)))${R}"
    local lines=() l
    if [[ -s "$FAIL_FILE" ]]; then
        while IFS= read -r l; do lines+=("${D}$(fit "${l#    }" $((COLS - 12)))${R}"); done < <(head -n $((ROWS - 16)) "$FAIL_FILE")
    else
        while IFS= read -r l; do lines+=("${D}${l}${R}"); done < <(log_tail $((ROWS - 16)))
    fi
    card 9 "${X}What went wrong${R}" "" "${lines[@]}"
    at $((ROWS - 3)) 5; out "${D}Full log: ${X}$(tilde "$LOG")${R}"
    press_continue $((ROWS - 2)) Exit 0
}
DONE_ACTION=exit
scr_done() {
    local login row k1 k2 changed=0 n
    [[ ! -s "$RUN_DIR/changed" ]] || changed="$(cat "$RUN_DIR/changed")"
    if [[ -n "${NIRI_SOCKET:-}" ]]; then login="niri is running: start the new shell below, or log out and back in"
    elif [[ "$GREETER" == 1 ]]; then login="Reboot: the login screen starts, with niri to pick"
    elif [[ -n "$CURRENT_DM" ]]; then login="Log out and pick ${P}niri${R}${X} in $CURRENT_DM"
    else login="Log in on a TTY and run ${P}niri-session${R}"; fi
    cls; header __done__ # a name that isn't a step: every one done
    at 6 3; out "${G}${B}${SYM_DONE} Material3UI $VERSION is installed${R}"
    at 7 3; out "${X}${login}${R}"
    keyrow() { # keyrow KEY LABEL KEY LABEL: two aligned columns
        local a
        a="$(pill "$1" "$BC" "$X") ${X}$2${R}"
        printf '%s%s%s' "$a" "$(rep " " $((34 - $(vlen "$a"))))" "$(pill "$3" "$BC" "$X") ${X}$4${R}"
    }
    k1="$(keyrow "Super+Space" Spotlight "Super+I" Settings)"
    k2="$(keyrow "Super+N" "Quick Settings" "Super+Return" "$TERMINAL")"
    local undo=()
    [[ "$changed" == 0 ]] || undo=("${D}$changed config paths changed; ./install.sh --restore undoes them${R}")
    card 9 "$k1" "$k2" "" "${D}Yours: $(tilde "$NIRI/material3ui-user.kdl") · material3ui msg help${R}" "${undo[@]}"
    row=17
    if [[ -s "$WARN_FILE" ]]; then
        at "$row" 3; out "${T}${B}${SYM_WARN} Notes${R}"
        row=$((row + 1))
        n=0
        while IFS= read -r l && (( row < ROWS - 4 )); do
            at "$row" 5; out "${D}$(fit "$l" $((COLS - 8)))${R}"
            row=$((row + 1)) n=$((n + 1))
        done <"$WARN_FILE"
    fi
    row=$((ROWS - 3))
    if [[ -n "${NIRI_SOCKET:-}" ]]; then
        footer "↑↓" move enter select
        tmenu "$row" start "Start the shell now|the new build, in this niri session|start" "Exit|start it later yourself|exit" || ANSWER=exit
        DONE_ACTION=$ANSWER
    elif [[ "$GREETER" == 1 ]]; then
        footer "↑↓" move enter select
        tmenu "$row" exit "Reboot now|to the login screen|reboot" "Exit|reboot later yourself|exit" || ANSWER=exit
        DONE_ACTION=$ANSWER
    else
        press_continue $((ROWS - 2)) Exit 0
    fi
    return 0
}

# Full screen when there's a terminal big enough for it, else plain prompts
choose_ui() {
    [[ "$YES" == 0 && "$PLAIN" == 0 && "$KEYS" == 1 && "${TERM:-dumb}" != dumb ]] || return 0
    read -r ROWS COLS < <(stty size </dev/tty 2>/dev/null || echo "24 80")
    if (( ROWS < 24 || COLS < 76 )); then
        note "The terminal is ${COLS}×${ROWS}; the full-screen installer needs 76×24: plain prompts instead."
        return 0
    fi
    (( COLS <= 100 )) || COLS=100
    # bash counts the screen's characters right only in a UTF-8 locale
    if [[ "$(locale charmap 2>/dev/null)" != UTF-8 ]] && locale -a 2>/dev/null | grep -qi '^c\.utf-\?8$'; then export LC_ALL=C.UTF-8; fi
    UI=tui
    setup_theme
}

# ═════════════════════════════════════════════════════════════════════════════
printf '\n%s%s  ●  Material3UI%s %s· a native Material 3 shell for niri%s\n' "$P" "$B" "$R" "$D" "$R"
[[ "$DRY" == 1 ]] && printf '  %sDry run: nothing will change.%s\n' "$Y" "$R"
locate_repo
mkdir -p "$LOG_DIR"
: >"$LOG"
case "$MODE" in
    restore) restore_mode; exit 0 ;;
    uninstall) uninstall_mode; exit 0 ;;
esac

[[ "$EUID" -ne 0 ]] || fail "Run as your normal user, not root; sudo is asked for when needed."
command -v pacman >/dev/null || fail "pacman not found: Material3UI supports Arch-based systems only."
validate_flags
detect
RUN_DIR="$(mktemp -d)"
choose_ui
[[ "$UI" == tui ]] || print_checks
ensure_jq

if [[ "$UI" == plain ]]; then
    plain_decisions
    compute_plan
    print_plan
    echo
    confirm "Go ahead?" Y || { info "Nothing changed."; exit 0; }
    compute_weights
    run_install
    print_done bar
    if [[ "$DRY" == 0 && -n "${NIRI_SOCKET:-}" ]] && confirm "Start the shell now?" Y; then start_shell; fi
    exit 0
fi

ui_enter
tui_decisions
compute_weights
if [[ "$DRY" == 1 ]]; then
    # A dry run prints every step in plain view
    ui_leave
    UI=plain
    print_plan
    run_install
    print_done
    exit 0
fi
# The password now, in the open: nothing may ask behind the full screen
if ! sudo -n true 2>/dev/null; then
    ui_leave
    printf '\n  %sMaterial3UI needs your password for the packages and to install the shell.%s\n' "$X" "$R"
    sudo -v || fail "sudo is needed for packages and for installing the shell"
    ui_enter
fi
sudo_keepalive
STATE="$RUN_DIR/state" WARN_FILE="$RUN_DIR/warnings" FAIL_FILE="$RUN_DIR/failure"
: >"$WARN_FILE"
trap '' INT # Ctrl+C can't stop pacman halfway
# The steps in the background, plain text into the log; the screen draws
# where they stand
(
    UI_ON=0
    B="" D="" R="" P="" A="" G="" Y="" E="" X="" O="" T=""
    run_install
    printf '%s' "$CHANGED" >"$RUN_DIR/changed"
    PROG_DONE=$PROG_TOTAL CUR_SECTION=Done
    state_put 0 ""
) >>"$LOG" 2>&1 &
INSTALL_PID=$!
render_install "$INSTALL_PID"
rc=0
wait "$INSTALL_PID" || rc=$?
trap - INT
if [[ "$rc" -ne 0 ]]; then
    scr_failed
    ui_leave
    printf '\n  %s✗ The install stopped.%s\n' "$E$B" "$R"
    if [[ -s "$FAIL_FILE" ]]; then cat "$FAIL_FILE"; fi
    printf '  %sLog: %s%s\n' "$D" "$LOG" "$R"
    exit 1
fi
scr_done
ui_leave
UI=plain
[[ ! -s "$RUN_DIR/changed" ]] || CHANGED="$(cat "$RUN_DIR/changed")"
print_done
if [[ -s "$WARN_FILE" ]]; then
    printf '  %sNotes:%s\n' "$Y" "$R"
    sed 's/^/    /' "$WARN_FILE"
fi
case "$DONE_ACTION" in
    start) start_shell ;;
    reboot) systemctl reboot ;;
esac
