// A Material You file picker: pictures for the wallpaper and your profile
// picture, or any files (several) to send with Quick Share
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Backups.h"
#include "services/Vpn.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

bool isPicture(const fs::path& p) {
    auto ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".bmp" || ext == ".gif" || ext == ".svg";
}

// A picture drawn to fit `w` × `h`: SVGs through the icon renderer
ImageView* preview(Node* parent, const std::string& path, float w, float h) {
    const bool svg = path.ends_with(".svg");
    auto* v = parent->add<ImageView>(w, h, [path, w, h, svg](float scale, int& tw, int& th) -> GLuint {
        auto* t = svg ? Images::get().icon(path, static_cast<int>(std::lround(std::max(w, h) * scale)))
                      : Images::get().picture(path, static_cast<int>(w * scale), static_cast<int>(h * scale), true);
        if (!t) return 0;
        tw = t->width;
        th = t->height;
        return t->id;
    });
    v->cover = true;
    return v;
}

// The icon for a file that isn't a picture, by its extension
std::string fileIcon(const fs::path& p) {
    auto ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" || ext == ".avi") return "movie";
    if (ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".opus" || ext == ".m4a" || ext == ".wav") return "music_note";
    if (ext == ".pdf") return "picture_as_pdf";
    if (ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".xz" || ext == ".zst" || ext == ".7z" || ext == ".rar") return "folder_zip";
    if (ext == ".apk") return "android";
    if (ext == ".txt" || ext == ".md" || ext == ".doc" || ext == ".docx" || ext == ".odt") return "description";
    return "draft";
}

} // namespace

// purpose: "wallpaper", "avatar", "vpn" (a WireGuard or OpenVPN file to
// import), "settings" (a settings file to import), or "send" followed by
// the files already chosen, one per line (they go back to Quick Share with
// the new ones)
void filePicker(Flex* card, Popup& popup, const std::string& purpose) {
    const bool avatar = purpose == "avatar";
    const bool vpn = purpose == "vpn", settingsFile = purpose == "settings";
    // One file of these types to import
    const bool importing = vpn || settingsFile;
    const std::vector<std::string> types = vpn ? std::vector<std::string>{".conf", ".ovpn"}
                                         : settingsFile ? std::vector<std::string>{".json"}
                                                        : std::vector<std::string>{};
    // Files of any kind, picked (several, when sending) by tapping
    const bool sending = purpose.starts_with("send") || importing;
    std::string already = sending && purpose.size() > 4 ? purpose.substr(5) : "";
    struct State {
        fs::path dir;
        std::string chosen;
        std::vector<std::string> picked; // sending: several
        Button* chosenTile = nullptr;
        Label* chosenName = nullptr;
        Button* choose = nullptr;
        Flex* places = nullptr;
        Flex* pathBar = nullptr;
        Flex* grid = nullptr;
        ScrollView* scroll = nullptr;
        std::function<void()> fill;
    };
    auto st = std::make_shared<State>();
    const std::string avatars = paths::dataFile("avatars");
    const std::string shipped = paths::dataFile("wallpapers");
    // Where it opens: the default pictures for a profile picture, the
    // wallpaper's folder for a wallpaper
    st->dir = importing ? fs::path(paths::home() + "/Downloads") : sending ? fs::path(paths::home()) : avatar && !avatars.empty() ? fs::path(avatars) : fs::path(Wallpaper::get().directory());
    std::error_code ec;
    // No wallpaper folder yet: the ones the shell ships
    if (!fs::is_directory(st->dir, ec) && !avatar && !shipped.empty()) st->dir = shipped;
    if (!fs::is_directory(st->dir, ec)) st->dir = paths::home() + "/Pictures";
    if (!fs::is_directory(st->dir, ec)) st->dir = paths::home();

    // Title
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>(vpn ? "vpn_key" : settingsFile ? "settings_backup_restore" : sending ? "send" : avatar ? "account_circle" : "wallpaper", 26)->setColor("primary", false);
    auto* title = head->add<Label>(vpn            ? "Choose a VPN file"
                                   : settingsFile ? "Choose a settings file"
                                   : sending ? "Choose files to send"
                                   : avatar  ? "Choose your picture"
                                             : "Choose a wallpaper");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    // Closing while sending goes back to Quick Share, files kept
    iconButton(head, "close", [&popup, sending, importing, already] {
        popup.close();
        if (sending && !importing) open("quickshare-send " + already);
    });

    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(16)->setAlignItems(Align::Start);
    // Places down the side
    st->places = columns->add<Flex>(Flex::Direction::Column);
    st->places->setSpacing(4)->setAlignItems(Align::Stretch);
    st->places->fixedWidth = 200;
    auto* right = columns->add<Flex>(Flex::Direction::Column);
    right->setSpacing(10)->setAlignItems(Align::Stretch);
    right->grow = true;
    st->pathBar = right->add<Flex>(Flex::Direction::Row);
    st->pathBar->setSpacing(8)->setAlignItems(Align::Center);
    st->scroll = right->add<ScrollView>();
    // A fixed height: the same size in every folder, and measured exactly
    // (the footer stays on the card)
    st->scroll->maxHeight = 380;
    st->scroll->fixedHeight = 380;
    st->scroll->blurEdges = true;
    st->grid = st->scroll->add<Flex>(Flex::Direction::Column);
    st->grid->setAlignItems(Align::Stretch);

    std::vector<std::tuple<std::string, std::string, std::string>> placeList; // icon, name, path
    if (avatar && !avatars.empty()) placeList.push_back({"face", "Default pictures", avatars});
    placeList.push_back({"home", "Home", paths::home()});
    if (sending) {
        placeList.push_back({"download", "Downloads", paths::home() + "/Downloads"});
        placeList.push_back({"description", "Documents", paths::home() + "/Documents"});
        placeList.push_back({"image", "Pictures", paths::home() + "/Pictures"});
        placeList.push_back({"movie", "Videos", paths::home() + "/Videos"});
        placeList.push_back({"music_note", "Music", paths::home() + "/Music"});
        placeList.push_back({"desktop_windows", "Desktop", paths::home() + "/Desktop"});
    } else {
        placeList.push_back({"image", "Pictures", paths::home() + "/Pictures"});
        placeList.push_back({"download", "Downloads", paths::home() + "/Downloads"});
    }
    if (!avatar && !sending) placeList.push_back({"wallpaper", "Wallpapers", Wallpaper::get().directory()});
    if (!avatar && !sending && !shipped.empty()) placeList.push_back({"palette", "Material3UI wallpapers", shipped});

    // Sending: what's picked, said in the footer
    const auto showPicked = [st, vpn, settingsFile] {
        if (!st->chosenName) return;
        st->chosenName->setText(st->picked.empty() && vpn            ? "Pick your provider's .conf (WireGuard) or .ovpn (OpenVPN) file"
                                : st->picked.empty() && settingsFile ? "Pick a settings file you exported (.json)"
                                : st->picked.empty()       ? "Pick files: tap again to leave one out"
                                : st->picked.size() == 1 ? fs::path(st->picked.front()).filename().string()
                                                         : std::format("{} files", st->picked.size()));
        if (st->choose) st->choose->setOpacity(st->picked.empty() ? 0.4f : 1);
    };

    st->fill = [st, placeList, avatar, sending, importing, types, showPicked] {
        std::error_code ec;
        // Places, the current one highlighted
        st->places->clearChildren();
        for (const auto& [icon, name, path] : placeList) {
            if (!fs::is_directory(path, ec)) continue;
            const bool here = fs::equivalent(path, st->dir, ec);
            auto* b = st->places->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({14, 10})->setSpacing(12);
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 20)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            b->add<Label>(name)->setSize(14)->setWeight(here ? 600 : 500)->setMaxWidth(150)->setColor(here ? "on_secondary_container" : "on_surface", false);
            const std::string to = path;
            b->onClicked = [st, to](uint32_t button) {
                if (button != BTN_LEFT) return;
                st->dir = to;
                EventLoop::get().defer([st] { st->fill(); });
            };
        }
        // Where we are, and up
        st->pathBar->clearChildren();
        const bool top = st->dir == st->dir.root_path();
        auto* up = st->pathBar->add<Button>();
        pill(up)->setJustify(Align::Center)->setBackground("surface_container_high", false);
        up->fixedWidth = up->fixedHeight = 38;
        up->add<Icon>("arrow_upward", 20)->setColor(top ? "outline" : "on_surface", false);
        up->onClicked = [st, top](uint32_t button) {
            if (button != BTN_LEFT || top) return;
            st->dir = st->dir.parent_path();
            EventLoop::get().defer([st] { st->fill(); });
        };
        std::string shown = st->dir.string();
        if (shown.starts_with(paths::home())) shown = "~" + shown.substr(paths::home().size());
        auto* where = st->pathBar->add<Flex>(Flex::Direction::Row);
        pill(where)->setPadding({16, 9})->setBackground("surface_container_high", false);
        where->grow = true;
        where->add<Label>(shown)->setSize(14)->setMaxWidth(520)->setColor("on_surface", false);

        // Folders, then pictures, by name; hidden ones left out
        std::vector<fs::path> folders, pictures;
        for (const auto& e : fs::directory_iterator(st->dir, fs::directory_options::skip_permission_denied, ec)) {
            const auto name = e.path().filename().string();
            if (name.starts_with(".")) continue;
            if (e.is_directory(ec)) folders.push_back(e.path());
            else if (e.is_regular_file(ec) && (importing ? std::ranges::find(types, e.path().extension().string()) != types.end()
                                                         : sending || isPicture(e.path())))
                pictures.push_back(e.path());
        }
        std::ranges::sort(folders);
        std::ranges::sort(pictures);
        st->grid->clearChildren();
        st->chosen.clear();
        st->chosenTile = nullptr;
        if (st->choose) st->choose->setOpacity(0.4f);
        if (st->chosenName) st->chosenName->setText(pictures.empty() ? "No pictures here" : "Pick a picture");
        if (sending) showPicked();
        auto* flow = st->grid->add<Flow>();
        flow->spacing = 10;
        // Pictures shown square for profile pictures, wide for wallpapers
        const float tw = avatar ? 120 : sending ? 150 : 196, th = avatar ? 120 : sending ? 100 : 110;
        size_t count = 0;
        for (const auto& f : folders) {
            if (++count > 400) break;
            auto* b = flow->add<Button>(Flex::Direction::Column);
            b->setPadding(Insets(8))->setSpacing(6)->setRadius(Radii(20))->setAlignItems(Align::Center);
            b->setBackground("surface_container_high", false);
            b->fixedWidth = tw + 16;
            auto* icon = b->add<Flex>(Flex::Direction::Row);
            icon->setJustify(Align::Center);
            icon->fixedHeight = th * 0.6f;
            icon->add<Icon>("folder", 44)->setFill(true)->setColor("primary", false);
            b->add<Label>(f.filename().string())->setSize(13)->setWeight(500)->setMaxWidth(tw)->setCentered(true);
            const fs::path to = f;
            b->onClicked = [st, to](uint32_t button) {
                if (button != BTN_LEFT) return;
                st->dir = to;
                EventLoop::get().defer([st] { st->fill(); });
            };
        }
        for (const auto& p : pictures) {
            if (++count > 400) break;
            if (sending) {
                // A tile per file: a preview for pictures, an icon otherwise;
                // tapping picks it or leaves it out
                const std::string file = p.string();
                const bool picked = std::ranges::find(st->picked, file) != st->picked.end();
                auto* b = flow->add<Button>(Flex::Direction::Column);
                b->setPadding(Insets(6))->setSpacing(6)->setRadius(Radii(20))->setAlignItems(Align::Center);
                b->fixedWidth = tw + 12;
                b->tooltip = p.filename().string();
                if (isPicture(p) && !file.ends_with(".svg")) {
                    preview(b, file, tw, th)->radius = 14;
                } else {
                    auto* icon = b->add<Flex>(Flex::Direction::Row);
                    icon->setJustify(Align::Center)->setRadius(Radii(14))->setBackground("surface_container_highest", false);
                    icon->fixedWidth = tw;
                    icon->fixedHeight = th;
                    icon->add<Icon>(fileIcon(p), 40)->setColor("on_surface_variant", false);
                }
                b->add<Label>(p.filename().string())->setSize(12)->setWeight(500)->setMaxWidth(tw)->setCentered(true);
                const auto mark = [b](bool on) {
                    b->setBorder(on ? 3 : 0, "primary")->setBackground(on ? ColorRef("secondary_container") : ColorRef::none());
                };
                mark(picked);
                b->onClicked = [st, file, mark, showPicked, importing](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    // One file at a time to import: picking another moves the mark
                    if (importing && !st->picked.empty() && st->picked.front() != file) {
                        st->picked.clear();
                        EventLoop::get().defer([st] { st->fill(); });
                    }
                    const auto it = std::ranges::find(st->picked, file);
                    const bool on = it == st->picked.end();
                    if (on) st->picked.push_back(file);
                    else st->picked.erase(it);
                    mark(on);
                    showPicked();
                };
                continue;
            }
            auto* b = flow->add<Button>(Flex::Direction::Column);
            b->setPadding(Insets(6))->setSpacing(6)->setRadius(Radii(avatar ? 70 : 20))->setAlignItems(Align::Center);
            b->pressedRadius = 12;
            b->tooltip = p.filename().string();
            auto* img = preview(b, p.string(), tw, th);
            if (avatar) img->radius = tw / 2; // as it will show: a circle
            const std::string file = p.string();
            b->onClicked = [st, b, file](uint32_t button) {
                if (button != BTN_LEFT) return;
                if (st->chosenTile) st->chosenTile->setBorder(0, "primary")->setBackground(ColorRef::none());
                st->chosenTile = b;
                st->chosen = file;
                b->setBorder(3, "primary")->setBackground("secondary_container");
                if (st->chosenName) st->chosenName->setText(fs::path(file).filename().string());
                if (st->choose) st->choose->setOpacity(1);
            };
        }
        if (folders.empty() && pictures.empty()) caption(st->grid, "Nothing here");
        if (count > 400) caption(st->grid, "Showing the first 400");
        st->scroll->scrollTo(0, false);
        st->grid->requestLayout();
    };

    // What's chosen, and choosing it
    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(10)->setAlignItems(Align::Center);
    st->chosenName = foot->add<Label>("Pick a picture");
    st->chosenName->setSize(14)->setMaxWidth(520)->setColor("on_surface_variant", false);
    st->chosenName->grow = true;
    pillButton(foot, "Cancel", false, [&popup, sending, importing, already] {
        popup.close();
        if (sending && !importing) open("quickshare-send " + already);
    });
    if (settingsFile) {
        // What's there now is backed up first; a bad file changes nothing
        st->choose = pillButton(foot, "Import", true, [st, &popup] {
            if (st->picked.empty()) return;
            const std::string file = st->picked.front();
            popup.close();
            Backups::get().create("before-import", [file](bool) {
                const std::string error = Config::get().importFrom(file);
                m3::run({"notify-send", "-a", "Settings", "-i", "settings", error.empty() ? "Settings imported" : "Settings not imported",
                         error.empty() ? "The previous ones are in Backups" : error},
                        nullptr);
                settingsPage("system");
            });
        }, "check");
        st->fill();
        return;
    }
    if (vpn) {
        st->choose = pillButton(foot, "Import", true, [st, &popup] {
            if (st->picked.empty()) return;
            Vpn::get().import(st->picked.front());
            popup.close();
            settingsPage("vpn");
        }, "check");
        st->fill();
        return;
    }
    if (sending) {
        st->choose = pillButton(foot, "Add", true, [st, already, &popup] {
            if (st->picked.empty()) return;
            std::string all = already;
            for (const auto& f : st->picked) all += f + "\n";
            popup.close();
            open("quickshare-send " + all);
        }, "check");
        st->fill();
        return;
    }
    st->choose = pillButton(foot, avatar ? "Use as my picture" : "Set as wallpaper", true, [st, avatar, &popup] {
        if (st->chosen.empty()) return;
        const std::string file = st->chosen;
        if (avatar) {
            // ~/.face, which login screens read too; an SVG becomes a PNG there
            const std::string face = paths::home() + "/.face";
            if (file.ends_with(".svg")) {
                m3::run({"rsvg-convert", "-w", "512", "-h", "512", file, "-o", face}, [face](int, const std::string&) {
                    Images::get().forget(face);
                    settingsPage("users");
                });
            } else {
                std::error_code e;
                fs::copy_file(file, face, fs::copy_options::overwrite_existing, e);
                Images::get().forget(face);
                EventLoop::get().defer([] { settingsPage("users"); });
            }
        } else {
            Wallpaper::get().set(file);
        }
        popup.close();
    }, avatar ? "account_circle" : "wallpaper");
    st->fill();
}

} // namespace m3::shell::panels
