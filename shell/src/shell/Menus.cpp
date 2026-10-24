#include "services/Notifications.h"
#include "services/Polkit.h"
#include "services/Session.h"
#include "services/Tray.h"
#include "shell/Notify.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <linux/input-event-codes.h>
#include <memory>
#include <set>
#include <utility>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

// ── Notification centre ──

// As Android 16's shade: the current notifications, an app's several piled
// into one card (the newest on top, the rest peeking out under it) that
// spreads into a connected group; and the history, dismissed ones under
// Today, Yesterday and Earlier. Cards glide when others come and go, and
// swipe away.
namespace {
int g_centres = 0; // notification centres on screen
}

bool notificationCentreShown() {
    return g_centres > 0;
}

void notifications(Flex* card, Popup&) {
    struct State {
        State() { ++g_centres; }
        ~State() { --g_centres; }
        std::string tab = "current";
        std::set<std::string> open; // apps whose pile is spread out
        std::string opening;        // the one spreading right now: its cards fan out
    };
    auto st = std::make_shared<State>();
    auto* live = card->add<Live>();
    live->setSpacing(12)->setAlignItems(Align::Stretch);
    auto* head = header(live, "notifications", "Notifications");
    auto* dndLabel = head->add<Label>("Do not disturb");
    dndLabel->setSize(13)->setColor("on_surface_variant", false);
    auto* dnd = head->add<Switch>();
    dnd->onToggled = [](bool on) { Notifications::get().setDoNotDisturb(on); };
    auto* tabs = live->add<Flex>(Flex::Direction::Row);
    auto* scroll = live->add<ScrollView>();
    scroll->maxHeight = 520;
    scroll->blurEdges = true;
    auto* list = scroll->add<GlideColumn>();
    list->setAlignItems(Align::Stretch)->setSpacing(8);
    auto* footer = live->add<Flex>(Flex::Direction::Row);
    footer->setJustify(Align::End)->setSpacing(8);

    auto connect = [](Flex* f, size_t i, size_t n) {
        // A connected group: round outside, tight inside
        const float top = i == 0 ? 24 : 6, bottom = i + 1 == n ? 24 : 6;
        f->setRadius({top, top, bottom, bottom});
    };
    auto empty = [](Node* parent, const char* icon, const std::string& text) {
        auto* e = parent->add<Flex>(Flex::Direction::Column);
        e->setPadding({0, 32})->setSpacing(8);
        e->add<Icon>(icon, 40)->setColor("outline_variant", false);
        e->add<Label>(text)->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(360)->setMaxLines(2)->setCentered(true);
    };

    live->sync = [=] {
        auto& n = Notifications::get();
        dnd->setChecked(n.doNotDisturb());
        tabs->clearChildren();
        const size_t count = n.current().size();
        connectedGroup<std::string>(tabs, st->tab,
                                    {{"current", count ? std::format("Now · {}", count) : std::string("Now")}, {"history", "History"}},
                                    [st, live](std::string t) {
                                        st->tab = t;
                                        live->syncSoon();
                                    });
        list->clearChildren();
        list->setSpacing(st->tab == "current" ? 8 : 2);
        footer->clearChildren();
        const std::string opening = std::exchange(st->opening, "");

        if (st->tab == "current") {
            if (count == 0) {
                empty(list, "notifications_off", n.active() ? "No notifications" : "Another notification daemon is running");
                return;
            }
            // By app, the app with the newest first
            std::vector<std::pair<std::string, std::vector<const Notifications::Notification*>>> groups;
            for (const auto& x : n.current()) {
                const std::string app = x.app.empty() ? "Notification" : x.app;
                auto it = std::ranges::find(groups, app, &decltype(groups)::value_type::first);
                if (it == groups.end()) groups.push_back({app, {&x}});
                else it->second.push_back(&x);
            }
            for (const auto& [app, items] : groups) {
                if (items.size() == 1) {
                    list->add<NotificationCard>(*items[0], false)->glideKey = "n:" + std::to_string(items[0]->id);
                    continue;
                }
                if (!st->open.contains(app)) {
                    // A pile: the newest, the next two peeking out under it
                    auto* pile = list->add<Flex>(Flex::Direction::Column);
                    pile->setAlignItems(Align::Stretch);
                    pile->glideKey = "g:" + app;
                    auto* top = pile->add<NotificationCard>(*items[0], false, static_cast<int>(items.size() - 1));
                    top->onExpand = [st, app, live] {
                        st->open.insert(app);
                        st->opening = app;
                        live->syncSoon();
                    };
                    for (size_t i = 1; i <= std::min<size_t>(2, items.size() - 1); ++i) {
                        auto* lip = pile->add<Flex>(Flex::Direction::Row);
                        lip->setPadding({14.0f * static_cast<float>(i), 0});
                        auto* edge = lip->add<Flex>(Flex::Direction::Row);
                        edge->grow = true;
                        edge->fixedHeight = 8;
                        edge->setRadius({0, 0, 14, 14});
                        edge->setBackground(ColorRef("surface_container_highest", i == 1 ? 0.8f : 0.5f), false);
                    }
                    continue;
                }
                // Spread out: the app, a collapse chip, then the cards joined
                auto* group = list->add<Flex>(Flex::Direction::Column);
                group->setAlignItems(Align::Stretch)->setSpacing(6);
                group->glideKey = "g:" + app;
                auto* bar = group->add<Flex>(Flex::Direction::Row);
                bar->setPadding({8, 0, 0, 0})->setSpacing(8);
                auto* name = bar->add<Label>(std::format("{} · {}", app, items.size()));
                name->setSize(13)->setWeight(600)->setColor("primary", false)->setMaxWidth(300);
                name->grow = true;
                auto* fold = bar->add<Button>();
                pill(fold->setBackground("secondary_container", false))->setPadding({10, 4, 6, 4})->setSpacing(2);
                fold->add<Label>("Less")->setSize(12)->setWeight(600)->setColor("on_secondary_container", false);
                fold->add<Icon>("expand_less", 18)->setColor("on_secondary_container", false);
                fold->onClicked = [st, app, live](uint32_t) {
                    st->open.erase(app);
                    live->syncSoon();
                };
                auto* cards = group->add<Flex>(Flex::Direction::Column);
                cards->setAlignItems(Align::Stretch)->setSpacing(2);
                for (size_t i = 0; i < items.size(); ++i) {
                    auto* c = cards->add<NotificationCard>(*items[i], false);
                    connect(c, i, items.size());
                    if (app == opening && i > 0) {
                        // Fanning out of the pile: each from under the one above
                        c->setTranslateY(-64.0f * static_cast<float>(i), false);
                        c->setOpacity(0, false);
                        c->setTranslateY(0);
                        c->setOpacity(1);
                    }
                }
            }
            auto* clear = footer->add<Button>();
            pill(clear->setBackground("secondary_container", false))->setPadding({18, 10})->setSpacing(8);
            clear->add<Icon>("clear_all", 18)->setColor("on_secondary_container", false);
            clear->add<Label>("Clear all")->setSize(14)->setWeight(500)->setColor("on_secondary_container", false);
            clear->onClicked = [](uint32_t) { Notifications::get().clear(); };
            return;
        }

        // History, by day
        if (!n.keepsHistory()) {
            empty(list, "history_toggle_off", "Notification history is off: Settings → Notifications");
            return;
        }
        if (n.history().empty()) {
            empty(list, "history", "Dismissed notifications show up here");
            return;
        }
        std::tm tm{};
        const std::time_t now = std::time(nullptr);
        localtime_r(&now, &tm);
        tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
        const std::time_t today = std::mktime(&tm);
        tm.tm_mday -= 1;
        const std::time_t yesterday = std::mktime(&tm);
        const std::pair<const char*, std::function<bool(std::time_t)>> sections[] = {
            {"Today", [today](std::time_t t) { return t >= today; }},
            {"Yesterday", [today, yesterday](std::time_t t) { return t >= yesterday && t < today; }},
            {"Earlier", [yesterday](std::time_t t) { return t < yesterday; }},
        };
        size_t shown = 0;
        for (const auto& [title, in] : sections) {
            std::vector<const Notifications::Notification*> items;
            for (const auto& h : n.history())
                if (in(h.time) && shown + items.size() < 150) items.push_back(&h);
            if (items.empty()) continue;
            shown += items.size();
            auto* heading = list->add<Flex>(Flex::Direction::Row);
            heading->setPadding({8, shown == items.size() ? 0.0f : 14.0f, 0, 6});
            heading->glideKey = std::string("t:") + title;
            heading->add<Label>(title)->setSize(14)->setWeight(600)->setColor("primary", false);
            for (size_t i = 0; i < items.size(); ++i) {
                auto* row = list->add<HistoryItem>(*items[i]);
                connect(row, i, items.size());
            }
        }
        auto* clear = footer->add<Button>();
        pill(clear->setBackground("secondary_container", false))->setPadding({18, 10})->setSpacing(8);
        clear->add<Icon>("delete_sweep", 18)->setColor("on_secondary_container", false);
        clear->add<Label>("Clear history")->setSize(14)->setWeight(500)->setColor("on_secondary_container", false);
        clear->onClicked = [](uint32_t) { Notifications::get().clearHistory(); };
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
