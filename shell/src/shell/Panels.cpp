#include "shell/Panels.h"

#include "shell/PanelKit.h"

#include "core/Process.h"
#include "core/Signal.h"
#include "render/Images.h"
#include "services/Audio.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "shell/Dock.h"
#include "services/Network.h"
#include "ui/Controls.h"

#include <cmath>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::panels {

using namespace m3::ui;

namespace {

using kit::Live;
using kit::caption;
using kit::header;
using kit::iconButton;

std::string percent(float v) {
    return std::format("{}%", static_cast<int>(std::lround(v * 100)));
}

} // namespace

// ── Audio ──

void audio(Flex* card, Popup&) {
    auto* live = card->add<Live>();
    auto* head = header(live, "volume_up", "Sound");
    auto* pct = head->add<Label>();
    pct->setSize(14)->setColor("on_surface_variant", false);
    auto* unmuted = head->add<Switch>();
    unmuted->onToggled = [](bool on) { Audio::get().setMuted(!on); };

    auto* master = live->add<Slider>();
    master->setRange(0, 1);
    master->onChanged = [](float v) { Audio::get().setVolume(v); };

    // Balance, for stereo outputs
    auto* balanceBox = live->add<Flex>(Flex::Direction::Column);
    balanceBox->setAlignItems(Align::Stretch)->setSpacing(4);
    auto* balanceRow = balanceBox->add<Flex>(Flex::Direction::Row);
    balanceRow->setSpacing(8);
    balanceRow->add<Label>("L")->setSize(13)->setWeight(600)->setColor("on_surface_variant", false);
    auto* balanceText = balanceRow->add<Label>();
    balanceText->setSize(13)->setColor("on_surface_variant", false);
    balanceText->grow = true;
    balanceRow->setJustify(Align::Center);
    balanceRow->add<Label>("R")->setSize(13)->setWeight(600)->setColor("on_surface_variant", false);
    auto* balance = balanceBox->add<Slider>();
    balance->setRange(-1, 1)->setCentred(true)->setStep(0.05f);
    balance->onChanged = [](float b) { Audio::get().setBalance(std::abs(b) < 0.04f ? 0 : b); };

    // Every channel on its own
    auto* channelsBox = live->add<Flex>(Flex::Direction::Column);
    channelsBox->setAlignItems(Align::Stretch)->setSpacing(4);

    caption(live, "Output");
    auto* devices = live->add<ScrollView>();
    devices->maxHeight = 180;
    auto* deviceList = devices->add<Flex>(Flex::Direction::Column);
    deviceList->setAlignItems(Align::Stretch)->setSpacing(2);

    auto* micRow = live->add<Flex>(Flex::Direction::Row);
    micRow->setSpacing(12);
    micRow->add<Icon>("mic", 22)->setColor("primary");
    auto* mic = micRow->add<Slider>();
    mic->grow = true;
    mic->onChanged = [](float v) { Audio::get().setVolume(v, true); };
    auto* micOn = micRow->add<Switch>();
    micOn->onToggled = [](bool on) { Audio::get().setMuted(!on, true); };

    // State shared by the sync closure
    auto deviceKey = std::make_shared<std::string>();
    auto channelCount = std::make_shared<size_t>(0);

    live->sync = [=] {
        auto& a = Audio::get();
        const auto* s = a.sink();
        live->setVisible(true);
        if (!s) {
            pct->setText("No output");
            return;
        }
        pct->setText(s->muted ? "Muted" : percent(s->volume()));
        unmuted->setChecked(!s->muted);
        master->setValue(s->volume());

        const bool stereo = s->volumes.size() == 2;
        balanceBox->setVisible(stereo);
        if (stereo) {
            const float b = s->balance();
            balance->setValue(b);
            balanceText->setText(std::abs(b) < 0.01f ? "Balance · centred"
                                                      : std::format("Balance · {}% {}", static_cast<int>(std::lround(std::abs(b) * 100)),
                                                                    b < 0 ? "left" : "right"));
        }

        // Channel sliders: rebuilt only when the layout of channels changes
        if (*channelCount != s->volumes.size()) {
            *channelCount = s->volumes.size();
            channelsBox->clearChildren();
            if (s->volumes.size() > 1) {
                for (size_t i = 0; i < s->volumes.size(); ++i) {
                    auto* row = channelsBox->add<Flex>(Flex::Direction::Row);
                    row->setSpacing(8);
                    auto* name = row->add<Label>(s->channels[i]);
                    name->setSize(13)->setColor("on_surface_variant", false);
                    name->fixedWidth = 96;
                    auto* sl = row->add<Slider>();
                    sl->grow = true;
                    sl->onChanged = [i](float v) { Audio::get().setChannelVolume(i, v); };
                }
            }
        }
        for (size_t i = 0; i < channelsBox->children().size() && i < s->volumes.size(); ++i) {
            auto* row = static_cast<Flex*>(channelsBox->children()[i].get());
            static_cast<Slider*>(row->children()[1].get())->setValue(s->volumes[i]);
        }

        // Output devices: rebuilt when the set or the default changes
        std::string key;
        for (const auto& d : a.sinks()) key += d.name + (d.isDefault ? "*" : "") + ';';
        if (key != *deviceKey) {
            *deviceKey = key;
            deviceList->clearChildren();
            for (const auto& d : a.sinks()) {
                auto* b = deviceList->add<Button>(Flex::Direction::Row);
                b->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(16));
                if (d.isDefault) b->setBackground("secondary_container", false);
                const bool headphones = d.name.find("headphone") != std::string::npos ||
                                        d.description.find("Headphone") != std::string::npos;
                b->add<Icon>(headphones ? "headphones" : "speaker", 20)
                    ->setColor(d.isDefault ? "on_secondary_container" : "on_surface_variant", false);
                auto* l = b->add<Label>(d.description);
                l->setSize(14)->setMaxWidth(240)->setColor(d.isDefault ? "on_secondary_container" : "on_surface", false);
                l->grow = true;
                const std::string name = d.name;
                b->onClicked = [name](uint32_t) { Audio::get().setDefault(name); };
            }
        }

        const auto* src = a.source();
        micRow->setVisible(src != nullptr);
        if (src) {
            mic->setValue(src->volume());
            micOn->setChecked(!src->muted);
        }
    };
    live->watch(Audio::get().changed);
    live->sync();
}

// ── Network ──

void network(Flex* card, Popup&) {
    auto* live = card->add<Live>();
    auto* head = header(live, "wifi", "Wi-Fi");
    auto* sw = head->add<Switch>();
    sw->onToggled = [](bool on) { Network::get().setWifiEnabled(on); };

    auto* status = live->add<Flex>(Flex::Direction::Row);
    status->setPadding({16, 14})->setSpacing(14)->setRadius(Radii(20))->setBackground("surface_container_high", false);
    auto* icon = status->add<Icon>("", 24);
    icon->setColor("primary");
    auto* texts = status->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(2);
    texts->grow = true;
    auto* title = texts->add<Label>();
    title->setSize(15)->setWeight(500)->setMaxWidth(240);
    auto* sub = texts->add<Label>();
    sub->setSize(13)->setColor("on_surface_variant", false);

    live->sync = [=] {
        auto& n = Network::get();
        sw->setChecked(n.wifiEnabled());
        icon->setText(n.icon());
        if (!n.available()) {
            title->setText("NetworkManager is not running");
            sub->setText("Network controls need NetworkManager");
            return;
        }
        switch (n.kind()) {
        case Network::Kind::Wifi:
            title->setText(n.name());
            sub->setText(std::format("Connected · signal {}%", n.strength()));
            break;
        case Network::Kind::Ethernet:
            title->setText(n.name().empty() ? "Ethernet" : n.name());
            sub->setText("Connected by cable");
            break;
        case Network::Kind::Other:
            title->setText(n.name());
            sub->setText("Connected");
            break;
        case Network::Kind::None:
            title->setText(n.connecting() ? "Connecting…" : "Not connected");
            sub->setText(n.wifiEnabled() ? "Wi-Fi is on" : "Wi-Fi is off");
            break;
        }
    };
    live->watch(Network::get().changed);
    live->sync();
}

// ── Bluetooth ──

void bluetooth(Flex* card, Popup&) {
    bluetoothView(card, {});
}

std::string bluetoothGlyph(const std::string& icon) {
    if (icon.find("headset") != std::string::npos || icon.find("headphone") != std::string::npos) return "headphones";
    if (icon.find("audio") != std::string::npos) return "speaker";
    if (icon.find("phone") != std::string::npos) return "smartphone";
    if (icon.find("keyboard") != std::string::npos) return "keyboard";
    if (icon.find("mouse") != std::string::npos) return "mouse";
    if (icon.find("gaming") != std::string::npos) return "sports_esports";
    if (icon.find("computer") != std::string::npos) return "computer";
    if (icon.find("watch") != std::string::npos) return "watch";
    return "bluetooth";
}

namespace {

Button* actionButton(Node* parent, const std::string& label, bool primary, std::function<void()> fn) {
    auto* b = parent->add<Button>();
    pill(b->setBackground(primary ? "primary" : "surface_container_highest", false))->setPadding({18, 10});
    b->pressedRadius = 10;
    b->add<Label>(label)->setSize(14)->setWeight(500)->setColor(primary ? "on_primary" : "on_surface", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT) fn();
    };
    return b;
}

// "123456" → "123 456", easier to compare at a glance
std::string spaced(const std::string& code) {
    return code.size() == 6 ? code.substr(0, 3) + " " + code.substr(3) : code;
}

} // namespace

void bluetoothPrompt(Flex* parent) {
    using Kind = Bluetooth::Request::Kind;
    const auto r = Bluetooth::get().request(); // a copy: answering clears it
    if (r.kind == Kind::None) return;
    auto* box = parent->add<Flex>(Flex::Direction::Column);
    box->setPadding({18, 16})->setSpacing(10)->setRadius(Radii(24))->setAlignItems(Align::Stretch);
    box->setBackground("secondary_container", false);
    auto* top = box->add<Flex>(Flex::Direction::Row);
    top->setSpacing(10);
    top->add<Icon>(r.kind == Kind::Authorize ? "bluetooth_connected" : "bluetooth_searching", 22)->setColor("on_secondary_container", false);
    top->add<Label>(r.kind == Kind::Authorize ? r.name + " wants to connect" : "Pair with " + r.name)
        ->setSize(16)->setWeight(600)->setColor("on_secondary_container", false)->setMaxWidth(320);
    const std::string hint = r.kind == Kind::Confirm      ? "Check that " + r.name + " shows the same code, then pair"
                           : r.kind == Kind::ShowCode     ? "Type this code on " + r.name + ", then press Enter there"
                           : r.kind == Kind::EnterPin     ? "Enter the PIN shown on " + r.name + " (often 0000 or 1234)"
                           : r.kind == Kind::EnterPasskey ? "Enter the 6-digit code shown on " + r.name
                                                          : "Allow it to use this computer?";
    box->add<Label>(hint)->setSize(13)->setColor("on_secondary_container", false)->setMaxWidth(340)->setMaxLines(2);

    if (r.kind == Kind::Confirm || r.kind == Kind::ShowCode) {
        auto* code = box->add<Label>(spaced(r.code));
        code->setSize(40)->setWeight(600)->setColor("on_secondary_container", false);
        code->alignSelf = Align::Center;
    }
    TextField* field = nullptr;
    if (r.kind == Kind::EnterPin || r.kind == Kind::EnterPasskey) {
        field = box->add<TextField>();
        field->setPlaceholder(r.kind == Kind::EnterPasskey ? "6-digit code" : "PIN");
        field->onSubmit = [](const std::string& t) { Bluetooth::get().accept(t); };
        field->focus();
    }

    auto* buttons = box->add<Flex>(Flex::Direction::Row);
    buttons->setSpacing(8)->setJustify(Align::End);
    actionButton(buttons, r.kind == Kind::Authorize ? "Deny" : "Cancel", false, [] { Bluetooth::get().cancelPairing(); });
    if (r.kind == Kind::Confirm) actionButton(buttons, "Pair", true, [] { Bluetooth::get().accept(); });
    if (r.kind == Kind::Authorize) actionButton(buttons, "Allow", true, [] { Bluetooth::get().accept(); });
    if (field) actionButton(buttons, "Pair", true, [field] { Bluetooth::get().accept(field->text()); });
}

void bluetoothPair(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    auto shown = Bluetooth::get().showRequests();
    live->sync = [live, &popup, shown] {
        live->clearChildren();
        // Answered or cancelled: nothing left to ask
        if (Bluetooth::get().request().kind == Bluetooth::Request::Kind::None) return popup.close();
        bluetoothPrompt(live);
    };
    live->watch(Bluetooth::get().changed);
    live->sync();
}

void bluetoothView(Flex* body, const std::function<void()>& back) {
    body->clearChildren();
    auto* live = body->add<Live>();
    live->setSpacing(8);
    auto* head = live->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8);
    if (back) iconButton(head, "arrow_back", back);
    auto* title = head->add<Label>("Bluetooth");
    title->setSize(20)->setWeight(500);
    title->grow = true;
    iconButton(head, "refresh", [] { Bluetooth::get().setDiscovering(true); });
    auto* sw = head->add<Switch>();
    sw->onToggled = [](bool on) { Bluetooth::get().setPowered(on); };

    auto* prompt = live->add<Flex>(Flex::Direction::Column);
    prompt->setAlignItems(Align::Stretch);
    auto* error = live->add<Label>();
    error->setSize(13)->setColor("error", false)->setMaxWidth(380)->setMaxLines(2);
    auto* scroll = live->add<ScrollView>();
    scroll->maxHeight = 340;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);

    // Scanning and answering pairing questions last as long as the view
    auto leases = std::make_shared<std::pair<Bluetooth::Lease, Bluetooth::Lease>>(Bluetooth::get().scan(), Bluetooth::get().showRequests());
    live->sync = [=] {
        (void)leases;
        auto& b = Bluetooth::get();
        sw->setChecked(b.powered());
        prompt->clearChildren();
        bluetoothPrompt(prompt);
        error->setText(b.lastError());
        error->setVisible(!b.lastError().empty());
        list->clearChildren();
        if (!b.available()) return (void)caption(list, "No Bluetooth adapter");
        if (!b.powered()) return (void)caption(list, "Bluetooth is off");

        auto section = [&](const std::string& text) {
            list->add<Label>(text)->setSize(13)->setWeight(600)->setColor("primary", false);
        };
        auto entry = [&](const Bluetooth::Device& d) {
            auto* e = list->add<Button>(Flex::Direction::Row);
            e->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(16));
            e->pressedRadius = 10;
            if (d.connected) e->setBackground("secondary_container", false);
            const char* fg = d.connected ? "on_secondary_container" : "on_surface";
            e->add<Icon>(bluetoothGlyph(d.icon), 20)->setColor(d.connected ? fg : "on_surface_variant", false);
            auto* texts = e->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->grow = true;
            texts->add<Label>(d.name)->setSize(14)->setMaxWidth(240)->setColor(fg, false);
            const std::string busy = b.busy(d.path);
            const std::string state = !busy.empty() ? busy : d.connected ? "Connected · tap to disconnect"
                                    : d.paired ? "Paired · tap to connect" : "Tap to pair";
            texts->add<Label>(state)->setSize(12)->setColor(d.connected ? fg : "on_surface_variant", false);
            const std::string path = d.path;
            if (d.paired && busy.empty()) kit::confirmButton(e, "bt:" + path, "delete", [path] { Bluetooth::get().forget(path); });
            const bool connected = d.connected, paired = d.paired;
            e->onClicked = [path, connected, paired](uint32_t button) {
                if (button != BTN_LEFT || !Bluetooth::get().busy(path).empty()) return;
                if (connected) Bluetooth::get().disconnect(path);
                else if (paired) Bluetooth::get().connect(path);
                else Bluetooth::get().pair(path);
            };
        };

        bool any = false;
        for (const auto& d : b.devices())
            if (d.paired) {
                if (!any) section("My devices");
                any = true;
                entry(d);
            }
        section("Nearby");
        bool nearby = false;
        // Unnamed devices are mostly beacons nobody wants to pair with
        for (const auto& d : b.devices())
            if (!d.paired && d.named) {
                nearby = true;
                entry(d);
            }
        if (!nearby) caption(list, b.discovering() ? "Searching… put the device in pairing mode" : "Nothing found. Tap refresh to look again.");
    };
    live->watch(Bluetooth::get().changed);
    live->sync();
}

// ── Dock menu ──

void dockMenu(Flex* card, Popup& popup, const std::string& arg) {
    const auto nl = arg.find('|');
    const std::string id = arg.substr(0, nl), cls = nl == std::string::npos ? "" : arg.substr(nl + 1);
    auto lower = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    auto bare = [](std::string t) { return t.ends_with(".desktop") ? t.substr(0, t.size() - 8) : t; };
    const Apps::App* app = nullptr;
    for (const auto& a : Apps::get().all())
        if (a.id == id || bare(a.id) == id) app = &a;
    std::vector<std::string> windows; // their addresses
    for (const auto& c : Compositor::get().windows(true))
        if (!cls.empty() && lower(c.appId) == lower(cls)) windows.push_back(c.id);
    auto ids = Dock::pinned();
    const bool pinned = std::ranges::find(ids, id) != ids.end();

    card->setPadding(Insets(10))->setSpacing(2);
    // The app: its icon and name
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setPadding({10, 8, 10, 10})->setSpacing(12);
    const std::string icon = app ? app->icon : Apps::get().iconForClass(cls);
    head->add<ImageView>(28, [icon](float scale, int& w, int& h) -> GLuint {
        auto* t = Images::get().icon(icon, static_cast<int>(std::lround(28 * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    head->add<Label>(app ? app->name : cls)->setSize(15)->setWeight(600)->setMaxWidth(180);

    auto item = [&](const char* glyph, const std::string& label, bool danger, std::function<void()> fn) {
        auto* b = card->add<Button>(Flex::Direction::Row);
        pill(b)->setPadding({14, 10})->setSpacing(12);
        b->pressedRadius = 10;
        b->add<Icon>(glyph, 20)->setColor(danger ? "error" : "on_surface_variant", false);
        b->add<Label>(label)->setSize(14)->setColor(danger ? "error" : "on_surface", false);
        b->onClicked = [fn = std::move(fn), &popup](uint32_t button) {
            if (button != BTN_LEFT) return;
            fn();
            popup.close();
        };
    };
    if (app) {
        const Apps::App copy = *app;
        item(windows.empty() ? "open_in_new" : "add", windows.empty() ? "Open" : "New window", false,
             [copy] { Apps::get().launch(copy); });
    }
    item(pinned ? "keep_off" : "keep", pinned ? "Unpin from dock" : "Pin to dock", false, [id, pinned] {
        auto now = Dock::pinned();
        if (pinned)
            std::erase(now, id);
        else
            now.push_back(id);
        Dock::setPinned(now);
    });
    if (!windows.empty())
        item("close", windows.size() == 1 ? "Close window" : std::format("Close all {} windows", windows.size()), true, [windows] {
            for (const auto& a : windows) Compositor::get().closeWindow(a);
        });
}

// ── Battery ──

void battery(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(10);
    live->sync = [live, &popup] {
        live->clearChildren();
        auto& p = Power::get();
        if (p.hasBattery()) {
            const bool low = p.percent() <= 15 && !p.charging();
            auto* top = live->add<Flex>(Flex::Direction::Row);
            top->setSpacing(12);
            top->add<Icon>(p.icon(), 32)->setFill(true)->setColor(low ? "error" : "primary", false);
            auto* texts = top->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->add<Label>(std::to_string(p.percent()) + "%")->setSize(28)->setWeight(600);
            texts->add<Label>(p.status())->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(280);
            live->add<kit::Meter>(static_cast<float>(p.percent()) / 100.0f,
                                  ColorRef(low ? "error" : p.charging() ? "tertiary" : "primary"), 10);
            // Draw and health, when the battery reports them
            std::string facts;
            if (p.watts() > 0) facts = std::format("{} {:.1f} W", p.charging() ? "Charging at" : "Using", p.watts());
            if (p.health() > 0) facts += (facts.empty() ? "" : "  ·  ") + std::format("Health {}%", p.health());
            if (!facts.empty()) caption(live, facts);
        } else {
            header(live, "power", "Power");
        }

        if (p.profiles().empty()) {
            caption(live, "Power modes need power-profiles-daemon");
        } else {
            // One tonal button per mode, the active one filled
            auto* modes = live->add<Flex>(Flex::Direction::Row);
            modes->setSpacing(6)->setAlignItems(Align::Stretch);
            const std::tuple<const char*, const char*, const char*> defs[] = {
                {"power-saver", "eco", "Saver"}, {"balanced", "balance", "Balanced"}, {"performance", "bolt", "Performance"}};
            for (const auto& [id, icon, label] : defs) {
                if (std::ranges::find(p.profiles(), id) == p.profiles().end()) continue;
                const bool on = p.profile() == id;
                auto* b = modes->add<Button>(Flex::Direction::Column);
                b->setPadding({8, 12})->setSpacing(4)->setRadius(Radii(on ? 20 : 14))->setJustify(Align::Center);
                b->setBackground(on ? ColorRef("primary") : ColorRef("on_surface", 0.1f), false);
                b->pressedRadius = 10;
                // Equal thirds of the card (380 − padding 40 − gaps 12), whatever the label
                b->fixedWidth = (340.0f - 12) / 3;
                b->add<Icon>(icon, 22)->setFill(on)->setColor(on ? "on_primary" : "on_surface_variant", false);
                b->add<Label>(label)->setSize(13)->setWeight(on ? 600 : 500)->setColor(on ? "on_primary" : "on_surface", false);
                b->onClicked = [profile = std::string(id)](uint32_t button) {
                    if (button == BTN_LEFT) Power::get().setProfile(profile);
                };
            }
        }

        auto* more = live->add<Button>(Flex::Direction::Row);
        pill(more)->setPadding({12, 8})->setSpacing(8);
        more->alignSelf = Align::End;
        more->add<Label>("Battery settings")->setSize(13)->setWeight(500)->setColor("primary", false);
        more->add<Icon>("arrow_forward", 16)->setColor("primary", false);
        more->onClicked = [&popup](uint32_t button) {
            if (button != BTN_LEFT) return;
            popup.close();
            if (open) open("settings battery");
        };
    };
    live->watch(Power::get().changed);
    live->sync();
}

// ── Run ──

void run(Flex* card, Popup& popup) {
    header(card, "terminal", "Run");
    auto* field = card->add<TextField>();
    field->setPlaceholder("Command, e.g. alacritty");
    field->onSubmit = [&popup](const std::string& cmd) {
        if (!cmd.empty()) spawn(cmd);
        popup.close();
    };
    field->onCancel = [&popup] { popup.close(); };
    popup.initialFocus = field;
}

} // namespace m3::shell::panels
