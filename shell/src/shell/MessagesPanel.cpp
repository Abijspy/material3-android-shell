// Text messages from your phone (KDE Connect's SMS): the conversations,
// one opened with a reply field, and a new message to a number
#include "shell/Panels.h"

#include "core/EventLoop.h"
#include "services/KdeConnect.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cctype>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using kit::Live;

namespace {

// "14:05" today, "Mon" this week, else "3 Nov"
std::string when(int64_t ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000), now = std::time(nullptr);
    std::tm tm{}, today{};
    localtime_r(&t, &tm);
    localtime_r(&now, &today);
    char buf[32];
    if (tm.tm_yday == today.tm_yday && tm.tm_year == today.tm_year) std::strftime(buf, sizeof buf, "%H:%M", &tm);
    else if (now - t < 6 * 86400) std::strftime(buf, sizeof buf, "%a", &tm);
    else std::strftime(buf, sizeof buf, "%-d %b", &tm);
    return buf;
}

// The paired phone that can text (a connected one first): its id, "" when none
std::string textingPhone(bool* reachable = nullptr) {
    std::string away;
    for (const auto& d : KdeConnect::get().devices()) {
        if (!d.paired || !d.has("sms")) continue;
        if (d.reachable) {
            if (reachable) *reachable = true;
            return d.id;
        }
        if (away.empty()) away = d.id;
    }
    if (reachable) *reachable = false;
    return away;
}

struct View {
    enum Kind { List, Thread, New } kind = List;
    int64_t thread = 0;
    std::string title;
    std::string asked; // the phone its conversations were asked of
};

} // namespace

void messages(Flex* card, Popup& popup) {
    auto view = std::make_shared<View>();
    auto built = std::make_shared<int>(0); // each rebuild's number: a late scroll checks it's still its own
    auto* live = card->add<Live>();
    live->setSpacing(10)->setAlignItems(Align::Stretch);
    live->sync = [live, view, built, &popup] {
        live->clearChildren();
        const int mine = ++*built;
        auto& k = KdeConnect::get();
        bool here = false;
        const std::string phone = textingPhone(&here);
        auto* head = live->add<Flex>(Flex::Direction::Row);
        head->setSpacing(10);
        if (view->kind != View::List)
            kit::iconButton(head, "arrow_back", [live, view] {
                view->kind = View::List;
                live->syncSoon();
            });
        else
            head->add<Icon>("sms", 24)->setColor("primary", false);
        auto* title = head->add<Label>(view->kind == View::List ? "Messages" : view->kind == View::New ? "New message" : view->title);
        title->setSize(20)->setWeight(500)->setMaxWidth(280);
        title->grow = true;
        // The phone's conversations, asked for once it's known (it may turn up after the panel)
        if (!phone.empty() && view->asked != phone) {
            view->asked = phone;
            EventLoop::get().defer([phone] { KdeConnect::get().loadConversations(phone); });
        }
        if (phone.empty()) {
            auto* l = live->add<Label>("No phone that can send texts is paired. Pair one in Settings → Connected devices, and allow "
                                       "KDE Connect's SMS on it.");
            l->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(460)->setMaxLines(ui::kAllLines);
            return;
        }
        // Away: what's here can be read, not answered
        if (!here) {
            auto* away = live->add<Flex>(Flex::Direction::Row);
            away->setPadding({14, 10})->setSpacing(10)->setRadius(Radii(18))->setBackground("tertiary_container", false);
            away->add<Icon>("phonelink_off", 20)->setColor("on_tertiary_container", false);
            away->add<Label>("Your phone isn't connected: open KDE Connect on it, on the same network, to text")
                ->setSize(13)
                ->setMaxWidth(400)
                ->setMaxLines(ui::kAllLines)
                ->setColor("on_tertiary_container", false);
        }
        if (view->kind == View::List) {
            pillButton(head, "New", true, [live, view] {
                view->kind = View::New;
                live->syncSoon();
            }, "edit");
            const auto& all = k.conversations();
            if (all.empty()) {
                auto* l = live->add<Label>("No messages yet. If your phone has some, allow KDE Connect to read them: on the phone, KDE "
                                           "Connect → this computer → plugin settings → SMS, and give it the SMS permission.");
                l->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(460)->setMaxLines(ui::kAllLines);
                return;
            }
            auto* scroll = live->add<ScrollView>();
            scroll->maxHeight = 440;
            auto* list = scroll->add<Flex>(Flex::Direction::Column);
            list->setAlignItems(Align::Stretch)->setSpacing(2);
            for (const auto& m : all) {
                const std::string who = m.addresses.empty() ? std::string("Unknown") : k.contactName(phone, m.addresses.front());
                auto* r = list->add<Button>(Flex::Direction::Row);
                r->setPadding({10, 10})->setSpacing(12)->setRadius(Radii(18))->setAlignItems(Align::Start); // the avatar at the top of a long one
                auto* avatar = r->add<Flex>(Flex::Direction::Row);
                pill(avatar->setBackground("primary_container", false))->setJustify(Align::Center);
                avatar->fixedWidth = avatar->fixedHeight = 38;
                // The name's first letter; a person for a number not in the phone's contacts
                if (!who.empty() && std::isalpha(static_cast<unsigned char>(who[0])))
                    avatar->add<Label>(who.substr(0, 1))->setSize(16)->setWeight(600)->setColor("on_primary_container", false);
                else
                    avatar->add<Icon>("person", 20)->setColor("on_primary_container", false);
                auto* texts = r->add<Flex>(Flex::Direction::Column);
                texts->setAlignItems(Align::Stretch)->setSpacing(1);
                texts->grow = true;
                auto* line = texts->add<Flex>(Flex::Direction::Row);
                auto* name = line->add<Label>(who);
                name->setSize(14)->setWeight(m.read ? 500 : 700)->setMaxWidth(300)->setMaxLines(ui::kAllLines);
                name->grow = true;
                line->add<Label>(when(m.date))->setSize(11)->setColor("on_surface_variant", false);
                // The whole message, as many lines as it takes
                texts->add<Label>((m.sent ? "You: " : "") + m.body)
                    ->setSize(13)
                    ->setMaxWidth(380)
                    ->setMaxLines(ui::kAllLines)
                    ->setColor(m.read ? "on_surface_variant" : "on_surface", false);
                const int64_t thread = m.thread;
                r->onClicked = [live, view, thread, who, phone](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    view->kind = View::Thread;
                    view->thread = thread;
                    view->title = who;
                    KdeConnect::get().loadThread(phone, thread);
                    live->syncSoon();
                };
            }
            return;
        }
        if (view->kind == View::New) {
            auto* to = live->add<TextField>();
            to->setPlaceholder("Phone number");
            to->focus();
            auto* text = live->add<TextField>();
            text->setPlaceholder("Message");
            auto send = [live, view, to, text, phone] {
                if (to->text().empty() || text->text().empty()) return;
                KdeConnect::get().sendNew(phone, to->text(), text->text());
                view->kind = View::List;
                live->syncSoon();
            };
            text->onSubmit = [send](const std::string&) { send(); };
            auto* row = live->add<Flex>(Flex::Direction::Row);
            row->setJustify(Align::End);
            pillButton(row, "Send", true, send, "send");
            return;
        }
        // A conversation: the messages as bubbles, the reply under them
        auto* scroll = live->add<ScrollView>();
        scroll->maxHeight = 400;
        auto* list = scroll->add<Flex>(Flex::Direction::Column);
        list->setAlignItems(Align::Stretch)->setSpacing(6);
        for (const auto& m : k.thread(view->thread)) {
            auto* line = list->add<Flex>(Flex::Direction::Row);
            line->setJustify(m.sent ? Align::End : Align::Start);
            auto* bubble = line->add<Flex>(Flex::Direction::Column);
            bubble->setPadding({14, 9})->setSpacing(3)->setRadius(Radii(18));
            bubble->setBackground(m.sent ? "primary_container" : "surface_container_highest", false);
            bubble->setAlignItems(m.sent ? Align::End : Align::Start);
            bubble->add<Label>(m.body)->setSize(14)->setMaxWidth(360)->setMaxLines(ui::kAllLines)->setColor(m.sent ? "on_primary_container" : "on_surface",
                                                                                                    false);
            bubble->add<Label>(when(m.date))->setSize(10)->setColor(m.sent ? "on_primary_container" : "on_surface_variant", false);
        }
        // The newest at the bottom, once laid out (and only if this view still stands)
        EventLoop::get().addTimer(60, [scroll, built, mine, alive = live->alive()] {
            if (*alive && *built == mine) scroll->scrollTo(1e9f, false);
        });
        auto* reply = live->add<TextField>();
        reply->setPlaceholder("Reply");
        reply->focus();
        const int64_t thread = view->thread;
        reply->onSubmit = [reply, thread, phone](const std::string& t) {
            KdeConnect::get().reply(phone, thread, t);
            reply->setText("");
        };
        (void)popup;
    };
    live->watch(KdeConnect::get().changed);
    live->watch(KdeConnect::get().messagesChanged);
    live->sync();
}

} // namespace m3::shell::panels
