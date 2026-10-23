#include "shell/SettingsKit.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "services/Wallpaper.h"

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
    Connection connection; // ends with the row
    ~AliveFlag() override {
        if (alive) *alive = false;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }
};
} // namespace

void accentRows(Node* group) {
    // One choice across both rows: the ✓ moves to whichever was picked
    struct Dot {
        std::string value;
        Button* button;
        Icon* tick;
        Icon* own; // the automatic swatch's wallpaper icon
    };
    struct State {
        std::string chosen;
        std::vector<Dot> dots;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };
    auto st = std::make_shared<State>();
    st->chosen = Config::get().value<std::string>("/appearance/accent", "");
    auto mark = [st] {
        for (auto& d : st->dots) {
            const bool on = d.value == st->chosen;
            d.button->setBorder(on ? 2 : 0, "on_surface");
            d.tick->setVisible(on);
            if (d.own) d.own->setVisible(!on);
        }
    };
    auto swatch = [st, mark](Flex* line, const std::string& value, const std::string& colour, const std::string& tip) {
        auto* b = line->add<Button>();
        pill(b)->fixedWidth = 30;
        b->fixedHeight = 30;
        b->pressedRadius = 6;
        b->setJustify(Align::Center);
        b->tooltip = tip;
        Icon* own = nullptr;
        ColorRef tickColour("on_surface");
        if (colour.empty()) {
            b->setBackground("surface_container_highest", false);
            own = b->add<Icon>("wallpaper", 16);
            own->setColor("on_surface_variant", false);
        } else {
            const Color c = *Color::fromHex(colour);
            b->setBackground(ColorRef(c), false);
            // White on dark swatches, black on light ones
            const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            tickColour = ColorRef(luma < 0.5f ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1});
        }
        auto* tick = b->add<Icon>("check", 18);
        tick->setWeight(700)->setColor(tickColour, false);
        b->onClicked = [st, value, mark](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->chosen = value;
            mark();
            Config::get().set("/appearance/accent", value);
            Wallpaper::get().recolour();
        };
        st->dots.push_back({value, b, tick, own});
    };

    // The wallpaper's own colours, as Android offers them (the first: picked
    // for you, the most colourful)
    auto wall = row(group, "wallpaper", "Wallpaper colours", "Taken from your wallpaper");
    auto* wallLine = wall.row->add<Flex>(Flex::Direction::Row);
    wallLine->setSpacing(6);
    // The page may go before Matugen answers: this goes with it
    auto* flag = wall.row->add<AliveFlag>();
    flag->alive = st->alive;
    // Filled now, and again when the wallpaper changes
    auto fillWall = std::make_shared<std::function<void()>>();
    *fillWall = [st, wallLine, swatch, mark] {
        std::erase_if(st->dots, [](const Dot& d) { return d.value.empty() || d.value.starts_with("wallpaper:"); });
        wallLine->clearChildren();
        swatch(wallLine, "", "", "Picked for you");
        mark();
        Wallpaper::get().sourceColours([st, wallLine, swatch, mark, alive = st->alive](const std::vector<std::string>& colours) {
            if (!*alive) return;
            for (size_t i = 0; i < colours.size(); ++i) swatch(wallLine, "wallpaper:" + std::to_string(i), colours[i], colours[i]);
            st->chosen = Config::get().value<std::string>("/appearance/accent", "");
            mark();
            wallLine->requestLayout();
        });
    };
    (*fillWall)();
    flag->connection = Wallpaper::get().changed.connect([fillWall, alive = st->alive] {
        if (*alive) EventLoop::get().defer([fillWall, alive] {
                if (*alive) (*fillWall)();
            });
    });
    // And the basic colours, whatever the wallpaper
    auto basic = row(group, "palette", "Basic colours", "The same on any wallpaper");
    auto* basicLine = basic.row->add<Flex>(Flex::Direction::Row);
    basicLine->setSpacing(6);
    for (const char* seed : {"#6750a4", "#0061a4", "#006d3b", "#8b5000", "#b3261e", "#984061", "#006a6a"}) swatch(basicLine, seed, seed, seed);
    mark();
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

} // namespace m3::shell::panels
