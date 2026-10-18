#include "shell/Panels.h"

#include "shell/PanelKit.h"

#include "core/Process.h"
#include "core/Signal.h"
#include "services/Audio.h"
#include "services/Network.h"
#include "ui/Controls.h"

#include <cmath>
#include <format>

namespace m3::shell::panels {

using namespace m3::ui;

namespace {

using kit::Live;
using kit::caption;
using kit::header;

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
    auto* live = card->add<Live>();
    auto* head = header(live, "bluetooth", "Bluetooth");
    auto* sw = head->add<Switch>();
    sw->onToggled = [](bool on) { Bluetooth::get().setPowered(on); };
    auto* list = live->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);

    live->sync = [=] {
        auto& b = Bluetooth::get();
        sw->setChecked(b.powered());
        list->clearChildren();
        if (!b.available()) {
            caption(list, "No Bluetooth adapter");
            return;
        }
        if (b.connected().empty()) {
            caption(list, b.powered() ? "No devices connected" : "Bluetooth is off");
            return;
        }
        for (const auto& name : b.connected()) {
            auto* row = list->add<Flex>(Flex::Direction::Row);
            row->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(16))->setBackground("surface_container_high", false);
            row->add<Icon>("bluetooth_connected", 20)->setColor("primary");
            row->add<Label>(name)->setSize(14)->setMaxWidth(260);
        }
    };
    live->watch(Bluetooth::get().changed);
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
