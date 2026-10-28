// The first-run tour (an app window) and What's new after an update (a popup)
#include "build_config.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "core/Version.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Wallpaper.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "shell/bar/Widgets.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <sstream>

namespace m3::shell::panels {

using namespace m3::ui;

namespace {

// The changelog's order: pre-releases before their release
bool newer(const std::string& a, const std::string& b) {
    return newerVersion(a, b);
}

struct Release {
    std::string version, date;
    std::vector<std::pair<std::string, std::string>> notes; // the bold lead, the rest
};

// CHANGELOG.md: "## vX — date", then "- **Lead.** more" bullets
std::vector<Release> changelog() {
    std::string file = paths::dataFile("CHANGELOG.md");
    if (file.empty()) file = paths::dataFile("../../CHANGELOG.md"); // running from shell/build
    std::ifstream in(file);
    std::vector<Release> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("## v")) {
            Release r;
            const auto dash = line.find(" — ");
            r.version = line.substr(4, dash == std::string::npos ? std::string::npos : dash - 4);
            if (dash != std::string::npos) r.date = line.substr(dash + std::string(" — ").size());
            out.push_back(std::move(r));
        } else if (line.starts_with("- ") && !out.empty()) {
            std::string text = line.substr(2), lead;
            if (text.starts_with("**")) {
                if (const auto end = text.find("**", 2); end != std::string::npos) {
                    lead = text.substr(2, end - 2);
                    text = text.substr(end + 2);
                    if (!text.empty() && text.front() == ' ') text.erase(0, 1);
                    // "**Lead**, the rest": the rest reads as its own sentence
                    if (!text.empty() && (text.front() == ',' || text.front() == ':' || text.front() == ';')) {
                        text.erase(0, text.size() > 1 && text[1] == ' ' ? 2 : 1);
                        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
                    }
                    if (!lead.empty() && lead.back() == '.') lead.pop_back();
                }
            }
            // Inline markup reads as plain text here
            for (const char* mark : {"**", "`"})
                for (size_t p; (p = text.find(mark)) != std::string::npos;) text.erase(p, std::string(mark).size());
            out.back().notes.push_back({lead, text});
        }
    }
    return out;
}

ImageView* logo(Node* parent, float size) {
    const std::string path = paths::dataFile("icons/Material3UI.svg");
    return parent->add<ImageView>(size, [path, size](float scale, int& w, int& h) -> GLuint {
        auto* t = path.empty() ? nullptr : Images::get().icon(path, static_cast<int>(std::lround(size * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
}

Label* para(Node* parent, const std::string& text, float width, float size = 14) {
    auto* l = parent->add<Label>(text);
    l->setSize(size)->setMaxWidth(width)->setMaxLines(12)->setColor("on_surface_variant", false);
    return l;
}

// One tour page: a large icon, a title and a line about it, then its controls
Flex* pageHead(Flex* page, const std::string& icon, const std::string& title, const std::string& sub) {
    auto* badge = page->add<Flex>(Flex::Direction::Row);
    badge->setPadding(Insets(14))->setRadius(Radii(24))->setBackground("primary_container", false);
    badge->alignSelf = Align::Start;
    badge->add<Icon>(icon, 32)->setColor("on_primary_container", false);
    page->add<Label>(title)->setSize(30)->setWeight(600);
    para(page, sub, 680, 15);
    return page;
}

void tourLook(Flex* page) {
    pageHead(page, "palette", "Make it yours", "Material You colours come from your wallpaper. Pick a theme and an accent; you can change these any time in Settings → Wallpaper & style.");
    auto* g = group(page, "Colours");
    auto dark = row(g, "dark_mode", "Dark theme");
    auto* sw = dark.row->add<Switch>();
    sw->setChecked(Config::get().value<bool>("/background/darkmode", true), false);
    sw->onToggled = [](bool on) {
        Config::get().set("/background/darkmode", on);
        Wallpaper::get().recolour();
    };
    accentRows(g);
    auto wp = row(g, "wallpaper", "Wallpaper", "A few come with the shell; add your own to the folder");
    pillButton(wp.row, "Choose…", false, [] {
        if (open) open("wallpapers");
    }, "image");
}

void tourLayout(Flex* page) {
    pageHead(page, "dashboard", "Bar and dock", "The bar holds your workspaces, the clock island and quick toggles; the dock keeps your favourite apps close.");
    auto* g = group(page, "Layout");
    auto pos = row(g, "vertical_align_top", "Bar position");
    connectedGroup<std::string>(pos.row, Config::get().value<std::string>("/statusBar/position", "top"),
                                {{"top", "Top"}, {"bottom", "Bottom"}, {"left", "Left"}, {"right", "Right"}},
                                [](std::string v) { Config::get().set("/statusBar/position", v); });
    auto dock = row(g, "dock_to_bottom", "Dock");
    connectedGroup<std::string>(dock.row, Config::get().value<std::string>("/dock/mode", "autohide"),
                                {{"off", "Off"}, {"autohide", "Auto-hide"}, {"always", "Always"}},
                                [](std::string v) { Config::get().set("/dock/mode", v); });
    std::vector<std::pair<std::string, std::string>> times;
    for (const char* f : {"hh:mm", "h:mm AP"}) times.push_back({f, Clock::sample(f)});
    auto clock = row(g, "schedule", "Clock");
    connectedGroup<std::string>(clock.row, Config::get().value<std::string>("/bar/clock/island/timeFormat", Config::get().value<std::string>("/time/format", "hh:mm")), times,
                                [](std::string v) {
                                    Config::get().set("/bar/clock/island/timeFormat", v);
                                    Config::get().set("/bar/clock/bar/timeFormat", v);
                                    Config::get().set("/desktop/timeFormat", v);
                                });
}

void tourKeys(Flex* page) {
    pageHead(page, "keyboard", "Keys worth knowing", "Super is the Windows key. Every shortcut can be changed in Hyprland Settings → Keybinds.");
    auto* g = group(page, "Shortcuts");
    const std::vector<std::tuple<std::string, std::string, std::string>> keys = {
        {"search", "Super", "Search apps, files and settings (tap)"},
        {"tab", "Super + Tab", "Switch windows; hold Super, tap Tab"},
        {"grid_view", "Super + `", "Overview of every workspace"},
        {"toggle_on", "Super + N", "Quick Settings"},
        {"notifications", "Super + A", "Notifications"},
        {"settings", "Super + I", "Settings"},
        {"close", "Super + Q", "Close the window"},
        {"lock", "Super + L", "Lock"},
    };
    for (const auto& [icon, key, what] : keys) {
        auto r = row(g, icon, what);
        auto* chip = r.row->add<Flex>(Flex::Direction::Row);
        pill(chip)->setPadding({12, 6})->setBackground("surface_container_highest", false);
        chip->add<Label>(key)->setSize(13)->setWeight(600)->setFamily(Theme::get().monoFamily());
    }
}

void tourDone(Flex* page) {
    pageHead(page, "celebration", "You're all set", "Right-click the desktop for widgets and shortcuts, and find everything else in Settings. This tour is in Settings → About whenever you want it again.");
    auto* g = group(page, "Next");
    auto s = row(g, "settings", "Settings", "Wi-Fi, sound, displays, the bar, the lock screen…");
    pillButton(s.row, "Open", false, [] {
        if (open) open("settings");
    });
    auto h = row(g, "tune", "Hyprland Settings", "Windows, animations, input, keybinds and more");
    pillButton(h.row, "Open", false, [] {
        if (open) open("hyprsettings");
    });
    auto d = row(g, "dashboard_customize", "Desktop widgets", "A clock, weather, music and app shortcuts on the wallpaper");
    pillButton(d.row, "Edit", false, [] {
        if (open) open("desktopedit");
    });
}

} // namespace

void releaseNotes(Node* parent, int max, float width, const std::string& since) {
    auto* list = parent->add<Flex>(Flex::Direction::Column);
    list->setSpacing(10)->setAlignItems(Align::Stretch);
    int shown = 0;
    for (const auto& r : changelog()) {
        if (newer(r.version, M3_VERSION)) continue; // written ahead of a release
        if (shown >= max || (shown > 0 && !newer(r.version, since))) break;
        ++shown;
        auto* ver = list->add<Flex>(Flex::Direction::Row);
        ver->setSpacing(8)->setAlignItems(Align::Center)->setPadding({0, shown > 1 ? 8.0f : 0.0f, 0, 0});
        auto* tag = ver->add<Flex>(Flex::Direction::Row);
        pill(tag)->setPadding({10, 3})->setBackground("primary_container", false);
        tag->add<Label>("v" + r.version)->setSize(12)->setWeight(600)->setColor("on_primary_container", false);
        ver->add<Label>(r.date)->setSize(12)->setColor("on_surface_variant", false);
        auto* seg = list->add<Segments>();
        for (const auto& [lead, text] : r.notes) {
            auto* item = seg->add<Flex>(Flex::Direction::Column);
            item->setPadding({16, 12})->setSpacing(3)->setAlignItems(Align::Start);
            if (!lead.empty()) item->add<Label>(lead)->setSize(14)->setWeight(600)->setMaxWidth(width)->setMaxLines(3);
            if (!text.empty()) para(item, text, width, 13);
        }
    }
    if (shown == 0) para(list, "Bug fixes and polish.", width);
}

void welcome(Flex* card, Popup& popup) {
    struct Tour {
        int page = 0;
        Flex* body = nullptr;
        Flex* dots = nullptr;
        Button *skip = nullptr, *back = nullptr, *next = nullptr;
        Label* nextLabel = nullptr;
        std::function<void()> show;
    };
    constexpr int kPages = 5;
    auto t = std::make_shared<Tour>();

    auto* scroll = card->add<ScrollView>();
    scroll->grow = true;
    scroll->maxHeight = 100000;
    scroll->blurEdges = true;
    t->body = scroll->add<Flex>(Flex::Direction::Column);
    t->body->setPadding({48, 28, 48, 20})->setSpacing(14)->setAlignItems(Align::Stretch);

    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setPadding({48, 14, 48, 24})->setSpacing(10)->setAlignItems(Align::Center);
    t->dots = foot->add<Flex>(Flex::Direction::Row);
    t->dots->setSpacing(6)->setAlignItems(Align::Center);
    t->dots->grow = true;
    auto* skip = t->skip = foot->add<Button>();
    pill(skip)->setPadding({16, 10});
    skip->add<Label>("Skip")->setSize(14)->setWeight(500)->setColor("primary", false);
    skip->onClicked = [&popup](uint32_t b) {
        if (b == BTN_LEFT) popup.close();
    };
    t->back = pillButton(foot, "Back", false, [t] {
        if (t->page > 0) {
            --t->page;
            t->show();
        }
    });
    t->next = pillButton(foot, "Next", true, [t, &popup] {
        if (t->page + 1 >= kPages) return popup.close();
        ++t->page;
        t->show();
    });
    for (auto& c : t->next->children())
        if (auto* l = dynamic_cast<Label*>(c.get())) t->nextLabel = l;

    t->show = [raw = t.get(), scroll] {
        auto* t = raw;
        t->body->clearChildren();
        switch (t->page) {
        case 0: {
            auto* hero = t->body->add<Flex>(Flex::Direction::Column);
            hero->setSpacing(14)->setPadding({24, 0})->setAlignItems(Align::Center);
            logo(hero, 132);
            hero->add<Label>("Welcome to Material3UI")->setSize(34)->setWeight(600);
            para(hero, "A Material 3 Expressive desktop for Hyprland, version " M3_VERSION ". A few quick choices and you're ready; "
                       "nothing here is final, and all of it is in Settings later.",
                 620, 16)->setCentered(true);
            break;
        }
        case 1: tourLook(t->body); break;
        case 2: tourLayout(t->body); break;
        case 3: tourKeys(t->body); break;
        default: tourDone(t->body); break;
        }
        t->dots->clearChildren();
        for (int i = 0; i < kPages; ++i) {
            auto* d = t->dots->add<Flex>(Flex::Direction::Row);
            pill(d)->fixedHeight = 8;
            d->fixedWidth = i == t->page ? 28 : 8;
            d->setBackground(i == t->page ? "primary" : "outline_variant", false);
        }
        t->back->setVisible(t->page > 0);
        t->skip->setVisible(t->page + 1 < kPages);
        if (t->nextLabel) t->nextLabel->setText(t->page == 0 ? "Get started" : t->page + 1 == kPages ? "Finish" : "Next");
        scroll->scrollTo(0, false);
        t->body->requestLayout();
    };
    t->show();
}

void whatsNew(Flex* card, Popup& popup, const std::string& since) {
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(14)->setAlignItems(Align::Center);
    logo(head, 48);
    auto* titles = head->add<Flex>(Flex::Direction::Column);
    titles->setSpacing(2)->setAlignItems(Align::Start);
    titles->grow = true;
    titles->add<Label>("What's new")->setSize(22)->setWeight(600);
    titles->add<Label>("Material3UI is up to date: version " M3_VERSION)->setSize(13)->setColor("on_surface_variant", false);

    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 460;
    scroll->blurEdges = true;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setPadding({0, 4, 0, 4});
    // Every release since the one last seen (a few at most), or just this one
    releaseNotes(list, since.empty() ? 1 : 4, 470, since);

    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(8)->setJustify(Align::End);
    pillButton(foot, "All release notes", false, [&popup] {
        spawn("xdg-open https://github.com/Abijspy/material3-android-shell/releases");
        popup.close();
    }, "open_in_new");
    pillButton(foot, "Got it", true, [&popup] { popup.close(); });
}

} // namespace m3::shell::panels
