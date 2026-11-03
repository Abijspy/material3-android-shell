#include "shell/bar/Status.h"

#include "shell/bar/Widgets.h"

#include "core/EventLoop.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Audio.h"
#include "services/Mpris.h"
#include "shell/PanelKit.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Privacy.h"
#include "services/Tray.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <linux/input-event-codes.h>
#include <map>
#include <numbers>
#include <tuple>

namespace m3::shell {

using namespace m3::ui;

namespace {

std::string volumeIcon() {
    const auto* s = Audio::get().sink();
    if (!s) return "volume_off";
    if (s->muted || s->volume() <= 0.001f) return "volume_off";
    return s->volume() < 0.34f ? "volume_mute" : s->volume() < 0.67f ? "volume_down" : "volume_up";
}

// A tray icon: draws the item's pixmap or themed icon, re-rendered at the
// surface scale
class TrayIcon : public Button {
public:
    TrayIcon(Tray::Item item, OpenPopup open) : m_item(std::move(item)) {
        pill(this);
        tooltip = m_item.title.empty() ? m_item.id : m_item.title;
        fixedWidth = 28;
        fixedHeight = 28;
        onClicked = [this, open](uint32_t b) {
            const int x = static_cast<int>(rect().x), y = static_cast<int>(rect().bottom());
            if (b == BTN_LEFT) Tray::get().activate(m_item.key, x, y);
            else if (b == BTN_MIDDLE) Tray::get().secondaryActivate(m_item.key, x, y);
            else if (b == BTN_RIGHT && !m_item.menuPath.empty()) open("traymenu:" + m_item.key, alongBar(this));
            else if (b == BTN_RIGHT) Tray::get().contextMenu(m_item.key, x, y);
        };
        onScrolled = [this](int d) { Tray::get().scroll(m_item.key, d * 120); };
    }

protected:
    void doPaint(Renderer& r) override {
        Button::doPaint(r);
        const int px = static_cast<int>(std::lround(18 * r.scale()));
        const Images::Texture* tex = nullptr;
        if (!m_item.pixmap.empty())
            tex = Images::get().pixmap(m_item.key + '#' + std::to_string(m_item.revision), m_item.pixmapW,
                                       m_item.pixmapH, m_item.pixmap);
        if (!tex) tex = Images::get().icon(m_item.iconName, px, m_item.iconThemePath);
        const auto& b = rect();
        if (tex) r.image(tex->id, {b.x + (b.w - 18) / 2, b.y + (b.h - 18) / 2, 18, 18});
        else r.rect({b.x + 9, b.y + 9, 10, 10}, Theme::get().color("on_surface_variant"), Radii(5));
    }

private:
    Tray::Item m_item;
};

// A Wi-Fi strength from NetworkManager's 0–100: the arcs it reaches
int wifiArcs(int strength) { return strength >= 75 ? 3 : strength >= 50 ? 2 : strength >= 25 ? 1 : 0; }

} // namespace

// ── Material 3 Expressive status glyphs ──

// Wi-Fi as Android 16 draws it: a dot under three arcs, round-capped, the
// arcs the signal doesn't reach faint. Drawn with Cairo, once per size and
// strength, as masks the theme tints
class WifiGlyph : public Node {
public:
    explicit WifiGlyph(float size) : m_size(size) {}
    // Arcs lit (0–3), or -1: nothing reached (on, not connected)
    void setLevel(int arcs) {
        if (arcs == m_arcs) return;
        m_arcs = arcs;
        requestRedraw();
    }

protected:
    SizeF doMeasure(SizeF) override { return {m_size, m_size}; }
    void doPaint(Renderer& r) override {
        const int px = static_cast<int>(std::lround(m_size * r.scale()));
        const Color fg = Theme::get().color("on_surface");
        const RectF& b = rect();
        if (const GLuint faint = mask(px, m_arcs, false)) r.mask(faint, b, fg.withAlpha(0.3f));
        if (const GLuint lit = mask(px, m_arcs, true)) r.mask(lit, b, fg);
    }

private:
    // The lit part (the dot and the arcs reached) or the faint rest
    static GLuint mask(int px, int arcs, bool lit) {
        static std::map<std::tuple<int, int, bool>, GLuint> made;
        const auto key = std::make_tuple(px, arcs, lit);
        if (auto it = made.find(key); it != made.end()) return it->second;
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        cairo_t* cr = cairo_create(surface);
        // Inside its box with a margin: the outer arc's round-capped ends
        // reach 0.707 r + stroke/2 from the centre (0.46 of the width), and
        // the whole stands centred top to bottom
        const double s = px, cx = s / 2, cy = s * 0.775, stroke = s * 0.10;
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        if (lit == (arcs >= 0)) {
            cairo_arc(cr, cx, cy, s * 0.085, 0, 2 * std::numbers::pi);
            cairo_fill(cr);
        }
        cairo_set_line_width(cr, stroke);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (int i = 0; i < 3; ++i) {
            if ((i < arcs) != lit) continue;
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, s * (0.22 + 0.18 * i), -0.75 * std::numbers::pi, -0.25 * std::numbers::pi);
            cairo_stroke(cr);
        }
        cairo_destroy(cr);
        cairo_surface_flush(surface);
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, px, px, 0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface));
        cairo_surface_destroy(surface);
        return made[key] = id;
    }

    float m_size;
    int m_arcs = 3;
};

// Children cut to its rect; it leaves them where its owner puts them
class Clip : public Node {
protected:
    SizeF doMeasure(SizeF) override { return {}; }
    void doArrange() override {}
    bool clipsChildren() const override { return true; }
};

// The charge as Android 16 draws it: a rounded body the charge fills, the
// percentage inside it in bold, a bolt while plugged in, the tip on its
// right, in batteryColor()'s colour. The number is
// drawn twice, split where the charge ends: in the bar's colour over the
// charge, in the charge's over the rest, so it reads at any level.
// Plugging in pops it (the bolt springing in, the charge flashing the
// accent); unplugging dips it (the bolt gone). While it charges
// a light sweeps along the charge every few seconds, drawn only while it
// moves (frames between sweeps would cost power for hours)
class BatteryPill : public Node {
public:
    explicit BatteryPill(bool compact) : m_compact(compact) {
        for (int i = 0; i < 2; ++i) {
            m_clip[i] = add<Clip>();
            m_text[i] = m_clip[i]->add<Flex>(Flex::Direction::Row);
            m_text[i]->setSpacing(0)->setAlignItems(Align::Center);
            m_bolt[i] = m_text[i]->add<Icon>("bolt", compact ? 9 : 11);
            m_bolt[i]->setFill(true);
            m_bolt[i]->setVisible(false);
            m_label[i] = m_text[i]->add<Label>();
            m_label[i]->setSize(compact ? 8.5f : 10)->setWeight(700);
        }
    }
    ~BatteryPill() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }
    void set(int percent, bool plugged, bool charging, const ColorRef& color) {
        m_percent = std::clamp(percent, 0, 100);
        m_color = color;
        for (int i = 0; i < 2; ++i) {
            const ColorRef c = i == 0 ? ColorRef("surface") : color;
            m_label[i]->setText(std::to_string(m_percent));
            m_label[i]->setColor(c, false);
            m_bolt[i]->setColor(c, false);
        }
        // The cable: in at once the first time, animated after that
        if (!m_known || plugged != m_plugged) {
            const bool animate = m_known;
            m_known = true;
            m_plugged = plugged;
            plugged ? plugIn(animate) : plugOut(animate);
        }
        if (charging != m_charging) {
            m_charging = charging;
            sweepSoon(charging ? 900 : 0);
        }
        requestLayout();
    }

protected:
    SizeF doMeasure(SizeF available) override {
        const SizeF t = m_text[0]->measure(available);
        return {std::max(t.w + (m_compact ? 6 : 8), m_compact ? 20.0f : 26.0f) + 3, m_compact ? 12.0f : 14.0f};
    }
    void doArrange() override {
        const RectF b = body();
        const SizeF t = m_text[0]->measure({b.w, b.h});
        const RectF at{b.x + (b.w - t.w) / 2, b.y + (b.h - t.h) / 2, t.w, t.h};
        const float filled = b.w * static_cast<float>(m_percent) / 100.0f;
        m_clip[0]->arrange({b.x, b.y, filled, b.h});
        m_clip[1]->arrange({b.x + filled, b.y, b.w - filled, b.h});
        for (auto* t2 : m_text) t2->arrange(at);
    }
    bool doAnimating() const override { return m_flash.running() || m_sweep.running(); }
    void doPaint(Renderer& r) override {
        const auto& theme = Theme::get();
        const RectF b = body();
        const Color c = m_color.resolve();
        const Radii round(m_compact ? 3.5f : 4.5f);
        r.rect(b, c.withAlpha(0.35f), round);
        const float filled = b.w * static_cast<float>(m_percent) / 100.0f;
        if (m_percent > 0) {
            r.pushClip({b.x, b.y, filled, b.h});
            r.rect(b, c, round);
            // Just plugged in: the charge lit in the accent, fading back
            if (const float f = m_flash.value(); f > 0.01f) r.rect(b, theme.color("primary").withAlpha(f), round);
            // Charging: a soft light travelling along the charge
            if (m_sweep.running()) {
                const float band = b.h * 1.4f, v = m_sweep.value();
                const float x = b.x - band + (filled + band) * v;
                r.rect({x, b.y, band, b.h}, theme.color("inverse_primary").withAlpha(0.55f * std::sin(v * std::numbers::pi_v<float>)), Radii(band / 2));
            }
            r.popClip();
        }
        const float tipH = b.h * 0.4f;
        r.rect({b.right() + 1, b.y + (b.h - tipH) / 2, 2, tipH}, m_percent == 100 ? c : c.withAlpha(0.35f), Radii(1));
    }

private:
    RectF body() const {
        const RectF& r = rect();
        return {r.x, r.y, r.w - 3, r.h};
    }
    void plugIn(bool animate) {
        for (auto* bolt : m_bolt) {
            bolt->setVisible(true);
            if (animate) bolt->setScale(0.2f, false);
            bolt->setScale(1, animate);
        }
        if (!animate) return;
        // A pop: up at once, then the bouncy spring back
        setScale(1.22f, false);
        setScale(1);
        m_flash.set(0.85f, false);
        m_flash.set(0);
        requestRedraw();
    }
    void plugOut(bool animate) {
        // The bolt gone at once: the number closing up under the dip
        for (auto* bolt : m_bolt) bolt->setVisible(false);
        if (!animate) return;
        setScale(0.82f, false);
        setScale(1);
        requestRedraw();
    }
    // The next sweep in `ms` while charging; none once it stops
    void sweepSoon(int ms) {
        if (m_timer) EventLoop::get().cancel(m_timer);
        m_timer = 0;
        if (!m_charging || motion::scale <= 0) return;
        m_timer = EventLoop::get().addTimer(ms, [this] {
            m_timer = 0;
            m_sweep.set(0, false);
            m_sweep.set(1);
            requestRedraw();
            sweepSoon(6000);
        });
    }

    bool m_compact;
    int m_percent = 100;
    bool m_known = false, m_plugged = false, m_charging = false;
    ColorRef m_color = ColorRef("on_surface");
    Clip* m_clip[2];
    Flex* m_text[2];
    Icon* m_bolt[2];
    Label* m_label[2];
    Tween m_flash{0, 700, Easing::standard()};
    Tween m_sweep{0, 1400, Easing::standard()};
    EventLoop::TimerId m_timer = 0;
};

// The battery's colour: red below 20%; yellow while it charges; green once
// charged (on the cable and no longer charging: full, or held at a charge
// limit); else the bar's, or the tertiary colour in power saver. Material
// You has no yellow or green: Material's own at tone 80 on a dark theme
// (light fills under dark digits), tone 40 on a light one
ColorRef batteryColor(int percent, bool plugged, bool charging, bool full, bool saver) {
    if (percent < 20) return ColorRef("error");
    const Color s = Theme::get().color("surface");
    const bool dark = 0.2126f * s.r + 0.7152f * s.g + 0.0722f * s.b < 0.5f;
    if (plugged && (full || !charging || percent >= 100))
        return dark ? Color{0.427f, 0.835f, 0.549f, 1} : Color{0.078f, 0.424f, 0.180f, 1}; // #6dd58c, #146c2e
    if (charging) return dark ? Color{1.0f, 0.835f, 0.31f, 1} : Color{0.478f, 0.349f, 0.0f, 1}; // #ffd54f, #7a5900
    return saver ? ColorRef("tertiary") : ColorRef("on_surface");
}

// A status glyph that answers its state flipping, on M3 Expressive's
// springs: springing in from small (on), popping large and settling (off),
// or shaking side to side (a muted speaker's "no", a bell ringing for a new
// notification). Drawn moved only while it moves
class StatusGlyph : public Icon {
public:
    explicit StatusGlyph(float size) : Icon("", size) { popOnChange = false; } // its own moves, below
    void popIn() { pop(0.35f); }
    void popOut() { pop(1.4f); }
    void shake() {
        m_shake.set(3.5f, false);
        m_shake.set(0);
        requestRedraw();
    }

protected:
    bool doAnimating() const override { return Icon::doAnimating() || m_pop.running() || m_shake.running(); }
    void doPaint(Renderer& r) override {
        const float s = m_pop.value(), dx = m_shake.value();
        const bool moved = std::abs(s - 1) > 0.001f || std::abs(dx) > 0.01f;
        if (moved) {
            const RectF& b = rect();
            r.pushTranslate(dx, 0);
            r.pushScale(b.x + b.w / 2, b.y + b.h / 2, std::max(0.0f, s));
        }
        Icon::doPaint(r);
        if (moved) {
            r.popTranslate();
            r.popTranslate();
        }
    }

private:
    void pop(float from) {
        if (motion::scale <= 0) return;
        m_pop.set(from, false);
        m_pop.set(1);
        requestRedraw();
    }
    Tween m_pop{1, Spring::bouncy()};
    Tween m_shake{0, Spring{900, 0.18f}}; // lightly damped: a few swings, gone in half a second
};

// ── StatusIcons ──

StatusIcons::StatusIcons(OpenPopup open, bool vertical) : Flex(vertical ? Direction::Column : Direction::Row) {
    setSpacing(2);
    auto make = [&](const char* popup) {
        auto* b = add<Button>();
        if (vertical) {
            pill(b)->setPadding({0, 6})->setJustify(Align::Center);
            b->fixedWidth = 28;
        } else {
            pill(b)->setPadding({6, 0});
            b->fixedHeight = 28;
        }
        b->onClicked = [this, b, open, popup](uint32_t button) {
            if (button == BTN_LEFT) open(popup, alongBar(b));
        };
        return b;
    };
    // Filled and a touch heavier, as Material 3 Expressive's status bar
    auto style = [](Icon* icon) {
        icon->setWeight(500);
        icon->setFill(true);
    };
    // The privacy chip: green while the microphone or camera is in use; a
    // tap shows the sound panel, which lists who's recording
    m_privacy = add<Button>();
    pill(m_privacy->setBackground(ColorRef(Color::fromHex("#1e8e3e").value_or(Color{0.12f, 0.56f, 0.24f, 1})), false))
        ->setPadding({6, 0})
        ->setSpacing(2)
        ->setJustify(Align::Center);
    if (vertical) m_privacy->fixedWidth = 28;
    else m_privacy->fixedHeight = 22;
    m_privacy->alignSelf = Align::Center;
    m_micDot = m_privacy->add<Icon>("mic", 14);
    m_camDot = m_privacy->add<Icon>("videocam", 14);
    for (auto* i : {m_micDot, m_camDot}) {
        i->setColor(ColorRef(Color{1, 1, 1, 1}), false);
        i->setFill(true);
    }
    m_privacy->onClicked = [this, open](uint32_t button) {
        if (button == BTN_LEFT) open("audio", alongBar(m_privacy));
    };
    m_privacy->setVisible(false);
    m_net = make("quicksettings");
    style(m_netIcon = m_net->add<StatusGlyph>(18));
    m_wifi = m_net->add<WifiGlyph>(18);
    m_bt = make("quicksettings");
    style(m_btIcon = m_bt->add<StatusGlyph>(18));
    m_vol = make("audio");
    style(m_volIcon = m_vol->add<StatusGlyph>(18));
    m_bell = make("notifications");
    style(m_bellIcon = m_bell->add<StatusGlyph>(18));
    m_vol->onScrolled = [](int d) { Audio::get().stepVolume(-0.05f * static_cast<float>(d)); };
    m_vol->onClicked = [this, open](uint32_t button) {
        if (button == BTN_LEFT) open("audio", alongBar(m_vol));
        if (button == BTN_MIDDLE) Audio::get().toggleMuted();
    };
    m_a = Network::get().changed.connect([this] { sync(); });
    m_b = Bluetooth::get().changed.connect([this] { sync(); });
    m_c = Audio::get().changed.connect([this] { sync(); });
    m_d = Notifications::get().changed.connect([this] { sync(); });
    m_e = Airplane::get().changed.connect([this] { sync(); });
    m_f = Privacy::get().changed.connect([this] { sync(); });
    sync();
}

void StatusIcons::sync() {
    // Who's listening or watching
    {
        auto& p = Privacy::get();
        const auto mic = p.micApps();
        const auto& cam = p.cameraApps();
        const bool shown = !mic.empty() || !cam.empty();
        m_micDot->setVisible(!mic.empty());
        m_camDot->setVisible(!cam.empty());
        if (shown != m_privacyShown) {
            m_privacy->setVisible(shown);
            if (shown) {
                // Springs in, so it's noticed
                m_privacy->setScale(0.4f, false);
                m_privacy->setScale(1);
            }
            m_privacyShown = shown;
        }
        auto list = [](const std::vector<std::string>& v) {
            std::string s;
            for (const auto& a : v) s += (s.empty() ? "" : ", ") + a;
            return s;
        };
        std::string tip;
        if (!mic.empty()) tip += "Microphone: " + list(mic);
        if (!cam.empty()) tip += (tip.empty() ? "" : "\n") + std::string("Camera: ") + list(cam);
        m_privacy->setTooltip(tip);
    }
    // Airplane mode: a plane. Connected over Wi-Fi, or on with nothing
    // reached: the arcs; else the glyph (Wi-Fi off, Ethernet, VPN)
    auto& nw = Network::get();
    const bool airplane = Airplane::get().on();
    const bool arcs = !airplane && nw.available() && nw.wifiEnabled() && (nw.kind() == Network::Kind::Wifi || nw.kind() == Network::Kind::None);
    m_wifi->setVisible(arcs);
    m_netIcon->setVisible(!arcs);
    if (arcs) m_wifi->setLevel(nw.kind() == Network::Kind::Wifi ? wifiArcs(nw.strength()) : -1);
    else m_netIcon->setText(airplane ? "flight" : !nw.wifiEnabled() && nw.kind() == Network::Kind::None ? "wifi_off" : nw.icon());
    m_net->setVisible(nw.available() || airplane);
    m_btIcon->setText(Bluetooth::get().icon());
    m_bt->setVisible(Bluetooth::get().available());
    m_volIcon->setText(volumeIcon());
    m_vol->setVisible(Audio::get().ready());
    auto& n = Notifications::get();
    m_bellIcon->setText(n.doNotDisturb() ? "notifications_off" : n.current().empty() ? "notifications" : "notifications_unread");
    // A state flipped since the last look: its icon answers (not at start)
    const bool btOn = Bluetooth::get().powered(), dnd = n.doNotDisturb();
    const auto* out = Audio::get().sink();
    const bool muted = !out || out->muted || out->volume() <= 0.001f;
    const size_t unread = n.current().size();
    if (m_known) {
        if (airplane != m_airplane) m_netIcon->popIn(); // the plane lands, or the signal comes back
        if (btOn != m_btOn) btOn ? m_btIcon->popIn() : m_btIcon->popOut();
        if (muted != m_muted) muted ? m_volIcon->shake() : m_volIcon->popIn();
        if (dnd != m_dnd) dnd ? m_bellIcon->popOut() : m_bellIcon->popIn();
        else if (!dnd && unread > m_unread) m_bellIcon->shake(); // a new one rings the bell
    }
    m_known = true;
    m_airplane = airplane;
    m_btOn = btOn;
    m_muted = muted;
    m_dnd = dnd;
    m_unread = unread;
    // What each says, for its tooltip
    auto& net = Network::get();
    m_net->setTooltip(airplane ? "Airplane mode"
                      : net.kind() == Network::Kind::None ? "Not connected" : net.name().empty() ? "Connected" : net.name());
    const std::string device = Bluetooth::get().connectedLabel();
    m_bt->setTooltip(!Bluetooth::get().powered() ? "Bluetooth off" : device.empty() ? "Bluetooth on" : "Bluetooth: " + device);
    const auto* sink = Audio::get().sink();
    m_vol->setTooltip(!sink ? "Sound" : sink->muted ? "Muted" : std::format("Volume {}%", std::lround(sink->volume() * 100)));
    m_bell->setTooltip(n.doNotDisturb() ? "Do not disturb" : n.current().empty() ? "No notifications"
                       : std::format("{} notification{}", n.current().size(), n.current().size() == 1 ? "" : "s"));
}

// ── BatteryStatus ──

BatteryStatus::BatteryStatus(OpenPopup open, bool vertical) : Button(vertical ? Direction::Column : Direction::Row) {
    if (vertical) {
        // Narrower, for the side bar's width
        pill(this)->setPadding({0, 8})->setJustify(Align::Center);
        fixedWidth = 28;
    } else {
        pill(this)->setPadding({10, 0})->setAlignItems(Align::Center);
        fixedHeight = 28;
    }
    onClicked = [this, open = std::move(open)](uint32_t button) {
        if (button == BTN_LEFT) open("battery", alongBar(this));
    };
    m_pill = add<BatteryPill>(vertical);
    m_conn = Power::get().changed.connect([this] { sync(); });
    // A light or dark theme picks the green and yellow tones
    m_theme = Theme::get().changed.connect([this] { sync(); });
    sync();
}

void BatteryStatus::sync() {
    auto& p = Power::get();
    setVisible(p.hasBattery());
    m_pill->set(p.percent(), p.pluggedIn(), p.charging(), batteryColor(p.percent(), p.pluggedIn(), p.charging(), p.full(), p.profile() == "power-saver"));
    setTooltip(std::format("Battery {}% · {}", p.percent(), p.status()));
}

// ── Media ──

Media::Media(bool vertical) : Button(Direction::Row) {
    pill(this)->setPadding({vertical ? 1.0f : 10.0f, 0})->setSpacing(6);
    fixedHeight = 28;
    // The play state inside a ring of the track's progress
    m_icon = add<kit::WavyRing>(26)->add<Icon>("music_note", 14);
    m_icon->setColor("primary");
    m_label = add<Label>();
    m_label->setSize(13)->setMaxWidth(240);
    m_label->setVisible(!vertical);
    onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().playPause();
        else if (b == BTN_RIGHT) Mpris::get().next();
    };
    onScrolled = [](int d) { d > 0 ? Mpris::get().next() : Mpris::get().previous(); };
    m_conn = Mpris::get().changed.connect([this] { sync(); });
    sync();
}

void Media::sync() {
    const auto* p = Mpris::get().active();
    setVisible(p && !p->title.empty());
    if (!p) return;
    m_icon->setText(p->status == "Playing" ? "graphic_eq" : "pause");
    m_label->setText(p->artist.empty() ? p->title : p->title + " · " + p->artist);
    setTooltip(m_label->text());
}

// ── TrayIcons ──

TrayIcons::TrayIcons(OpenPopup open, bool vertical) : Flex(vertical ? Direction::Column : Direction::Row), m_open(std::move(open)) {
    setSpacing(2);
    collapseWhenEmpty = true;
    m_conn = Tray::get().changed.connect([this] { sync(); });
    sync();
}

void TrayIcons::sync() {
    clearChildren();
    for (const auto& item : Tray::get().items())
        if (item.status != "Passive") add<TrayIcon>(item, m_open);
}

} // namespace m3::shell
