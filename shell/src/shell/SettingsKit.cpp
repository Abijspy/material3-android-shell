#include "shell/SettingsKit.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Packages.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"

#include <algorithm>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

// A titled group of rows
Flex* group(Node* page, const std::string& title, const std::string& subtitle) {
    auto* head = page->add<Flex>(Flex::Direction::Column);
    head->setAlignItems(Align::Start)->setSpacing(2)->setPadding({8, 10, 8, 2});
    head->add<Label>(title)->setSize(14)->setWeight(600)->setColor("primary", false);
    if (!subtitle.empty()) head->add<Label>(subtitle)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(640)->setMaxLines(2);
    return page->add<Segments>();
}

// A group holding one block of controls on a single surface (popup
// layouts reused inside Settings)
Flex* panelGroup(Node* page, const std::string& title, const std::string& subtitle) {
    auto* inner = group(page, title, subtitle)->add<Flex>(Flex::Direction::Column);
    inner->setAlignItems(Align::Stretch)->setSpacing(12)->setPadding({20, 16});
    return inner;
}

void missingTool(Node* group, const std::string& icon, const std::string& title, const std::string& why,
                 std::vector<std::string> commands, std::vector<std::string> packages, std::function<void()> after) {
    auto* live = group->add<kit::Live>();
    live->collapseWhenEmpty = true;
    std::string what;
    for (size_t i = 0; i < packages.size(); ++i) what += (i == 0 ? "" : i + 1 == packages.size() ? " and " : ", ") + packages[i];
    live->sync = [live, icon, title, why, commands, packages, after, what] {
        live->clearChildren();
        if (std::ranges::all_of(commands, [](const std::string& c) { return haveCommand(c); })) return;
        auto r = row(live, icon, title, why);
        auto& p = Packages::get();
        if (p.busy()) {
            // Ours running, or another task first: one at a time
            r.row->add<Label>(p.status() == Packages::installingLabel(what) ? "Installing…" : "After the current task")
                ->setSize(13)->setColor("on_surface_variant", false);
            return;
        }
        pillButton(r.row, "Install", true, [packages, after, what] {
            Packages::get().installTools(what, packages, [after](bool ok) {
                if (ok && after) after();
            });
        }, "download");
    };
    live->watch(Packages::get().changed);
    live->sync();
}

Row row(Node* group, const std::string& icon, const std::string& title, const std::string& subtitle) {
    auto* r = group->add<Flex>(Flex::Direction::Row);
    r->setPadding({20, 12})->setSpacing(16);
    r->minHeight = 60;
    if (!icon.empty()) r->add<Icon>(icon, 22)->setColor("on_surface_variant", false);
    auto* texts = r->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    auto* t = texts->add<Label>(title);
    t->setSize(15)->setWeight(500)->setMaxWidth(420);
    auto* s = texts->add<Label>(subtitle);
    s->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(440)->setMaxLines(2);
    s->setVisible(!subtitle.empty());
    return {r, t, s};
}

namespace {
// Takes nothing, shows nothing: tells a callback whether its row still exists
class AliveFlag : public Node {
public:
    std::shared_ptr<bool> alive;
    ~AliveFlag() override {
        if (alive) *alive = false;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }
};

// Android's colour option: a circle in three, the top half the primary's
// light tone and below it the tertiary's and the primary's middle one (a
// palette at a glance); the chosen one shrinks inside a ring
class Swatch : public Button {
public:
    Swatch() {
        pill(this);
        fixedWidth = 44;
        fixedHeight = 44;
        pressedRadius = 14;
    }
    ~Swatch() override { *m_alive = false; }
    // Matugen works the colours out (a moment, the first time): the seed's
    // own colour until then
    void load(const std::string& seed, const std::string& scheme) {
        if (const auto c = Color::fromHex(seed)) m_colours = {*c, *c, *c};
        Wallpaper::swatch(seed, scheme, [this, alive = m_alive](const std::vector<Color>& colours) {
            if (!*alive || colours.size() != 3) return;
            m_colours = colours;
            requestRedraw();
        });
    }
    void setChosen(bool on, bool animate) {
        if (m_chosen.target() == (on ? 1.0f : 0.0f)) return;
        m_chosen.set(on ? 1 : 0, animate);
        requestRedraw();
    }

protected:
    void doPaint(Renderer& r) override {
        const RectF b = rect();
        const float t = std::max(0.0f, m_chosen.value());
        if (t > 0.01f) r.rect(b, Color{0, 0, 0, 0}, b.w / 2, 2, ColorRef("on_surface").resolve().withAlpha(std::min(1.0f, t)));
        const float inset = 6 * t;
        const RectF c{b.x + inset, b.y + inset, b.w - 2 * inset, b.h - 2 * inset};
        if (m_colours.size() == 3) {
            const float hw = c.w / 2, hh = c.h / 2;
            const RectF parts[3] = {{c.x, c.y, c.w, hh}, {c.x, c.y + hh, hw, hh}, {c.x + hw, c.y + hh, hw, hh}};
            for (int i = 0; i < 3; ++i) {
                r.pushClip(parts[i]);
                r.rect(c, m_colours[static_cast<size_t>(i)], c.w / 2);
                r.popClip();
            }
        }
        Button::doPaint(r); // hover and press
        if (m_chosen.running()) requestRedraw();
    }
    bool doAnimating() const override { return Button::doAnimating() || m_chosen.running(); }

private:
    std::vector<Color> m_colours;
    Tween m_chosen{0, Spring::bouncy()};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// Each of the wallpaper's colours is offered in these, as Android offers
// its four styles (the shell's own default first)
const std::vector<std::pair<std::string, std::string>> kStyles = {
    {"scheme-content", "Content"}, {"scheme-tonal-spot", "Tonal"}, {"scheme-vibrant", "Vibrant"}, {"scheme-expressive", "Expressive"}};

std::string schemeNow() {
    return Config::get().value<std::string>("/background/matugenScheme", "scheme-content");
}
std::string accentNow() {
    return Config::get().value<std::string>("/appearance/accent", "");
}
// Which of the wallpaper's colours the accent is (0, picked for you, when
// unset), or -1: a colour of its own
int wallpaperIndex(const std::string& accent) {
    if (accent.empty()) return 0;
    return accent.starts_with("wallpaper:") ? std::atoi(accent.c_str() + 10) : -1;
}
// Basic colours are shown in the scheme in use; Mono would grey them all
std::string basicScheme() {
    const auto s = schemeNow();
    return s == "scheme-monochrome" ? "scheme-tonal-spot" : s;
}

// A segment: its row, then a line of controls under it, lined up with the
// row's text
Flex* underRow(Node* group, const std::string& icon, const std::string& title, const std::string& subtitle) {
    auto* segment = group->add<Flex>(Flex::Direction::Column);
    segment->setAlignItems(Align::Stretch);
    row(segment, icon, title, subtitle);
    return segment;
}
} // namespace

void accentRows(Node* group) {
    using kit::Live;
    struct Option {
        int index = -1; // one of the wallpaper's colours (0: picked for you), or -1
        std::string seed, scheme;
        Swatch* swatch = nullptr;
    };
    struct State {
        std::vector<Option> options;
        std::vector<std::string> seeds; // the wallpaper's colours
        std::string seedsFor, wallBuilt, basicIn, styleShown;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };
    auto st = std::make_shared<State>();
    // The ring on whichever the settings name
    auto mark = [st] {
        const auto accent = accentNow(), scheme = schemeNow();
        const int index = wallpaperIndex(accent);
        for (auto& o : st->options)
            o.swatch->setChosen(o.index >= 0 ? o.index == index && o.scheme == scheme : o.seed == accent, true);
    };
    auto choose = [mark](const Option& o) {
        if (o.index >= 0) {
            // A wallpaper colour and its style, together
            Config::get().set("/background/matugenScheme", o.scheme);
            Config::get().set("/appearance/accent", o.index == 0 ? std::string() : "wallpaper:" + std::to_string(o.index));
        } else {
            if (schemeNow() == "scheme-monochrome") Config::get().set("/background/matugenScheme", std::string("scheme-tonal-spot"));
            Config::get().set("/appearance/accent", o.seed);
        }
        mark();
        Wallpaper::get().recolour();
    };
    auto add = [st, choose](Flex* line, Option o, const std::string& tip) {
        auto* s = line->add<Swatch>();
        s->load(o.seed, o.scheme);
        s->tooltip = tip;
        s->onClicked = [choose, o](uint32_t button) {
            if (button == BTN_LEFT) choose(o);
        };
        o.swatch = s;
        st->options.push_back(std::move(o));
    };
    auto lines = [](Flex* segment) {
        auto* l = segment->add<Live>();
        l->setSpacing(10)->setAlignItems(Align::Start)->setPadding({58, 0, 20, 16});
        return l;
    };

    // The wallpaper's colours, each in four styles; built again when the
    // wallpaper changes, once Matugen has looked at it
    auto* wallSegment = underRow(group, "wallpaper", "Wallpaper colours", "From your wallpaper, in four styles");
    auto* wall = lines(wallSegment);
    wallSegment->add<AliveFlag>()->alive = st->alive;
    wall->sync = [st, wall, add, mark] {
        const std::string image = Wallpaper::get().path();
        if (st->seedsFor != image) {
            st->seedsFor = image;
            Wallpaper::get().sourceColours([st, wall, image](const std::vector<std::string>& colours) {
                if (!*st->alive || st->seedsFor != image) return;
                st->seeds = colours;
                wall->syncSoon();
            });
        }
        std::string built = image;
        for (const auto& s : st->seeds) built += " " + s;
        if (built != st->wallBuilt) {
            st->wallBuilt = built;
            std::erase_if(st->options, [](const Option& o) { return o.index >= 0; });
            wall->clearChildren();
            Flex* line = nullptr;
            for (size_t i = 0; i < st->seeds.size(); ++i) {
                // Two colours a line, their styles together
                if (i % 2 == 0) (line = wall->add<Flex>(Flex::Direction::Row))->setSpacing(28);
                auto* styles = line->add<Flex>(Flex::Direction::Row);
                styles->setSpacing(8);
                for (const auto& [scheme, name] : kStyles) {
                    const bool picked = i == 0 && scheme == kStyles.front().first;
                    add(styles, {static_cast<int>(i), st->seeds[i], scheme},
                        picked ? "Picked for you" : st->seeds.size() > 1 ? std::format("{} · colour {}", name, i + 1) : name);
                }
            }
        }
        mark();
    };
    wall->watch(Wallpaper::get().changed);
    wall->watch(Config::get().changed);
    wall->sync();

    // Basic colours, the same on any wallpaper, shown in the scheme in use
    auto* basicSegment = underRow(group, "palette", "Basic colours", "The same on any wallpaper");
    auto* basic = lines(basicSegment);
    basic->sync = [st, basic, add, mark] {
        if (const auto in = basicScheme(); in != st->basicIn) {
            st->basicIn = in;
            std::erase_if(st->options, [](const Option& o) { return o.index < 0; });
            basic->clearChildren();
            auto* line = basic->add<Flex>(Flex::Direction::Row);
            line->setSpacing(8);
            for (const char* seed : {"#6750a4", "#0061a4", "#006d3b", "#8b5000", "#b3261e", "#984061", "#006a6a"}) add(line, {-1, seed, in}, seed);
        }
        mark();
    };
    basic->watch(Config::get().changed);
    basic->sync();

    // The style on its own (a wallpaper colour picks one too): rebuilt when
    // one does, so it shows it
    auto* style = group->add<Live>();
    style->setSpacing(0);
    style->sync = [st, style] {
        const auto scheme = schemeNow();
        if (scheme == st->styleShown) return;
        st->styleShown = scheme;
        style->clearChildren();
        auto r = row(style, "format_paint", "Style", "How much colour, from the wallpaper's own to none");
        connectedGroup<std::string>(r.row, scheme,
                                    {{"scheme-content", "Content"}, {"scheme-tonal-spot", "Tonal"}, {"scheme-expressive", "Expressive"},
                                     {"scheme-vibrant", "Vibrant"}, {"scheme-monochrome", "Mono"}},
                                    [st](std::string v) {
                                        st->styleShown = v; // shown already
                                        Config::get().set("/background/matugenScheme", v);
                                        Wallpaper::get().recolour();
                                    });
    };
    style->watch(Config::get().changed);
    style->sync();
}

Button* pillButton(Node* parent, const std::string& label, bool primary, std::function<void()> fn, const std::string& icon) {
    auto* b = parent->add<Button>();
    pill(b->setBackground(primary ? "primary" : "secondary_container", false))->setPadding({18, 10})->setSpacing(8);
    b->pressedRadius = 10;
    if (!icon.empty()) b->add<Icon>(icon, 18)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->add<Label>(label)->setSize(14)->setWeight(500)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT && fn) fn();
    };
    return b;
}


Button* dropdown(Node* tail, Node* g, const std::string& shown, const std::vector<std::pair<std::string, std::string>>& items,
                 std::function<void(const std::string&)> pick, std::function<Node*(Node*, const std::string&)> lead) {
    auto* drop = tail->add<Button>(Flex::Direction::Row);
    pill(drop->setBackground("surface_container_highest", false))->setPadding({14, 8})->setSpacing(6);
    drop->add<Label>(shown)->setSize(14)->setWeight(500);
    auto* chevron = drop->add<Icon>("expand_more", 18);
    auto* menu = g->add<Flex>(Flex::Direction::Column);
    menu->setPadding({20, 4, 20, 12})->setSpacing(2)->setAlignItems(Align::Stretch);
    menu->setVisible(false);
    for (const auto& [value, label] : items) {
        auto* item = menu->add<Button>(Flex::Direction::Row);
        pill(item)->setPadding({14, 8})->setSpacing(12);
        if (lead) lead(item, value);
        item->add<Label>(label)->setSize(14);
        item->onClicked = [pick, value = value](uint32_t button) {
            if (button == BTN_LEFT) pick(value);
        };
    }
    drop->onClicked = [menu, chevron](uint32_t button) {
        if (button != BTN_LEFT) return;
        menu->setVisible(!menu->visible());
        chevron->setText(menu->visible() ? "expand_less" : "expand_more");
    };
    return drop;
}

// ── Config-bound rows ──

void switchRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub,
               const std::string& pointer, bool fallback, std::function<void(bool)> after) {
    auto r = row(g, icon, title, sub);
    auto* sw = r.row->add<Switch>();
    sw->setChecked(Config::get().value<bool>(pointer, fallback), false);
    sw->onToggled = [pointer, after](bool on) {
        Config::get().set(pointer, on);
        if (after) after(on);
    };
}

void sliderRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, double fallback,
               double min, double max, double step, const std::function<std::string(double)>& format,
               std::function<void(double)> after) {
    auto r = row(g, icon, title, format(Config::get().value<double>(pointer, fallback)));
    auto* sl = r.row->add<Slider>();
    sl->fixedWidth = 240;
    sl->setRange(static_cast<float>(min), static_cast<float>(max))->setStep(static_cast<float>(step / (max - min)));
    sl->setValue(static_cast<float>(Config::get().value<double>(pointer, fallback)), false);
    auto* sub = r.subtitle;
    sub->setVisible(true);
    sl->onChanged = [sub, format, step](float v) { sub->setText(format(std::round(v / step) * step)); };
    sl->onCommitted = [pointer, step, after](float v) {
        const double snapped = std::round(v / step) * step;
        Config::get().set(pointer, snapped);
        if (after) after(snapped);
    };
}

void textRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, const std::string& fallback) {
    auto r = row(g, icon, title, "Enter to apply");
    auto* f = r.row->add<TextField>();
    f->fixedWidth = 280;
    f->setText(Config::get().value<std::string>(pointer, fallback));
    f->onSubmit = [pointer](const std::string& t) { Config::get().set(pointer, t); };
}

} // namespace m3::shell::panels
