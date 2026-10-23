// A Material You file picker for pictures: the wallpaper, your profile picture
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
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

} // namespace

void filePicker(Flex* card, Popup& popup, const std::string& purpose) {
    const bool avatar = purpose == "avatar";
    struct State {
        fs::path dir;
        std::string chosen;
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
    st->dir = avatar && !avatars.empty() ? fs::path(avatars) : fs::path(Wallpaper::get().directory());
    std::error_code ec;
    // No wallpaper folder yet: the ones the shell ships
    if (!fs::is_directory(st->dir, ec) && !avatar && !shipped.empty()) st->dir = shipped;
    if (!fs::is_directory(st->dir, ec)) st->dir = paths::home() + "/Pictures";
    if (!fs::is_directory(st->dir, ec)) st->dir = paths::home();

    // Title
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>(avatar ? "account_circle" : "wallpaper", 26)->setColor("primary", false);
    auto* title = head->add<Label>(avatar ? "Choose your picture" : "Choose a wallpaper");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    iconButton(head, "close", [&popup] { popup.close(); });

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
    st->scroll->maxHeight = 460;
    st->scroll->blurEdges = true;
    st->grid = st->scroll->add<Flex>(Flex::Direction::Column);
    st->grid->setAlignItems(Align::Stretch);

    std::vector<std::tuple<std::string, std::string, std::string>> placeList; // icon, name, path
    if (avatar && !avatars.empty()) placeList.push_back({"face", "Default pictures", avatars});
    placeList.push_back({"home", "Home", paths::home()});
    placeList.push_back({"image", "Pictures", paths::home() + "/Pictures"});
    placeList.push_back({"download", "Downloads", paths::home() + "/Downloads"});
    if (!avatar) placeList.push_back({"wallpaper", "Wallpapers", Wallpaper::get().directory()});
    if (!avatar && !shipped.empty()) placeList.push_back({"palette", "Material3UI wallpapers", shipped});

    st->fill = [st, placeList, avatar] {
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
            else if (e.is_regular_file(ec) && isPicture(e.path())) pictures.push_back(e.path());
        }
        std::ranges::sort(folders);
        std::ranges::sort(pictures);
        st->grid->clearChildren();
        st->chosen.clear();
        st->chosenTile = nullptr;
        if (st->choose) st->choose->setOpacity(0.4f);
        if (st->chosenName) st->chosenName->setText(pictures.empty() ? "No pictures here" : "Pick a picture");
        auto* flow = st->grid->add<Flow>();
        flow->spacing = 10;
        // Pictures shown square for profile pictures, wide for wallpapers
        const float tw = avatar ? 120 : 196, th = avatar ? 120 : 110;
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
        st->scroll->scrollTo(0, false);
        st->grid->requestLayout();
    };

    // What's chosen, and choosing it
    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(10)->setAlignItems(Align::Center);
    st->chosenName = foot->add<Label>("Pick a picture");
    st->chosenName->setSize(14)->setMaxWidth(520)->setColor("on_surface_variant", false);
    st->chosenName->grow = true;
    pillButton(foot, "Cancel", false, [&popup] { popup.close(); });
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
