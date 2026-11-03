// The Notes panel (a to-do list) and its desktop widget's card
#include "shell/Panels.h"

#include "shell/PanelKit.h"
#include "services/Notes.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::panels {

using namespace m3::ui;
using kit::Live;

namespace {

// One item: tap to tick it off; done ones dimmed
void itemRow(Node* list, size_t i, const Notes::Item& item, bool removable) {
    auto* r = list->add<Button>(Flex::Direction::Row);
    r->setPadding({10, 8})->setSpacing(10)->setRadius(Radii(14));
    r->add<Icon>(item.done ? "check_circle" : "radio_button_unchecked", 20)
        ->setFill(item.done)
        ->setColor(item.done ? "primary" : "on_surface_variant", false);
    auto* text = r->add<Label>(item.text);
    text->setSize(14)->setMaxWidth(300)->setMaxLines(3)->setColor(item.done ? "on_surface_variant" : "on_surface", false);
    text->grow = true;
    r->onClicked = [i](uint32_t button) {
        if (button == BTN_LEFT) Notes::get().toggle(i);
    };
    if (!removable) return;
    auto* del = r->add<Button>();
    pill(del)->setPadding(Insets(4));
    del->add<Icon>("close", 16)->setColor("on_surface_variant", false);
    del->onClicked = [i](uint32_t) { Notes::get().remove(i); };
}

} // namespace

void notes(Flex* card, Popup&) {
    auto* head = kit::header(card, "checklist", "Notes");
    auto* clear = head->add<Button>();
    pill(clear->setBackground("secondary_container", false))->setPadding({12, 4});
    clear->add<Label>("Clear done")->setSize(12)->setWeight(500)->setColor("on_secondary_container", false);
    clear->onClicked = [](uint32_t) { Notes::get().clearDone(); };

    auto* field = card->add<TextField>();
    field->setPlaceholder("Add a note, then Enter");
    field->onSubmit = [field](const std::string& t) {
        Notes::get().add(t);
        field->setText("");
    };
    field->focus();

    auto* live = card->add<Live>();
    live->setSpacing(2);
    live->sync = [live, clear] {
        live->clearChildren();
        const auto& items = Notes::get().items();
        clear->setVisible(Notes::get().open() < items.size());
        if (items.empty()) {
            live->add<Label>("Nothing yet: type one above")->setSize(13)->setColor("on_surface_variant", false);
            return;
        }
        for (size_t i = 0; i < items.size(); ++i) itemRow(live, i, items[i], true);
    };
    live->watch(Notes::get().changed);
    live->sync();
}

Node* notesCard(Node* parent) {
    auto* live = parent->add<Live>();
    auto* card = live->add<Button>(Flex::Direction::Column);
    card->setPadding({16, 14})->setSpacing(6)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    card->setBackground("surface_container_high", false);
    card->squish = false;
    card->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT && open) open("notes");
    };
    live->sync = [card] {
        card->clearChildren();
        auto& n = Notes::get();
        auto* head = card->add<Flex>(Flex::Direction::Row);
        head->setSpacing(8);
        head->add<Icon>("checklist", 20)->setColor("primary", false);
        auto* title = head->add<Label>("Notes");
        title->setSize(15)->setWeight(600);
        title->grow = true;
        head->add<Label>(n.open() ? std::format("{} to do", n.open()) : std::string("All done"))
            ->setSize(12)
            ->setColor("on_surface_variant", false);
        const auto& items = n.items();
        if (items.empty()) {
            card->add<Label>("Tap to add one")->setSize(13)->setColor("on_surface_variant", false);
            return;
        }
        constexpr size_t kShown = 6;
        for (size_t i = 0; i < items.size() && i < kShown; ++i) itemRow(card, i, items[i], false);
        if (items.size() > kShown)
            card->add<Label>(std::format("+{} more", items.size() - kShown))->setSize(12)->setColor("on_surface_variant", false);
    };
    live->watch(Notes::get().changed);
    live->sync();
    return card; // what blurs behind it on the desktop
}

} // namespace m3::shell::panels
