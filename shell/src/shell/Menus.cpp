#include "services/Notifications.h"
#include "services/Polkit.h"
#include "services/Session.h"
#include "services/Tray.h"
#include "shell/Notify.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"

#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

// ── Notification centre ──

void notifications(Flex* card, Popup&) {
    auto* live = card->add<Live>();
    auto* head = header(live, "notifications", "Notifications");
    auto* dndLabel = head->add<Label>("Do not disturb");
    dndLabel->setSize(13)->setColor("on_surface_variant", false);
    auto* dnd = head->add<Switch>();
    dnd->onToggled = [](bool on) { Notifications::get().setDoNotDisturb(on); };

    auto* scroll = live->add<ScrollView>();
    scroll->maxHeight = 520;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(8);

    auto* footer = live->add<Flex>(Flex::Direction::Row);
    footer->setJustify(Align::End);
    auto* clear = footer->add<Button>();
    pill(clear->setBackground("secondary_container", false))->setPadding({18, 10})->setSpacing(8);
    clear->add<Icon>("clear_all", 18)->setColor("on_secondary_container", false);
    clear->add<Label>("Clear all")->setSize(14)->setWeight(500)->setColor("on_secondary_container", false);
    clear->onClicked = [](uint32_t) { Notifications::get().clear(); };

    live->sync = [=] {
        auto& n = Notifications::get();
        dnd->setChecked(n.doNotDisturb());
        list->clearChildren();
        footer->setVisible(!n.history().empty());
        if (n.history().empty()) {
            auto* empty = list->add<Flex>(Flex::Direction::Column);
            empty->setPadding({0, 32})->setSpacing(8);
            empty->add<Icon>("notifications_off", 40)->setColor("outline_variant", false);
            empty->add<Label>(n.active() ? "No notifications" : "Another notification daemon is running")
                ->setSize(14)
                ->setColor("on_surface_variant", false);
            return;
        }
        for (const auto& item : n.history()) list->add<NotificationCard>(item, false);
    };
    live->watch(Notifications::get().changed);
    live->sync();
}

// ── Power menu ──

void power(Flex* card, Popup& popup) {
    card->setAlignItems(Align::Center);
    auto* title = card->add<Label>("Power");
    title->setSize(22)->setWeight(500);
    auto* row = card->add<Flex>(Flex::Direction::Row);
    row->setSpacing(16)->setPadding({0, 8});
    struct Action {
        const char* icon;
        const char* label;
        std::function<void()> run;
    };
    const Action actions[] = {
        {"lock", "Lock", [] { if (lock) lock(); }},
        {"logout", "Log out", [] { Session::get().logout(); }},
        {"bedtime", "Suspend", [] { Session::get().suspend(); }},
        {"restart_alt", "Restart", [] { Session::get().reboot(); }},
        {"power_settings_new", "Shut down", [] { Session::get().powerOff(); }},
    };
    Button* first = nullptr;
    for (const auto& a : actions) {
        auto* col = row->add<Flex>(Flex::Direction::Column);
        col->setSpacing(8);
        auto* b = col->add<Button>();
        b->setBackground(std::string(a.icon) == "power_settings_new" ? "error_container" : "secondary_container", false)
            ->setRadius(Radii(28))
            ->setJustify(Align::Center);
        b->fixedWidth = b->fixedHeight = 72;
        b->add<Icon>(a.icon, 30)->setColor(std::string(a.icon) == "power_settings_new" ? "on_error_container" : "on_secondary_container", false);
        b->onClicked = [&popup, run = a.run](uint32_t button) {
            if (button != BTN_LEFT) return;
            popup.close();
            run();
        };
        col->add<Label>(a.label)->setSize(13)->setColor("on_surface_variant", false);
        if (!first) first = b;
    }
    (void)first;
}

// ── Tray menu (DBusMenu) ──

namespace {

void showMenu(Flex* list, Popup& popup, const std::string& key, const Tray::MenuItem& menu,
              std::shared_ptr<std::vector<Tray::MenuItem>> stack) {
    list->clearChildren();
    if (stack->size() > 1) {
        auto* back = list->add<Button>(Flex::Direction::Row);
        back->setPadding({12, 10})->setSpacing(10)->setRadius(Radii(14));
        back->add<Icon>("arrow_back", 18)->setColor("on_surface_variant", false);
        back->add<Label>(menu.label)->setSize(14)->setWeight(500);
        back->onClicked = [list, &popup, key, stack](uint32_t) {
            EventLoop::get().defer([list, &popup, key, stack] {
                stack->pop_back();
                showMenu(list, popup, key, stack->back(), stack);
            });
        };
    }
    for (const auto& item : menu.children) {
        if (!item.visible) continue;
        if (item.separator) {
            auto* sep = list->add<Flex>(Flex::Direction::Row);
            sep->setBackground("outline_variant", false);
            sep->fixedHeight = 1;
            sep->alignSelf = Align::Stretch;
            continue;
        }
        auto* b = list->add<Button>(Flex::Direction::Row);
        b->setPadding({12, 10})->setSpacing(10)->setRadius(Radii(14));
        const char* glyph = item.toggleType == "checkmark" ? (item.toggled ? "check_box" : "check_box_outline_blank")
                          : item.toggleType == "radio"     ? (item.toggled ? "radio_button_checked" : "radio_button_unchecked")
                                                           : nullptr;
        if (glyph) b->add<Icon>(glyph, 18)->setColor("primary", false);
        auto* l = b->add<Label>(item.label);
        l->setSize(14)->setMaxWidth(260)->setColor(item.enabled ? "on_surface" : "outline", false);
        l->grow = true;
        if (!item.children.empty()) b->add<Icon>("chevron_right", 18)->setColor("on_surface_variant", false);
        if (!item.enabled) continue;
        b->onClicked = [list, &popup, key, item, stack](uint32_t) {
            if (!item.children.empty()) {
                EventLoop::get().defer([list, &popup, key, item, stack] {
                    stack->push_back(item);
                    showMenu(list, popup, key, item, stack);
                });
                return;
            }
            Tray::get().menuClicked(key, item.id);
            popup.close();
        };
    }
}

} // namespace

void trayMenu(Flex* card, Popup& popup, const std::string& itemKey) {
    card->setPadding(Insets(8))->setSpacing(0);
    auto* list = card->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);
    caption(list, "Loading…");
    // The popup may close before the reply; only touch it while it lives
    auto alive = std::make_shared<bool>(true);
    struct Guard : Node {
        std::shared_ptr<bool> alive;
        ~Guard() override { *alive = false; }
    };
    card->add<Guard>()->alive = alive;
    Tray::get().fetchMenu(itemKey, [list, &popup, itemKey, alive](const Tray::MenuItem& root) {
        if (!*alive) return;
        auto stack = std::make_shared<std::vector<Tray::MenuItem>>(std::vector{root});
        showMenu(list, popup, itemKey, root, stack);
        if (root.children.empty()) caption(list, "This menu is empty");
    });
}

// ── Polkit ──

void polkit(Flex* card, Popup& popup, int id) {
    card->setSpacing(14);
    auto* live = card->add<Live>();
    auto* top = live->add<Flex>(Flex::Direction::Column);
    top->setSpacing(10);
    auto* badge = top->add<Flex>(Flex::Direction::Row);
    pill(badge->setBackground("primary_container", false))->setJustify(Align::Center);
    badge->fixedWidth = badge->fixedHeight = 56;
    badge->add<Icon>("shield_lock", 30)->setColor("on_primary_container", false);
    top->add<Label>("Authentication required")->setSize(22)->setWeight(500);
    auto* message = live->add<Label>();
    message->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(420)->setMaxLines(4);
    auto* who = live->add<Label>();
    who->setSize(13)->setColor("on_surface_variant", false);
    auto* field = live->add<TextField>();
    field->setSecret(true);
    auto* status = live->add<Label>(" ");
    status->setSize(13)->setColor("error", false)->setMaxWidth(420);
    auto* buttons = live->add<Flex>(Flex::Direction::Row);
    buttons->setSpacing(8)->setJustify(Align::End);
    auto* cancel = buttons->add<Button>();
    pill(cancel)->setPadding({20, 10});
    cancel->add<Label>("Cancel")->setSize(14)->setWeight(500)->setColor("primary", false);
    auto* ok = buttons->add<Button>();
    pill(ok->setBackground("primary", false))->setPadding({20, 10});
    ok->add<Label>("Authenticate")->setSize(14)->setWeight(500)->setColor("on_primary", false);

    auto submit = [id, field] {
        if (const auto* r = Polkit::get().find(id); r && !r->busy) Polkit::get().respond(id, field->text());
        field->setText("");
    };
    field->onSubmit = [submit](const std::string&) { submit(); };
    field->onCancel = [&popup] { popup.close(); };
    ok->onClicked = [submit](uint32_t) { submit(); };
    cancel->onClicked = [&popup](uint32_t) { popup.close(); };
    popup.initialFocus = field;

    auto* conns = live;
    conns->conns.push_back(Polkit::get().updated.connect([live, id](int changed) {
        if (changed == id) live->syncSoon();
    }));
    live->sync = [=] {
        const auto* r = Polkit::get().find(id);
        if (!r) return;
        message->setText(r->message);
        who->setText("As " + r->user + (r->action.empty() ? "" : "  ·  " + r->action));
        field->setPlaceholder(r->prompt);
        field->setSecret(!r->echo);
        status->setText(r->busy ? "Checking…" : !r->error.empty() ? r->error : !r->info.empty() ? r->info : " ");
        status->setColor(r->busy || r->error.empty() ? "on_surface_variant" : "error");
    };
    live->sync();
}

} // namespace m3::shell::panels
