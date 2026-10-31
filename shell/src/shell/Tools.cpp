// Clipboard history and the screenshot tool
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Clipboard.h"
#include "services/Screenshot.h"
#include "services/Search.h"
#include "shell/ClockFaces.h"
#include "shell/PanelKit.h"
#include "shell/SettingsKit.h"
#include "shell/Panels.h"
#include "shell/bar/Bar.h"
#include "ui/Controls.h"
#include "wayland/Display.h"

#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// A picture file as a cropped thumbnail
ImageView* thumbnail(Node* parent, const std::string& path, float w, float h) {
    auto* v = parent->add<ImageView>(w, h, [path, w, h](float scale, int& tw, int& th) -> GLuint {
        auto* t = Images::get().picture(path, static_cast<int>(w * scale), static_cast<int>(h * scale), true);
        if (!t) return 0;
        tw = t->width;
        th = t->height;
        return t->id;
    });
    v->cover = true;
    return v;
}

// M3 Expressive option tile: a big icon over a label; selected turns primary
// and rounds off further
class OptionTile : public Button {
public:
    OptionTile(const std::string& icon, const std::string& label) : Button(Direction::Column) {
        setPadding({12, 16})->setSpacing(8)->setAlignItems(Align::Center)->setRadius(Radii(20));
        pressedRadius = 12;
        grow = true;
        m_icon = add<Icon>(icon, 28);
        m_label = add<Label>(label);
        m_label->setSize(13)->setWeight(500);
        set(false, false);
    }
    void set(bool on, bool animate = true) {
        setRadius(Radii(on ? 32 : 20));
        setBackground(on ? ColorRef("primary") : ColorRef("surface_container_high"), animate);
        m_icon->setFill(on)->setColor(on ? "on_primary" : "on_surface_variant", animate);
        m_label->setColor(on ? "on_primary" : "on_surface", animate);
    }

private:
    Icon* m_icon;
    Label* m_label;
};

} // namespace

void clipboard(Flex* card, Popup& popup) {
    auto& clip = Clipboard::get();
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10);
    head->add<Icon>("content_paste", 24)->setColor("primary", false);
    auto* title = head->add<Label>("Clipboard");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    auto* clear = head->add<Button>();
    pill(clear->setBackground("secondary_container", false))->setPadding({14, 8})->setSpacing(6);
    clear->pressedRadius = 8;
    clear->add<Icon>("delete_sweep", 18)->setColor("on_secondary_container", false);
    clear->add<Label>("Clear all")->setSize(13)->setWeight(500)->setColor("on_secondary_container", false);
    clear->onClicked = [](uint32_t) { Clipboard::get().wipe(); };

    auto* field = card->add<TextField>();
    field->setPlaceholder("Search the clipboard");
    popup.initialFocus = field;

    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 480;
    auto* live = scroll->add<Live>();
    live->setSpacing(4);
    auto query = std::make_shared<std::string>();
    auto first = std::make_shared<Clipboard::Entry>();

    live->sync = [live, query, first, &popup] {
        live->clearChildren();
        auto& c = Clipboard::get();
        first->line.clear();
        if (!c.available()) {
            // cliphist to install, with Install right here; else nothing yet
            missingTool(live, "content_paste_off", "Clipboard history needs cliphist", "Keeps what you copy, pictures too",
                        {"cliphist", "wl-copy", "wl-paste"}, {"cliphist", "wl-clipboard"}, [] {
                            Clipboard::get().ensureWatchers();
                            Clipboard::get().refresh();
                        });
            if (haveCommand("cliphist")) caption(live, "Nothing copied yet");
            return;
        }
        size_t shown = 0;
        for (const auto& e : c.entries()) {
            if (!query->empty() && (e.image || fuzzyScore(*query, e.preview) <= 0)) continue;
            if (shown++ == 0) *first = e;
            auto* row = live->add<Button>(Flex::Direction::Row);
            row->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(16))->setBackground("surface_container_high", false);
            row->pressedRadius = 8;
            if (e.image || !e.file.empty()) {
                // A screenshot, a copied picture or video file
                if (!e.thumb.empty()) thumbnail(row, e.thumb, 96, 56)->grow = false;
                else row->add<Icon>(e.video ? "movie" : "image", 22)->setColor("on_surface_variant", false);
                auto* l = row->add<Label>(e.image ? "Image" : std::filesystem::path(e.file).filename().string());
                l->setSize(13)->setMaxWidth(300)->setColor("on_surface_variant", false);
                l->grow = true;
            } else {
                row->add<Icon>(e.preview.starts_with("http") ? "link" : "notes", 20)->setColor("on_surface_variant", false);
                auto* l = row->add<Label>(e.preview);
                l->setSize(14)->setMaxWidth(380)->setMaxLines(2);
                l->grow = true;
            }
            iconButton(row, "close", [e] { Clipboard::get().remove(e); }, 18);
            row->onClicked = [e, &popup](uint32_t button) {
                if (button != BTN_LEFT) return;
                Clipboard::get().copy(e);
                popup.close();
            };
            if (shown >= 60) break;
        }
        if (shown == 0) caption(live, query->empty() ? "Nothing copied yet" : "No matches");
    };
    field->onChanged = [query, live](const std::string& t) {
        *query = t;
        live->syncSoon();
    };
    field->onSubmit = [first, &popup](const std::string&) {
        if (first->line.empty()) return;
        Clipboard::get().copy(*first);
        popup.close();
    };
    live->watch(clip.changed);
    live->sync();
    clip.refresh();
}

// The Screenshot tab: what to capture, a delay, what to do after, recent shots
void screenshotBody(Flex* card, Popup& popup) {
    auto& cfg = Config::get();
    // What to capture
    auto mode = std::make_shared<int>(cfg.value<int>("/screenshot/mode", 0));
    auto* modes = card->add<Flex>(Flex::Direction::Row);
    modes->setSpacing(8)->setAlignItems(Align::Stretch);
    auto tiles = std::make_shared<std::vector<OptionTile*>>();
    const std::pair<const char*, const char*> kModes[] = {{"crop", "Region"}, {"select_window", "Window"}, {"fullscreen", "Screen"}};
    for (int i = 0; i < 3; ++i) {
        auto* t = modes->add<OptionTile>(kModes[i].first, kModes[i].second);
        t->set(i == *mode, false);
        t->onClicked = [tiles, mode, i](uint32_t) {
            *mode = i;
            Config::get().set("/screenshot/mode", i);
            for (int k = 0; k < 3; ++k) (*tiles)[static_cast<size_t>(k)]->set(k == i);
        };
        tiles->push_back(t);
    }

    // Delay: a connected button group
    auto delay = std::make_shared<int>(cfg.value<int>("/screenshot/delay", 0));
    auto* delayRow = card->add<Flex>(Flex::Direction::Row);
    delayRow->setSpacing(12)->setPadding({4, 4});
    delayRow->add<Icon>("timer", 20)->setColor("on_surface_variant", false);
    auto* dl = delayRow->add<Label>("Delay");
    dl->setSize(14)->setWeight(500);
    dl->grow = true;
    auto* chips = delayRow->add<Flex>(Flex::Direction::Row);
    chips->setSpacing(2);
    auto delayButtons = std::make_shared<std::vector<std::pair<int, Button*>>>();
    auto paintDelay = [delayButtons, delay](bool animate) {
        const size_t n = delayButtons->size();
        for (size_t i = 0; i < n; ++i) {
            auto& [v, b] = (*delayButtons)[i];
            const bool on = v == *delay;
            const float l = on || i == 0 ? 100 : 8, r = on || i + 1 == n ? 100 : 8;
            b->setRadius({l, r, r, l});
            b->setBackground(on ? ColorRef("primary") : ColorRef("surface_container_highest"), animate);
            static_cast<Label*>(b->children()[0].get())->setColor(on ? "on_primary" : "on_surface", animate);
        }
    };
    for (int s : {0, 3, 5, 10}) {
        auto* b = chips->add<Button>();
        b->setPadding({12, 7});
        b->pressedRadius = 4;
        b->add<Label>(s == 0 ? "None" : std::format("{} s", s))->setSize(13)->setWeight(500);
        b->onClicked = [delay, s, paintDelay](uint32_t) {
            *delay = s;
            Config::get().set("/screenshot/delay", s);
            paintDelay(true);
        };
        delayButtons->push_back({s, b});
    }
    paintDelay(false);

    // After capture
    auto toggle = [card](const char* icon, const char* label, const char* pointer, bool fallback) {
        auto* r = card->add<Flex>(Flex::Direction::Row);
        r->setSpacing(12)->setPadding({4, 2});
        r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
        auto* l = r->add<Label>(label);
        l->setSize(14)->setWeight(500);
        l->grow = true;
        auto* sw = r->add<Switch>();
        sw->setChecked(Config::get().value<bool>(pointer, fallback), false);
        sw->onToggled = [pointer](bool on) { Config::get().set(pointer, on); };
    };
    toggle("content_copy", "Copy to clipboard", "/screenshot/copy", true);
    toggle("edit", "Open in the editor", "/screenshot/edit", false);

    // Capture: the one big action
    auto* go = card->add<Button>(Flex::Direction::Row);
    pill(go->setBackground("primary", false))->setPadding({20, 16})->setSpacing(10)->setJustify(Align::Center);
    go->pressedRadius = 16;
    go->add<Icon>("photo_camera", 22)->setFill(true)->setColor("on_primary", false);
    go->add<Label>("Capture")->setSize(16)->setWeight(600)->setColor("on_primary", false);
    const std::string output = popup.output() ? popup.output()->name : "";
    go->onClicked = [mode, delay, output, &popup](uint32_t) {
        // The panel fades out before the shot (its exit animation, then the delay)
        Screenshot::get().capture(static_cast<Screenshot::Mode>(*mode), *delay * 1000 + 450, output);
        popup.close();
    };

    // Recent shots
    const auto recent = Screenshot::get().recent(4);
    if (!recent.empty()) {
        auto* label = card->add<Label>("Recent");
        label->setSize(13)->setWeight(600)->setColor("primary", false);
        label->alignSelf = Align::Start;
        auto* strip = card->add<Flex>(Flex::Direction::Row);
        strip->setSpacing(8);
        for (const auto& f : recent) {
            auto* b = strip->add<Button>();
            b->setRadius(Radii(14))->setPadding(Insets(0));
            b->pressedRadius = 6;
            thumbnail(b, f, 98, 62);
            b->onClicked = [f, &popup](uint32_t) {
                spawn("xdg-open '" + f + "'");
                popup.close();
            };
        }
    }
}

void barWidgets(Flex* card, Popup& popup, const std::string& zone) {
    const size_t z = zone == "left" ? 0 : zone == "right" ? 2 : 1;
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10);
    head->add<Icon>("widgets", 24)->setColor("primary", false);
    auto* title = head->add<Label>("Add to the " + std::string(z == 0 ? "left" : z == 2 ? "right" : "centre"));
    title->setSize(20)->setWeight(600);
    title->grow = true;
    iconButton(head, "close", [&popup] { popup.close(); });

    const auto lay = Bar::layout();
    Flex* line = nullptr;
    size_t count = 0;
    for (const auto& m : Bar::modules()) {
        if (Bar::placed(lay, m.id)) continue;
        // Two tiles per line
        if (count++ % 2 == 0) {
            line = card->add<Flex>(Flex::Direction::Row);
            line->setSpacing(8)->setAlignItems(Align::Stretch);
        }
        auto* b = line->add<Button>(Flex::Direction::Column);
        b->setBackground("secondary_container", false)->setRadius(Radii(20))->setPadding({14, 12})->setSpacing(4);
        b->setAlignItems(Align::Start);
        b->pressedRadius = 12;
        b->grow = true;
        b->fixedWidth = 212;
        auto* top = b->add<Flex>(Flex::Direction::Row);
        top->setSpacing(8);
        top->add<Icon>(m.icon, 20)->setColor("on_secondary_container", false);
        top->add<Label>(m.name)->setSize(14)->setWeight(600)->setColor("on_secondary_container", false);
        b->add<Label>(m.about)->setSize(12)->setMaxWidth(184)->setMaxLines(2)->setColor("on_secondary_container", false);
        const std::string id = m.id;
        b->onClicked = [id, z, &popup](uint32_t) {
            auto l = Bar::layout();
            Bar::add(l, z, id);
            Bar::setLayout(l);
            popup.close();
        };
    }
    if (count == 0) caption(card, "Every widget is already on the bar.");
}

// How long the recording has run, ticking each second while shown
class RecordingTime : public Label {
public:
    RecordingTime() {
        sync();
        m_ticker = std::make_unique<SecondTicker>([this] { sync(); });
    }

private:
    void sync() {
        const long s = std::max<long>(0, std::time(nullptr) - Screenshot::get().recordingSince());
        setText(std::format("{}:{:02}", s / 60, s % 60));
    }
    std::unique_ptr<SecondTicker> m_ticker;
};

// A row of choices: an icon and a name, then a connected group
template <typename T>
void choiceLine(Node* card, const char* icon, const char* label, const char* pointer, T fallback,
                std::vector<std::pair<T, std::string>> options) {
    auto* r = card->add<Flex>(Flex::Direction::Row);
    r->setSpacing(12)->setPadding({4, 2})->setAlignItems(Align::Center);
    r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
    auto* l = r->add<Label>(label);
    l->setSize(14)->setWeight(500);
    l->grow = true;
    connectedGroup<T>(r, Config::get().value<T>(pointer, fallback), std::move(options),
                      [pointer](T v) { Config::get().set(pointer, v); });
}

// The Record tab: what to record and how, or the recording going on
void recordBody(Flex* card, Popup& popup) {
    auto& shot = Screenshot::get();
    const std::string output = popup.output() ? popup.output()->name : "";
    if (shot.recording()) {
        auto* live = card->add<Live>();
        live->setSpacing(14)->setAlignItems(Align::Center)->setPadding({0, 16});
        auto* dot = live->add<Flex>(Flex::Direction::Row);
        dot->setSpacing(10)->setAlignItems(Align::Center);
        dot->add<Icon>("radio_button_checked", 28)->setFill(true)->setColor("error", false);
        dot->add<RecordingTime>()->setSize(34)->setWeight(500);
        caption(live, "Recording. Stop here, with Super+Shift+R, or from the island");
        auto* stop = card->add<Button>(Flex::Direction::Row);
        pill(stop->setBackground("error", false))->setPadding({20, 16})->setSpacing(10)->setJustify(Align::Center);
        stop->pressedRadius = 16;
        stop->add<Icon>("stop", 22)->setFill(true)->setColor("on_error", false);
        stop->add<Label>("Stop and save")->setSize(16)->setWeight(600)->setColor("on_error", false);
        stop->onClicked = [&popup](uint32_t) {
            Screenshot::get().stopRecording();
            popup.close();
        };
        return;
    }
    // What to record
    auto mode = std::make_shared<int>(Config::get().value<int>("/recorder/mode", 0));
    auto* modes = card->add<Flex>(Flex::Direction::Row);
    modes->setSpacing(8)->setAlignItems(Align::Stretch);
    auto tiles = std::make_shared<std::vector<OptionTile*>>();
    const std::pair<const char*, const char*> kModes[] = {{"crop", "Region"}, {"select_window", "Window"}, {"fullscreen", "Screen"}};
    for (int i = 0; i < 3; ++i) {
        auto* t = modes->add<OptionTile>(kModes[i].first, kModes[i].second);
        t->set(i == *mode, false);
        t->onClicked = [tiles, mode, i](uint32_t) {
            *mode = i;
            Config::get().set("/recorder/mode", i);
            for (int k = 0; k < 3; ++k) (*tiles)[static_cast<size_t>(k)]->set(k == i);
        };
        tiles->push_back(t);
    }
    choiceLine<std::string>(card, "movie", "Format", "/recorder/format", "mp4",
                            {{"mp4", "MP4"}, {"webm", "WebM"}, {"mkv", "MKV"}, {"gif", "GIF"}});
    choiceLine<int>(card, "aspect_ratio", "Resolution", "/recorder/height", 0,
                    {{0, "Native"}, {1080, "1080p"}, {720, "720p"}, {480, "480p"}});
    choiceLine<int>(card, "speed", "Frame rate", "/recorder/fps", 60, {{24, "24"}, {30, "30"}, {60, "60"}});
    choiceLine<std::string>(card, "high_quality", "Quality", "/recorder/quality", "high",
                            {{"low", "Small"}, {"medium", "Balanced"}, {"high", "Best"}});
    choiceLine<std::string>(card, "volume_up", "Sound", "/recorder/audio", "off",
                            {{"off", "None"}, {"system", "System"}, {"mic", "Microphone"}});
    choiceLine<int>(card, "timer", "Countdown", "/recorder/delay", 0, {{0, "None"}, {3, "3 s"}, {5, "5 s"}, {10, "10 s"}});
    {
        auto* r = card->add<Flex>(Flex::Direction::Row);
        r->setSpacing(12)->setPadding({4, 2})->setAlignItems(Align::Center);
        r->add<Icon>("memory", 20)->setColor("on_surface_variant", false);
        auto* texts = r->add<Flex>(Flex::Direction::Column);
        texts->setSpacing(1)->setAlignItems(Align::Start);
        texts->grow = true;
        texts->add<Label>("Hardware encoding")->setSize(14)->setWeight(500);
        texts->add<Label>("The GPU encodes (VA-API): lighter on the CPU. MP4 and MKV")->setSize(12)->setColor("on_surface_variant", false);
        auto* sw = r->add<Switch>();
        sw->setChecked(Config::get().value<bool>("/recorder/hardware", false), false);
        sw->onToggled = [](bool on) { Config::get().set("/recorder/hardware", on); };
    }
    auto* go = card->add<Button>(Flex::Direction::Row);
    pill(go->setBackground("error", false))->setPadding({20, 16})->setSpacing(10)->setJustify(Align::Center);
    go->pressedRadius = 16;
    go->add<Icon>("radio_button_checked", 22)->setFill(true)->setColor("on_error", false);
    go->add<Label>("Start recording")->setSize(16)->setWeight(600)->setColor("on_error", false);
    go->onClicked = [output, &popup](uint32_t) {
        // The panel fades out first, then the countdown
        const int delay = Config::get().value<int>("/recorder/delay", 0) * 1000;
        Screenshot::get().startRecording(Screenshot::RecordOptions::saved(), output, delay + 450);
        popup.close();
    };
}

void screenshot(Flex* card, Popup& popup) {
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10);
    head->add<Icon>("screenshot_region", 24)->setColor("primary", false);
    auto* title = head->add<Label>("Screen capture");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    iconButton(head, "folder_open", [&popup] {
        spawn("xdg-open '" + Screenshot::get().directory() + "'");
        popup.close();
    });

    // Screenshot or Record: a connected pair, and that tab's panel under it
    auto tab = std::make_shared<int>(Screenshot::get().recording() ? 1 : Config::get().value<int>("/screenshot/tab", 0));
    auto* tabs = card->add<Flex>(Flex::Direction::Row);
    tabs->setJustify(Align::Center);
    auto* holder = card->add<Flex>(Flex::Direction::Column);
    holder->setAlignItems(Align::Stretch);
    auto fill = std::make_shared<std::function<void()>>();
    *fill = [holder, tab, &popup] {
        holder->clearChildren();
        auto* b = holder->add<Flex>(Flex::Direction::Column);
        b->setSpacing(12)->setAlignItems(Align::Stretch);
        if (*tab == 0) screenshotBody(b, popup);
        else recordBody(b, popup);
        holder->requestLayout();
    };
    connectedGroup<int>(tabs, *tab, {{0, "Screenshot"}, {1, "Record"}}, [tab, fill](int v) {
        *tab = v;
        Config::get().set("/screenshot/tab", v);
        // Not inside the click: the rebuild replaces what was clicked
        EventLoop::get().defer([fill] { (*fill)(); });
    });
    (*fill)();
}

} // namespace m3::shell::panels
