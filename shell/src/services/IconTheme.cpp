#include "services/IconTheme.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "ui/Theme.h"

#include <cairo-svg.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace m3::icontheme {

namespace {

// Bumped when the drawing or the table changes, so it's drawn again
constexpr int kVersion = 1;

enum Tone { Primary, Secondary, Tertiary, Neutral, Error };

struct Def {
    const char* context; // the theme's folder: places, mimetypes, devices, apps
    const char* glyph;   // a Material Symbol
    Tone tone;
    std::vector<const char*> names; // the freedesktop names it answers to
};

// What it draws. Places in the primary tone; file types by what they hold;
// apps by what they do
const std::vector<Def>& table() {
    static const std::vector<Def> defs = {
        // Places
        {"places", "folder", Primary, {"folder", "inode-directory", "folder-new", "folder-saved-search", "folder-visiting"}},
        {"places", "folder_open", Primary, {"folder-open", "folder-drag-accept"}},
        {"places", "home", Primary, {"user-home", "folder-home", "go-home"}},
        {"places", "desktop_windows", Primary, {"user-desktop", "folder-desktop"}},
        {"places", "description", Primary, {"folder-documents", "folder-text"}},
        {"places", "download", Primary, {"folder-download", "folder-downloads"}},
        {"places", "music_note", Primary, {"folder-music", "folder-sound"}},
        {"places", "image", Primary, {"folder-pictures", "folder-images", "folder-photos"}},
        {"places", "movie", Primary, {"folder-videos", "folder-video"}},
        {"places", "public", Primary, {"folder-publicshare", "folder-public"}},
        {"places", "draft", Primary, {"folder-templates"}},
        {"places", "code", Primary, {"folder-development", "folder-code", "folder-projects"}},
        {"places", "cloud", Primary, {"folder-remote", "folder-cloud", "network-server"}},
        {"places", "lan", Primary, {"network-workgroup", "network-wired"}},
        {"places", "delete", Secondary, {"user-trash"}},
        {"places", "delete_forever", Tertiary, {"user-trash-full"}},
        {"places", "bookmark", Primary, {"user-bookmarks"}},
        {"places", "lock", Primary, {"folder-locked"}},
        // Drives and devices
        {"devices", "hard_drive", Secondary, {"drive-harddisk", "drive-harddisk-system", "drive-harddisk-solidstate", "drive-multidisk"}},
        {"devices", "usb", Secondary, {"drive-removable-media", "drive-removable-media-usb", "media-removable"}},
        {"devices", "sd_card", Secondary, {"media-flash", "media-flash-sd", "media-flash-memory-stick"}},
        {"devices", "album", Secondary, {"media-optical", "drive-optical", "media-optical-cd", "media-optical-dvd"}},
        {"devices", "computer", Secondary, {"computer", "computer-laptop", "laptop"}},
        {"devices", "smartphone", Secondary, {"phone", "smartphone", "multimedia-player"}},
        {"devices", "print", Secondary, {"printer", "printer-network"}},
        {"devices", "photo_camera", Secondary, {"camera-photo", "camera"}},
        {"devices", "videocam", Secondary, {"camera-web", "camera-video"}},
        {"devices", "mouse", Secondary, {"input-mouse"}},
        {"devices", "keyboard", Secondary, {"input-keyboard"}},
        {"devices", "headphones", Secondary, {"audio-headphones", "audio-headset"}},
        {"devices", "speaker", Secondary, {"audio-speakers", "audio-card"}},
        {"devices", "mic", Secondary, {"audio-input-microphone"}},
        {"devices", "sports_esports", Secondary, {"input-gaming"}},
        {"devices", "wifi", Secondary, {"network-wireless"}},
        {"devices", "bluetooth", Secondary, {"bluetooth", "preferences-bluetooth"}},
        {"devices", "battery_full", Secondary, {"battery"}},
        {"devices", "monitor", Secondary, {"video-display", "display"}},
        // File types
        {"mimetypes", "article", Primary, {"text-x-generic", "text-plain", "x-office-document", "application-rtf", "application-vnd.oasis.opendocument.text", "application-msword",
                                           "application-vnd.openxmlformats-officedocument.wordprocessingml.document", "text-x-generic-template"}},
        {"mimetypes", "table", Secondary, {"x-office-spreadsheet", "application-vnd.oasis.opendocument.spreadsheet", "application-vnd.ms-excel", "text-csv",
                                           "application-vnd.openxmlformats-officedocument.spreadsheetml.sheet"}},
        {"mimetypes", "slideshow", Tertiary, {"x-office-presentation", "application-vnd.oasis.opendocument.presentation", "application-vnd.ms-powerpoint",
                                              "application-vnd.openxmlformats-officedocument.presentationml.presentation"}},
        {"mimetypes", "picture_as_pdf", Error, {"application-pdf", "application-x-pdf"}},
        {"mimetypes", "image", Tertiary, {"image-x-generic", "image-png", "image-jpeg", "image-gif", "image-webp", "image-svg+xml", "image-bmp", "image-tiff", "image-avif", "image-heif",
                                          "x-office-drawing", "application-vnd.oasis.opendocument.graphics"}},
        {"mimetypes", "movie", Secondary, {"video-x-generic", "video-mp4", "video-x-matroska", "video-webm", "video-quicktime", "video-x-msvideo"}},
        {"mimetypes", "music_note", Secondary, {"audio-x-generic", "audio-mpeg", "audio-flac", "audio-ogg", "audio-x-wav", "audio-mp4", "audio-x-vorbis+ogg"}},
        {"mimetypes", "folder_zip", Tertiary, {"package-x-generic", "application-zip", "application-x-tar", "application-x-compressed-tar", "application-x-7z-compressed",
                                               "application-x-rar", "application-vnd.rar", "application-x-archive", "application-gzip", "application-x-bzip", "application-x-xz",
                                               "application-x-zstd-compressed-tar", "application-x-xz-compressed-tar", "application-zstd"}},
        {"mimetypes", "deployed_code", Tertiary, {"application-x-deb", "application-x-rpm", "application-vnd.flatpak", "application-x-alpm-package", "application-vnd.appimage"}},
        {"mimetypes", "album", Tertiary, {"application-x-cd-image", "application-x-iso9660-image", "application-vnd.efi.iso"}},
        {"mimetypes", "terminal", Neutral, {"text-x-script", "application-x-shellscript", "text-x-sh", "application-x-executable", "application-x-sharedlib-executable"}},
        {"mimetypes", "code", Primary, {"text-x-python", "text-x-python3", "text-x-csrc", "text-x-chdr", "text-x-c++src", "text-x-c++hdr", "text-x-java", "text-x-rust", "text-x-go",
                                        "application-javascript", "text-javascript", "application-x-php", "text-x-ruby", "text-x-lua", "text-x-makefile", "text-x-cmake",
                                        "application-x-perl", "text-x-kotlin", "text-x-typescript", "application-typescript", "text-x-meson", "text-x-patch", "text-x-sql"}},
        {"mimetypes", "data_object", Primary, {"application-json", "application-x-yaml", "application-toml", "application-xml", "text-xml", "application-x-ini", "text-x-ini"}},
        {"mimetypes", "html", Secondary, {"text-html", "application-xhtml+xml"}},
        {"mimetypes", "css", Secondary, {"text-css"}},
        {"mimetypes", "markdown", Primary, {"text-markdown", "text-x-markdown"}},
        {"mimetypes", "receipt_long", Neutral, {"text-x-log"}},
        {"mimetypes", "font_download", Secondary, {"font-x-generic", "application-x-font-ttf", "font-ttf", "font-otf", "application-vnd.ms-opentype", "font-woff", "font-woff2"}},
        {"mimetypes", "extension", Neutral, {"application-x-sharedlib", "application-x-object"}},
        {"mimetypes", "menu_book", Tertiary, {"application-epub+zip", "application-x-mobipocket-ebook"}},
        {"mimetypes", "event", Primary, {"text-calendar", "text-x-vcalendar"}},
        {"mimetypes", "contact_page", Primary, {"text-vcard", "text-x-vcard", "text-directory"}},
        {"mimetypes", "downloading", Secondary, {"application-x-bittorrent"}},
        {"mimetypes", "apps", Neutral, {"application-x-desktop"}},
        {"mimetypes", "key", Neutral, {"application-pgp-keys", "application-x-pem-file", "application-pkcs12"}},
        {"mimetypes", "draft", Neutral, {"application-octet-stream", "unknown", "application-x-generic", "empty"}},
        // Apps: what they do
        {"apps", "public", Primary, {"firefox", "org.mozilla.firefox", "firefox-esr", "chromium", "google-chrome", "brave-browser", "com.brave.Browser", "zen-browser",
                                     "app.zen_browser.zen", "librewolf", "io.gitlab.librewolf-community", "vivaldi", "microsoft-edge", "opera", "epiphany", "org.gnome.Epiphany",
                                     "falkon", "qutebrowser", "web-browser", "internet-web-browser", "helium"}},
        {"apps", "code", Tertiary, {"code", "code-oss", "visual-studio-code", "com.visualstudio.code", "vscodium", "com.vscodium.codium", "cursor", "zed", "dev.zed.Zed",
                                    "jetbrains-idea", "intellij-idea-community", "pycharm", "clion", "android-studio", "sublime-text", "geany", "lapce", "neovide", "nvim", "vim", "gvim"}},
        {"apps", "terminal", Neutral, {"utilities-terminal", "Alacritty", "alacritty", "kitty", "foot", "footclient", "org.wezfurlong.wezterm", "com.mitchellh.ghostty", "ghostty",
                                       "org.gnome.Console", "org.gnome.Terminal", "gnome-terminal", "konsole", "org.kde.konsole", "xterm", "terminator", "tilix",
                                       "com.gexperts.Tilix", "xfce4-terminal", "org.xfce.terminal", "st", "rio", "material3ui-terminal"}},
        {"apps", "folder", Primary, {"system-file-manager", "org.gnome.Nautilus", "nautilus", "thunar", "org.xfce.thunar", "nemo", "dolphin", "org.kde.dolphin", "pcmanfm",
                                     "pcmanfm-qt", "caja", "yazi", "ranger", "material3ui-files"}},
        {"apps", "forum", Secondary, {"discord", "vesktop", "dev.vencord.Vesktop", "webcord", "telegram", "org.telegram.desktop", "telegram-desktop", "signal-desktop",
                                      "org.signal.Signal", "element-desktop", "im.riot.Riot", "slack", "com.slack.Slack", "whatsapp", "teams", "fractal", "org.gnome.Fractal"}},
        {"apps", "library_music", Tertiary, {"spotify", "spotify-client", "com.spotify.Client", "rhythmbox", "org.gnome.Rhythmbox3", "elisa", "org.kde.elisa", "lollypop",
                                             "amberol", "io.bassi.Amberol", "strawberry", "audacious", "clementine", "cider", "youtube-music", "multimedia-audio-player"}},
        {"apps", "play_circle", Secondary, {"vlc", "mpv", "io.mpv.Mpv", "celluloid", "io.github.celluloid_player.Celluloid", "totem", "org.gnome.Totem", "haruna",
                                            "org.kde.haruna", "smplayer", "showtime", "org.gnome.Showtime", "multimedia-video-player", "kodi", "stremio", "jellyfin-media-player"}},
        {"apps", "brush", Tertiary, {"gimp", "org.gimp.GIMP", "krita", "org.kde.krita", "pinta", "mypaint", "drawing", "com.github.maoschanz.drawing"}},
        {"apps", "draw", Tertiary, {"inkscape", "org.inkscape.Inkscape", "libreoffice-draw"}},
        {"apps", "radio_button_checked", Error, {"obs", "com.obsproject.Studio", "simplescreenrecorder", "kooha", "io.github.seadve.Kooha", "gpu-screen-recorder"}},
        {"apps", "sports_esports", Secondary, {"steam", "com.valvesoftware.Steam", "lutris", "net.lutris.Lutris", "heroic", "com.heroicgameslauncher.hgl", "bottles",
                                               "com.usebottles.bottles", "prismlauncher", "org.prismlauncher.PrismLauncher", "minecraft-launcher", "retroarch", "protonup-qt"}},
        {"apps", "article", Primary, {"libreoffice-writer", "libreoffice-startcenter", "libreoffice", "onlyoffice-desktopeditors", "org.onlyoffice.desktopeditors", "abiword",
                                      "wps-office2019-wpsmain", "x-office-document-template"}},
        {"apps", "table", Secondary, {"libreoffice-calc", "gnumeric"}},
        {"apps", "slideshow", Tertiary, {"libreoffice-impress"}},
        {"apps", "mail", Primary, {"thunderbird", "org.mozilla.Thunderbird", "net.thunderbird.Thunderbird", "evolution", "org.gnome.Evolution", "geary", "org.gnome.Geary",
                                   "betterbird", "kmail", "mail-client", "internet-mail"}},
        {"apps", "view_in_ar", Tertiary, {"blender", "org.blender.Blender", "freecad", "org.freecad.FreeCAD", "godot", "org.godotengine.Godot"}},
        {"apps", "downloading", Secondary, {"qbittorrent", "org.qbittorrent.qBittorrent", "transmission", "transmission-gtk", "transmission-qt", "com.transmissionbt.Transmission",
                                            "deluge", "fragments", "de.haeckerfelix.Fragments", "motrix"}},
        {"apps", "tune", Neutral, {"pavucontrol", "org.pulseaudio.pavucontrol", "multimedia-volume-control", "pwvucontrol", "com.saivert.pwvucontrol", "helvum",
                                   "qpwgraph", "easyeffects", "com.github.wwmm.easyeffects"}},
        {"apps", "lan", Primary, {"nm-connection-editor", "preferences-system-network", "network-manager"}},
        {"apps", "bluetooth", Primary, {"blueman", "blueman-manager", "blueberry", "overskride"}},
        {"apps", "settings", Neutral, {"preferences-system", "org.gnome.Settings", "gnome-control-center", "systemsettings", "preferences-desktop", "nwg-look",
                                       "qt5ct", "qt6ct", "lxappearance", "dconf-editor", "ca.desrt.dconf-editor", "material3ui-settings"}},
        {"apps", "calculate", Secondary, {"accessories-calculator", "org.gnome.Calculator", "gnome-calculator", "kcalc", "org.kde.kcalc", "qalculate-gtk", "galculator"}},
        {"apps", "edit_note", Primary, {"accessories-text-editor", "org.gnome.TextEditor", "gnome-text-editor", "gedit", "org.gnome.gedit", "kate", "org.kde.kate", "kwrite",
                                        "mousepad", "org.xfce.mousepad", "featherpad", "xed"}},
        {"apps", "phonelink", Secondary, {"kdeconnect", "org.kde.kdeconnect.app", "org.kde.kdeconnect-settings", "org.kde.kdeconnect", "kdeconnect-indicator", "gsconnect",
                                          "org.gnome.Shell.Extensions.GSConnect", "material3ui-quickshare", "scrcpy"}},
        {"apps", "note_stack", Tertiary, {"obsidian", "md.obsidian.Obsidian", "logseq", "joplin", "notion-app", "standard-notes", "org.gnome.gitlab.cheywood.Folio", "xournalpp",
                                          "com.github.xournalpp.xournalpp"}},
        {"apps", "monitoring", Secondary, {"utilities-system-monitor", "org.gnome.SystemMonitor", "gnome-system-monitor", "htop", "btop", "plasma-systemmonitor",
                                           "io.missioncenter.MissionCenter", "missioncenter", "resources", "net.nokyan.Resources", "material3ui-tasks"}},
        {"apps", "schedule", Primary, {"org.gnome.clocks", "gnome-clocks", "kclock"}},
        {"apps", "calendar_month", Primary, {"org.gnome.Calendar", "gnome-calendar", "korganizer", "x-office-calendar"}},
        {"apps", "photo_camera", Tertiary, {"org.gnome.Snapshot", "cheese", "org.gnome.Cheese", "kamoso", "guvcview"}},
        {"apps", "image", Tertiary, {"org.gnome.Loupe", "loupe", "eog", "org.gnome.eog", "gwenview", "org.kde.gwenview", "ristretto", "org.xfce.ristretto", "feh", "imv",
                                     "nomacs", "qimgv", "gthumb", "shotwell", "org.gnome.Shotwell", "darktable", "rawtherapee", "digikam", "material3ui-viewer"}},
        {"apps", "videocam", Primary, {"zoom", "Zoom", "us.zoom.Zoom", "skypeforlinux", "jitsi-meet"}},
        {"apps", "computer", Neutral, {"virt-manager", "org.virt_manager.virt-manager", "virtualbox", "gnome-boxes", "org.gnome.Boxes", "remmina", "org.remmina.Remmina", "quickemu"}},
        {"apps", "help", Neutral, {"help-browser", "yelp", "org.gnome.Yelp", "help-contents"}},
        {"apps", "storefront", Secondary, {"org.gnome.Software", "gnome-software", "system-software-install", "pamac", "pamac-manager", "octopi", "plasmadiscover",
                                           "org.kde.discover", "bauh", "com.github.tchx84.Flatseal", "flatseal", "warehouse", "io.github.flattool.Warehouse"}},
        {"apps", "screenshot_region", Tertiary, {"flameshot", "org.flameshot.Flameshot", "spectacle", "org.kde.spectacle", "gnome-screenshot", "org.gnome.Screenshot",
                                                 "ksnip", "swappy", "satty"}},
        {"apps", "key", Neutral, {"keepassxc", "org.keepassxc.KeePassXC", "bitwarden", "com.bitwarden.desktop", "1password", "seahorse", "org.gnome.seahorse.Application",
                                  "proton-pass", "kwalletmanager"}},
        {"apps", "picture_as_pdf", Error, {"evince", "org.gnome.Evince", "okular", "org.kde.okular", "zathura", "org.pwmt.zathura", "papers", "org.gnome.Papers", "atril",
                                           "mupdf", "xreader"}},
        {"apps", "folder_zip", Tertiary, {"file-roller", "org.gnome.FileRoller", "ark", "org.kde.ark", "xarchiver", "engrampa", "peazip"}},
        {"apps", "album", Secondary, {"brasero", "k3b", "org.kde.k3b", "gnome-disks", "org.gnome.DiskUtility", "gparted", "partitionmanager", "org.kde.partitionmanager",
                                      "balena-etcher", "impression", "io.gitlab.adhami3310.Impression", "fedora-media-writer"}},
        {"apps", "cloud", Primary, {"nextcloud", "com.nextcloud.desktopclient.nextcloud", "dropbox", "insync", "megasync", "syncthing", "syncthing-gtk", "rclone-browser"}},
        {"apps", "translate", Primary, {"dialect", "app.drey.Dialect", "crow-translate"}},
        {"apps", "weather_mix", Primary, {"org.gnome.Weather", "gnome-weather"}},
        {"apps", "map", Secondary, {"org.gnome.Maps", "gnome-maps", "marble"}},
        {"apps", "podcasts", Tertiary, {"org.gnome.Podcasts", "gpodder", "kasts"}},
        {"apps", "rss_feed", Secondary, {"newsflash", "io.gitlab.news_flash.NewsFlash", "liferea", "akregator", "fluent-reader"}},
        {"apps", "mic", Error, {"audacity", "org.audacityteam.Audacity", "gnome-sound-recorder", "org.gnome.SoundRecorder", "tenacity", "ardour"}},
        {"apps", "movie_edit", Tertiary, {"kdenlive", "org.kde.kdenlive", "shotcut", "org.shotcut.Shotcut", "openshot", "org.openshot.OpenShot", "pitivi", "davinci-resolve",
                                          "flowblade", "handbrake", "fr.handbrake.ghb", "losslesscut"}},
        {"apps", "font_download", Secondary, {"gnome-font-viewer", "org.gnome.font-viewer", "fontforge", "font-manager"}},
        {"apps", "palette", Tertiary, {"gpick", "hyprpicker", "org.gnome.design.Palette", "gcolor3", "nl.hjdskes.gcolor3"}},
        {"apps", "smart_toy", Primary, {"claude", "claude-desktop", "chatgpt", "ollama", "alpaca", "com.jeffser.Alpaca", "jan", "lmstudio"}},
        {"apps", "account_tree", Neutral, {"github-desktop", "io.github.shiftey.Desktop", "gitg", "org.gnome.gitg", "gitkraken", "sublime-merge", "lazygit"}},
        {"apps", "database", Neutral, {"dbeaver", "io.dbeaver.DBeaverCommunity", "beekeeper-studio", "sqlitebrowser", "pgadmin4"}},
        {"apps", "api", Neutral, {"postman", "insomnia", "bruno"}},
        {"apps", "power_settings_new", Error, {"system-shutdown", "system-log-out", "system-reboot", "wlogout"}},
        {"apps", "lock", Neutral, {"system-lock-screen", "hyprlock", "swaylock"}},
        {"apps", "search", Primary, {"system-search", "catfish", "org.gnome.Characters", "gnome-characters"}},
        {"apps", "local_shipping", Secondary, {"system-software-update", "software-update-available", "topgrade"}},
        {"apps", "shield", Primary, {"gufw", "firewall-config", "clamtk", "protonvpn", "proton-vpn-gtk-app", "mullvad-vpn", "nordvpn"}},
    };
    return defs;
}

std::string dataHome() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return x && *x ? x : paths::home() + "/.local/share";
}
std::string themeDir() {
    return dataHome() + "/icons/" + kName;
}

std::string hex(const Color& c) {
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
}

std::pair<Color, Color> colours(Tone t) {
    auto& th = Theme::get();
    switch (t) {
    case Primary: return {th.color("primary_container"), th.color("on_primary_container")};
    case Secondary: return {th.color("secondary_container"), th.color("on_secondary_container")};
    case Tertiary: return {th.color("tertiary_container"), th.color("on_tertiary_container")};
    case Error: return {th.color("error_container"), th.color("on_error_container")};
    default: return {th.color("surface_container_highest"), th.color("on_surface")};
    }
}

// What it's drawn in: when these and the version are unchanged, it's left
std::string stamp() {
    std::string s = std::format("v{}", kVersion);
    for (Tone t : {Primary, Secondary, Tertiary, Neutral, Error}) {
        const auto [a, b] = colours(t);
        s += " " + hex(a) + hex(b);
    }
    return s;
}

std::string read(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// One icon: a tonal circle with the glyph filled in its middle, as an SVG
// (cairo writes the glyph as its outline, so it needs no font to be drawn)
bool draw(const std::string& file, const char* glyph, Tone tone, PangoFontMap* fonts) {
    constexpr double S = 128;
    cairo_surface_t* surface = cairo_svg_surface_create(file.c_str(), S, S);
    cairo_svg_surface_set_document_unit(surface, CAIRO_SVG_UNIT_PX);
    cairo_t* cr = cairo_create(surface);
    const auto [bg, fg] = colours(tone);
    cairo_arc(cr, S / 2, S / 2, S / 2 - 6, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
    cairo_fill(cr);

    PangoContext* ctx = pango_font_map_create_context(fonts);
    PangoLayout* layout = pango_layout_new(ctx);
    PangoFontDescription* desc = pango_font_description_from_string(Theme::get().iconFamily().c_str());
    pango_font_description_set_absolute_size(desc, 64 * PANGO_SCALE);
    pango_font_description_set_variations(desc, "FILL=1,wght=500,GRAD=0,opsz=48");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout, glyph, -1);
    // A name the font has no ligature for comes out as its letters: not drawn
    bool ok = pango_layout_get_line_count(layout) == 1;
    if (ok) {
        PangoLayoutLine* line = pango_layout_get_line_readonly(layout, 0);
        int glyphs = 0;
        for (GSList* r = line->runs; r; r = r->next) glyphs += static_cast<PangoGlyphItem*>(r->data)->glyphs->num_glyphs;
        ok = glyphs == 1;
    }
    if (ok) {
        PangoRectangle ink, logical;
        pango_layout_get_pixel_extents(layout, &ink, &logical);
        cairo_move_to(cr, S / 2 - ink.x - ink.width / 2.0, S / 2 - ink.y - ink.height / 2.0);
        cairo_set_source_rgb(cr, fg.r, fg.g, fg.b);
        pango_cairo_update_context(cr, ctx);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
    g_object_unref(ctx);
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    const bool fine = cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    if (!ok) {
        log::warn("icon theme: \"{}\" isn't a Material Symbol", glyph);
        std::error_code ec;
        fs::remove(file, ec);
    }
    return ok && fine;
}

int g_count = 0;

// All of it into a fresh folder, then swapped in whole: GTK sees the icons
// folder change and looks again
void generate() {
    const auto started = std::chrono::steady_clock::now();
    const std::string dir = themeDir(), fresh = dir + ".new", old = dir + ".old";
    std::error_code ec;
    fs::remove_all(fresh, ec);
    PangoFontMap* fonts = pango_cairo_font_map_new();
    int n = 0;
    std::vector<std::string> contexts;
    for (const auto& d : table()) {
        const std::string sub = std::string("scalable/") + d.context;
        if (std::ranges::find(contexts, d.context) == contexts.end()) {
            contexts.push_back(d.context);
            fs::create_directories(fresh + "/" + sub, ec);
        }
        // Drawn once, the other names linked to it
        const std::string first = fresh + "/" + sub + "/" + d.names.front() + ".svg";
        if (!draw(first, d.glyph, d.tone, fonts)) continue;
        ++n;
        for (size_t i = 1; i < d.names.size(); ++i) {
            fs::create_symlink(std::string(d.names.front()) + ".svg", fresh + "/" + sub + "/" + d.names[i] + ".svg", ec);
            ++n;
        }
    }
    g_object_unref(fonts);

    std::string index = std::format("[Icon Theme]\nName={}\nComment=Material Symbols in your wallpaper's colours, drawn by Material3UI\n"
                                    "Inherits=Adwaita,hicolor\nExample=folder\n",
                                    kName);
    std::string dirs;
    for (const auto& c : contexts) dirs += (dirs.empty() ? "" : ",") + std::string("scalable/") + c;
    index += "Directories=" + dirs + "\n";
    for (const auto& c : contexts) {
        const std::string ctx = std::string(c) == "apps" ? "Applications" : std::string(c) == "places" ? "Places" : std::string(c) == "devices" ? "Devices" : "MimeTypes";
        index += std::format("\n[scalable/{}]\nSize=128\nMinSize=8\nMaxSize=512\nType=Scalable\nContext={}\n", c, ctx);
    }
    paths::writeFile(fresh + "/index.theme", index);
    paths::writeFile(fresh + "/.stamp", stamp());

    fs::remove_all(old, ec);
    if (fs::exists(dir, ec)) fs::rename(dir, old, ec);
    fs::rename(fresh, dir, ec);
    if (ec) log::warn("icon theme: {}", ec.message());
    fs::remove_all(old, ec);
    g_count = n;
    log::info("icon theme: drew {} icons in {} ms", n,
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    // The shell's own icons, drawn afresh from it
    Images::get().reloadIcons();
}

// A key in a GTK settings.ini's [Settings], the rest left as it was
void setGtkKey(const std::string& file, const std::string& key, const std::string& value) {
    std::istringstream in(read(file));
    std::string out, line;
    bool inSettings = false, done = false, sawSection = false;
    while (std::getline(in, line)) {
        if (line.starts_with("[")) {
            if (inSettings && !done) out += key + "=" + value + "\n", done = true;
            inSettings = line.starts_with("[Settings]");
            sawSection = sawSection || inSettings;
        } else if (inSettings) {
            const std::string k = line.substr(0, line.find('='));
            if (k.substr(0, k.find_last_not_of(" \t") + 1) == key) {
                if (!done) out += key + "=" + value + "\n", done = true;
                continue;
            }
        }
        out += line + "\n";
    }
    if (!done) out += (sawSection ? "" : "[Settings]\n") + key + "=" + value + "\n";
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    paths::writeFile(file, out);
}

std::string gtkIconTheme() {
    std::istringstream in(read(paths::home() + "/.config/gtk-3.0/settings.ini"));
    for (std::string line; std::getline(in, line);)
        if (line.starts_with("gtk-icon-theme-name")) {
            std::string v = line.substr(line.find('=') + 1);
            v.erase(0, v.find_first_not_of(" \t\""));
            v.erase(v.find_last_not_of(" \t\"") + 1);
            return v;
        }
    return "";
}

void applyGtk(const std::string& name) {
    for (const char* v : {"gtk-3.0", "gtk-4.0"}) setGtkKey(paths::home() + "/.config/" + v + "/settings.ini", "gtk-icon-theme-name", name);
    run({"gsettings", "set", "org.gnome.desktop.interface", "icon-theme", name}, nullptr);
    Images::get().rescan();
}

} // namespace

bool enabled() {
    return Config::get().value<bool>("/appearance/iconTheme", false);
}

int count() {
    return g_count;
}

void update() {
    if (!enabled()) return;
    // A palette change comes as a few in a row (dark, then the colours):
    // drawn once they've settled
    static EventLoop::TimerId pending = 0;
    if (pending) EventLoop::get().cancel(pending);
    pending = EventLoop::get().addTimer(600, [] {
        pending = 0;
        if (!enabled()) return;
        if (read(themeDir() + "/.stamp") == stamp() && fs::exists(themeDir() + "/index.theme")) {
            if (!g_count) {
                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(themeDir(), ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                    g_count += it->path().extension() == ".svg";
            }
            return;
        }
        generate();
    });
}

void setEnabled(bool on) {
    if (on == enabled()) return;
    if (on) {
        const std::string before = gtkIconTheme();
        if (before != kName) Config::get().set("/appearance/iconThemeBefore", before.empty() ? "Adwaita" : before);
        Config::get().set("/appearance/iconTheme", true);
        generate();
        applyGtk(kName);
    } else {
        Config::get().set("/appearance/iconTheme", false);
        applyGtk(Config::get().value<std::string>("/appearance/iconThemeBefore", "Adwaita"));
        Images::get().reloadIcons();
    }
}

} // namespace m3::icontheme
