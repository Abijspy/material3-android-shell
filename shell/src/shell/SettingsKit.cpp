#include "shell/SettingsKit.h"

#include "core/Config.h"
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

Flex* accentSwatches(Node* parent) {
    auto* swatches = parent->add<Flex>(Flex::Direction::Row);
    swatches->setSpacing(6);
    const std::vector<std::string> seeds = {"", "#6750a4", "#0061a4", "#006d3b", "#8b5000", "#b3261e", "#984061", "#006a6a"};
    auto chosen = std::make_shared<std::string>(Config::get().value<std::string>("/appearance/accent", ""));
    struct Dot {
        Button* button;
        Icon* tick;
        Icon* wallpaper; // the "from the wallpaper" swatch's own icon
    };
    auto dots = std::make_shared<std::vector<Dot>>();
    auto mark = [dots, seeds, chosen] {
        for (size_t i = 0; i < dots->size(); ++i) {
            const bool on = seeds[i] == *chosen;
            auto& d = (*dots)[i];
            d.button->setBorder(on ? 2 : 0, "on_surface");
            d.tick->setVisible(on);
            if (d.wallpaper) d.wallpaper->setVisible(!on);
        }
    };
    for (const auto& seed : seeds) {
        auto* b = swatches->add<Button>();
        pill(b)->fixedWidth = 30;
        b->fixedHeight = 30;
        b->pressedRadius = 6;
        b->setJustify(Align::Center);
        Icon* wallpaper = nullptr;
        std::string tickColour = "on_surface";
        if (seed.empty()) {
            b->setBackground("surface_container_highest", false);
            b->tooltip = "From the wallpaper";
            wallpaper = b->add<Icon>("wallpaper", 16);
            wallpaper->setColor("on_surface_variant", false);
        } else {
            const Color c = *Color::fromHex(seed);
            b->setBackground(ColorRef(c), false);
            // White on dark swatches, black on light ones
            const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            tickColour = luma < 0.5f ? "#ffffff" : "#000000";
        }
        auto* tick = b->add<Icon>("check", 18);
        tick->setWeight(700);
        if (tickColour.starts_with('#')) tick->setColor(ColorRef(*Color::fromHex(tickColour)), false);
        else tick->setColor(tickColour, false);
        b->onClicked = [seed, chosen, mark](uint32_t button) {
            if (button != BTN_LEFT) return;
            *chosen = seed;
            mark();
            Config::get().set("/appearance/accent", seed);
            Wallpaper::get().recolour();
        };
        dots->push_back({b, tick, wallpaper});
    }
    mark();
    return swatches;
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
