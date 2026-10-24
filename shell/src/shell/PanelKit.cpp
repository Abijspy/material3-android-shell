#include "shell/PanelKit.h"

#include "core/Config.h"

#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Mpris.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::kit {

using namespace m3::ui;

Live::Live() : Flex(Direction::Column) {
    setAlignItems(Align::Stretch)->setSpacing(12);
}

Live::~Live() {
    *m_alive = false;
}

void Live::watch(Signal<>& s) {
    conns.push_back(s.connect([this] { syncSoon(); }));
}

void Live::syncSoon() {
    if (m_pending) return;
    m_pending = true;
    EventLoop::get().defer([this, alive = m_alive] {
        if (!*alive) return;
        m_pending = false;
        if (sync) sync();
    });
}

Flex* header(Node* parent, const std::string& icon, const std::string& title) {
    auto* row = parent->add<Flex>(Flex::Direction::Row);
    row->setSpacing(12);
    row->add<Icon>(icon, 24)->setColor("primary");
    auto* t = row->add<Label>(title);
    t->setSize(20)->setWeight(500);
    t->grow = true;
    return row;
}

Label* caption(Node* parent, const std::string& text) {
    auto* l = parent->add<Label>(text);
    l->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
    l->alignSelf = Align::Start;
    return l;
}

Meter::Meter(float value, ColorRef color, float height)
    : m_value(std::clamp(value, 0.0f, 1.0f)), m_height(height), m_color(std::move(color)) {}

void Meter::doPaint(Renderer& r) {
    const Radii round(m_height / 2);
    r.rect(rect(), ColorRef("surface_container_highest").resolve(), round);
    RectF fill = rect();
    fill.w *= m_value;
    if (fill.w > 0) r.rect(fill, m_color.resolve(), round);
}

// ── Loading indicators ──

namespace {
double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void WavyLoader::doPaint(Renderer& r) {
    const auto& b = rect();
    const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const float stroke = std::max(3.0f, m_size / 10), radius = m_size / 2 - stroke / 2 - 1.5f;
    const Color c = Theme::get().color("primary");
    const double t = seconds();
    constexpr float tau = 2 * static_cast<float>(M_PI);
    // The arc breathes between a short and a long sweep while the whole
    // thing turns, as Android's indeterminate indicator
    const float breath = 0.5f - 0.5f * std::cos(tau * static_cast<float>(std::fmod(t / 1.4, 1.0)));
    const float sweep = tau * (0.22f + 0.5f * breath);
    const float start = static_cast<float>(std::fmod(t * 1.1, 1.0)) * tau + breath * tau * 0.25f;
    const float step = 0.9f / radius; // about a pixel apart
    // A faint track keeps the circle readable when the arc is short
    const Color track = Theme::get().color("on_surface").withAlpha(0.14f);
    const float gap = (stroke + 3) / radius;
    for (float a = start + sweep + gap; a <= start + tau - gap; a += step)
        r.rect({cx + radius * std::cos(a) - stroke / 2, cy + radius * std::sin(a) - stroke / 2, stroke, stroke}, track,
               Radii(stroke / 2));
    // The wave rides along the arc: small and tight, so it stays a circle
    const float phase = static_cast<float>(std::fmod(t * 5.0, tau));
    const float waves = std::round(m_size / 3), amp = m_size / 36;
    for (float a = start; a <= start + sweep; a += step) {
        const float rr = radius - amp + amp * std::sin(waves * a - phase);
        const float x = cx + rr * std::cos(a), y = cy + rr * std::sin(a);
        r.rect({x - stroke / 2, y - stroke / 2, stroke, stroke}, c, Radii(stroke / 2));
    }
    // More frames come from doAnimating, at the ambient rate
}

void WavyBar::doPaint(Renderer& r) {
    const auto& b = rect();
    const float cy = b.y + b.h / 2, stroke = 4, gap = 6;
    const Color active = Theme::get().color("primary");
    const Color track = Theme::get().color("on_surface").withAlpha(0.18f);
    const double t = seconds();
    // A stretch a third as wide as the bar enters on the left and leaves on
    // the right, easing in the middle
    const float cycle = static_cast<float>(std::fmod(t / 1.8, 1.0));
    const float eased = cycle < 0.5f ? 2 * cycle * cycle : 1 - 2 * (1 - cycle) * (1 - cycle);
    const float len = b.w * 0.34f;
    const float from = b.x - len + eased * (b.w + len), to = from + len;
    const float a = std::max(from, b.x + stroke / 2), z = std::min(to, b.right() - stroke / 2);
    // The track on either side, with a gap around the wave
    auto flat = [&](float x0, float x1) {
        if (x1 - x0 > stroke) r.rect({x0, cy - stroke / 2, x1 - x0, stroke}, track, Radii(stroke / 2));
    };
    flat(b.x, a - gap);
    flat(z + gap, b.right());
    const float phase = static_cast<float>(std::fmod(t * 6.0, 2 * M_PI));
    constexpr float wavelength = 26, amp = 3;
    for (float x = a; x <= z; x += 1.5f) {
        const float y = cy + amp * std::sin(2 * static_cast<float>(M_PI) * (x - b.x) / wavelength - phase);
        r.rect({x - stroke / 2, y - stroke / 2, stroke, stroke}, active, Radii(stroke / 2));
    }
}

WavyRing::WavyRing(float size) : Flex(Direction::Row), m_size(size) {
    setJustify(Align::Center);
    fixedWidth = fixedHeight = size;
}

void WavyRing::doPaint(Renderer& r) {
    Flex::doPaint(r);
    const auto* p = Mpris::get().active();
    const bool playing = p && p->status == "Playing";
    if (playing != m_playing) {
        m_playing = playing;
        m_amp.set(playing ? 1.0f : 0.0f);
    }
    const float f = p && p->length > 0
                        ? std::clamp(static_cast<float>(static_cast<double>(p->positionNow()) / static_cast<double>(p->length)), 0.0f, 1.0f)
                        : 0.0f;
    const auto& b = rect();
    const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const float stroke = std::max(2.5f, m_size / 11), amp = m_size / 40 * m_amp.value();
    const float radius = m_size / 2 - stroke / 2 - m_size / 40;
    constexpr float tau = 2 * static_cast<float>(M_PI), top = -static_cast<float>(M_PI) / 2;
    const float step = 0.9f / radius, gap = (stroke + 3) / radius;
    const float end = top + f * tau;
    auto dot = [&](float a, float rr, const Color& c) {
        r.rect({cx + rr * std::cos(a) - stroke / 2, cy + rr * std::sin(a) - stroke / 2, stroke, stroke}, c, Radii(stroke / 2));
    };
    // The rest of the track, flat and faint, with gaps at both ends
    const Color track = Theme::get().color("on_surface").withAlpha(0.22f);
    if (f < 0.98f)
        for (float a = end + gap; a <= top + tau - (f > 0.02f ? gap : 0); a += step) dot(a, radius, track);
    // The played arc
    const Color active = Theme::get().color("primary");
    const float phase = static_cast<float>(std::fmod(seconds() * 5.0, tau));
    const float waves = std::max(8.0f, std::round(m_size / 2.5f)); // small rings: small, tight waves
    for (float a = top; a <= end; a += step) dot(a, radius + amp * std::sin(waves * a - phase), active);
}

// ── WavyProgress ──

void WavyProgress::set(float value, bool playing) {
    m_value = std::clamp(value, 0.0f, 1.0f);
    if (playing != m_playing) {
        m_playing = playing;
        m_amp.set(playing ? 1.0f : 0.0f);
    }
    requestRedraw();
}

void WavyProgress::onPress(uint32_t button, float x, float) {
    if (button != BTN_LEFT || rect().w <= 0) return;
    m_drag = std::clamp((x - rect().x) / rect().w, 0.0f, 1.0f);
    requestRedraw();
}

void WavyProgress::onDrag(float x, float) {
    if (m_drag < 0) return;
    m_drag = std::clamp((x - rect().x) / rect().w, 0.0f, 1.0f);
    requestRedraw();
}

void WavyProgress::onRelease() {
    if (m_drag < 0) return;
    const float at = m_drag;
    m_drag = -1;
    m_value = at;
    if (onSeek) onSeek(at);
    requestRedraw();
}

void WavyProgress::doPaint(Renderer& r) {
    const auto& b = rect();
    const float cy = b.y + b.h / 2, stroke = 4, gap = 6;
    const float f = m_drag >= 0 ? m_drag : m_value;
    const float split = b.x + f * b.w;
    const Color active = Theme::get().color("primary");
    const Color rest = Theme::get().color("on_surface").withAlpha(0.25f);
    // The wave: dots along a sine that travels while playing
    const float amp = 3.5f * m_amp.value();
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float phase = static_cast<float>(std::fmod(t * 4.0, 2 * M_PI));
    constexpr float wavelength = 28;
    const float end = split - gap;
    for (float x = b.x + stroke / 2; x <= end; x += 1.5f) {
        const float y = cy + amp * std::sin(2 * static_cast<float>(M_PI) * (x - b.x) / wavelength - phase);
        r.rect({x - stroke / 2, y - stroke / 2, stroke, stroke}, active, Radii(stroke / 2));
    }
    // The rest: a straight track and a stop dot at its end
    const float from = split + gap;
    if (from < b.right()) r.rect({from, cy - stroke / 2, b.right() - from, stroke}, rest, Radii(stroke / 2));
    r.rect({b.right() - stroke, cy - stroke / 2, stroke, stroke}, active, Radii(stroke / 2));
    // The handle where playback is
    r.rect({split - 2, cy - 10, 4, 20}, active, Radii(2));
}

// ── MediaCard ──

namespace {
std::string clockText(long long us) {
    const long long s = std::max(0LL, us / 1000000);
    return s >= 3600 ? std::format("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60) : std::format("{}:{:02}", s / 60, s % 60);
}
} // namespace

MediaCard::MediaCard(bool compact) : Flex(Direction::Column) {
    setPadding(Insets(compact ? 16 : 20))->setSpacing(compact ? 6 : 10)->setAlignItems(Align::Stretch)->setRadius(Radii(28));
    setBackground(ColorRef("on_surface", 0.1f), false);
    // Compact: one row of text and controls; full: the text stacked above them
    Flex* textHost = this;
    Flex* topRow = nullptr;
    if (compact) {
        topRow = add<Flex>(Direction::Row);
        topRow->setSpacing(8);
        textHost = topRow->add<Flex>(Direction::Column);
        textHost->setAlignItems(Align::Start);
        textHost->grow = true;
    }
    m_app = textHost->add<Label>();
    m_app->setSize(12)->setWeight(500)->setColor("on_surface_variant", false);
    // One line that scrolls when it's longer than the card (a long title
    // wrapped and ran past the card's edge)
    m_title = textHost->add<Marquee>(compact ? 200 : 300, "");
    m_title->setMode(Marquee::modeOf(Config::get().value<std::string>("/bar/scrolling/islandMedia", "loop")))
        ->setSpeed(Config::get().value<float>("/bar/scrolling/speed", 30));
    m_title->label()->setSize(compact ? 16 : 20)->setWeight(600);
    m_artist = textHost->add<Label>();
    m_artist->setSize(compact ? 13 : 14)->setColor("on_surface_variant", false)->setMaxWidth(compact ? 200 : 340);
    if (compact) m_app->setVisible(false);
    m_progressBox = add<Flex>(Direction::Column);
    m_progressBox->setAlignItems(Align::Stretch)->setSpacing(2);
    m_progress = m_progressBox->add<WavyProgress>();
    m_progress->onSeek = [](float f) { Mpris::get().seek(f); };
    auto* times = m_progressBox->add<Flex>(Direction::Row);
    m_elapsed = times->add<Label>();
    m_elapsed->setSize(12)->setColor("on_surface_variant", false);
    m_elapsed->grow = true;
    m_left = times->add<Label>();
    m_left->setSize(12)->setColor("on_surface_variant", false);

    auto* controls = compact ? topRow->add<Flex>(Direction::Row) : add<Flex>(Direction::Row);
    controls->setSpacing(compact ? 4 : 20)->setJustify(Align::Center);
    m_prev = iconButton(controls, "skip_previous", [] { Mpris::get().previous(); }, compact ? 24 : 28);
    // Play: a circle while paused, a rounded square while playing
    m_play = controls->add<Button>(Direction::Row);
    m_play->setJustify(Align::Center)->setBackground("primary", false);
    m_play->fixedWidth = m_play->fixedHeight = compact ? 52 : 64;
    m_compact = compact;
    m_play->pressedRadius = 12;
    m_playIcon = m_play->add<Icon>("play_arrow", 32);
    m_playIcon->setFill(true)->setColor("on_primary", false);
    m_play->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT) Mpris::get().playPause();
    };
    m_next = iconButton(controls, "skip_next", [] { Mpris::get().next(); }, compact ? 24 : 28);

    m_conn = Mpris::get().changed.connect([this] { sync(); });
    m_lease = Mpris::get().watchPosition();
    m_tick = EventLoop::get().addTimer(1000, [this] { sync(); }, true);
    sync();
}

void MediaCard::doArrange() {
    // The room inside the padding, beside the controls when compact
    const float room = std::max(80.0f, rect().w - (m_compact ? 32 + 160 : 40));
    if (std::abs(room - m_room) > 0.5f) {
        m_room = room;
        m_title->setMaxWidth(room);
        m_artist->setMaxWidth(room);
    }
    Flex::doArrange();
}

MediaCard::~MediaCard() {
    *m_alive = false;
    if (m_tick) EventLoop::get().cancel(m_tick);
}

void MediaCard::sync() {
    auto& m = Mpris::get();
    const auto* p = m.active();
    m_app->setText(p ? p->identity : "Media");
    m_title->setText(p && !p->title.empty() ? p->title : "Nothing playing");
    m_artist->setText(p ? (p->artist.empty() ? p->album : p->artist) : "Start music in any player");
    m_artist->setVisible(!m_artist->text().empty());
    const bool playing = p && p->status == "Playing";
    const bool timed = p && p->length > 0;
    m_progressBox->setVisible(timed);
    if (timed) {
        const long long at = p->positionNow();
        m_progress->set(static_cast<float>(static_cast<double>(at) / static_cast<double>(p->length)), playing);
        m_progress->onSeek = p->canSeek ? std::function<void(float)>([](float f) { Mpris::get().seek(f); }) : nullptr;
        m_elapsed->setText(clockText(at));
        m_left->setText("-" + clockText(p->length - at));
    }
    m_playIcon->setText(playing ? "pause" : "play_arrow");
    const float size = m_compact ? 52 : 64;
    m_play->setRadius(Radii(playing ? size * 0.3f : size / 2));
    m_prev->setOpacity(p && p->canPrevious ? 1.0f : 0.4f);
    m_next->setOpacity(p && p->canNext ? 1.0f : 0.4f);
    const std::string art = p ? m.artPath(*p) : "";
    if (art != m_art) {
        m_art = art;
        requestRedraw();
    }
}

void MediaCard::doPaint(Renderer& r) {
    if (m_art.empty()) return Flex::doPaint(r);
    // The cover fills the card, under a tint that keeps the text readable
    const auto& b = rect();
    const Radii radii = effectiveRadius();
    const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
    if (auto* t = Images::get().picture(m_art, w, h, true, [this, alive = m_alive] {
            if (*alive) requestRedraw();
        }))
        r.image(t->id, b, 1, radii);
    r.rect(b, ColorRef("surface", 0.62f).resolve(), radii);
}

// ── AvatarView ──

AvatarView::AvatarView(std::string picture, const std::string& name, float size) : Flex(Direction::Row) {
    std::error_code ec;
    if (!picture.empty() && std::filesystem::exists(picture, ec)) m_picture = std::move(picture);
    pill(this)->setJustify(Align::Center)->setBackground("secondary_container", false);
    fixedWidth = fixedHeight = size;
    if (m_picture.empty()) {
        // The first letter, upper-cased (ASCII; other scripts keep theirs)
        std::string initial = name.empty() ? "?" : name.substr(0, 1);
        initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        add<Label>(initial)->setSize(size * 0.45f)->setWeight(600)->setColor("on_secondary_container", false);
    }
}

AvatarView::~AvatarView() {
    *m_alive = false;
}

void AvatarView::doPaint(Renderer& r) {
    Flex::doPaint(r);
    if (m_picture.empty()) return;
    const auto& b = rect();
    const int px = static_cast<int>(std::lround(b.w * r.scale()));
    if (auto* t = Images::get().picture(m_picture, px, px, true, [this, alive = m_alive] {
            if (*alive) requestRedraw();
        }))
        r.image(t->id, b, 1, Radii(b.w / 2));
}

Button* iconButton(Node* parent, const std::string& icon, std::function<void()> onClick, float size) {
    auto* b = parent->add<Button>();
    pill(b)->setPadding(Insets(8));
    b->add<Icon>(icon, size)->setColor("on_surface_variant", false);
    b->onClicked = [onClick = std::move(onClick)](uint32_t button) {
        if (button == BTN_LEFT && onClick) onClick();
    };
    return b;
}

Button* confirmButton(Node* parent, const std::string& key, const std::string& icon, std::function<void()> onConfirm) {
    static std::string armedKey;
    static std::chrono::steady_clock::time_point armedAt;
    static constexpr auto window = std::chrono::seconds(4);
    const auto armed = [key] { return armedKey == key && std::chrono::steady_clock::now() - armedAt < window; };
    auto* b = parent->add<Button>();
    pill(b)->setPadding(Insets(6))->setJustify(Align::Center);
    auto* glyph = b->add<Icon>(armed() ? "delete_forever" : icon, 20);
    glyph->setColor(armed() ? "error" : "on_surface_variant", false);
    b->onClicked = [key, armed, glyph, onConfirm = std::move(onConfirm)](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (armed()) {
            armedKey.clear();
            onConfirm();
            return;
        }
        armedKey = key;
        armedAt = std::chrono::steady_clock::now();
        glyph->setText("delete_forever");
        glyph->setColor("error");
    };
    return b;
}

Tile::Tile(const std::string& icon, const std::string& title) : Button(Direction::Row) {
    pill(this)->setPadding({10, 10, 18, 10})->setSpacing(12);
    setBackground(ColorRef("on_surface", 0.1f), false);
    pressedRadius = 18;
    minHeight = 64;
    grow = true;
    m_chip = add<Button>(Direction::Row);
    m_chip->setJustify(Align::Center);
    m_chip->fixedWidth = m_chip->fixedHeight = 44;
    m_chip->pressedRadius = 10;
    m_icon = m_chip->add<Icon>(icon, 22);
    auto* texts = add<Flex>(Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(0);
    texts->grow = true;
    m_title = texts->add<Label>(title);
    // Long names (networks, devices) wrap to a second line instead of cutting off
    m_title->setSize(15)->setWeight(600)->setMaxWidth(112)->setMaxLines(2);
    m_status = texts->add<Label>();
    m_status->setSize(13)->setMaxWidth(112)->setMaxLines(2);
    // With details, the body opens them and the chip toggles
    onClicked = [this](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (onDetails) onDetails();
        else if (onToggle) onToggle();
    };
    m_chip->onClicked = [this](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (onToggle) onToggle();
    };
    set(false, "");
}

void Tile::set(bool active, const std::string& status, const std::string& icon) {
    m_active = active;
    const bool chipOnly = static_cast<bool>(onDetails);
    const bool filled = active && !chipOnly;
    // Off: a translucent tint, so the blurred desktop shows through
    setBackground(filled ? ColorRef("primary") : ColorRef("on_surface", 0.1f));
    // The chip: a circle while off, a rounded square while on, morphing
    // between them (at once the first time)
    m_chip->morphRadius(Radii(active ? 14 : 22), m_shaped);
    m_shaped = true;
    m_chip->setBackground(active && chipOnly ? ColorRef("primary") : filled ? ColorRef::none() : ColorRef("on_surface", 0.1f));
    if (!icon.empty()) m_icon->setText(icon);
    m_icon->setFill(active)->setColor(active ? "on_primary" : "on_surface");
    m_title->setColor(filled ? "on_primary" : "on_surface");
    m_status->setText(status);
    m_status->setVisible(!status.empty());
    m_status->setColor(filled ? "on_primary" : "on_surface_variant");
}

void Tile::setSelected(bool selected) {
    setBorder(selected ? 3 : 0, "on_surface");
}

SmallTile::SmallTile(const std::string& icon, float width) : Button(Direction::Row) {
    pill(this)->setJustify(Align::Center);
    setBackground(ColorRef("on_surface", 0.1f), false);
    pressedRadius = 18;
    fixedWidth = width;
    fixedHeight = 64;
    m_icon = add<Icon>(icon, 24);
    set(false);
}

void SmallTile::set(bool active, const std::string& icon) {
    setBackground(active ? ColorRef("primary") : ColorRef("on_surface", 0.1f));
    if (!icon.empty()) m_icon->setText(icon);
    m_icon->setFill(active)->setColor(active ? "on_primary" : "on_surface");
}

void SmallTile::setSelected(bool selected) {
    setBorder(selected ? 3 : 0, "on_surface");
}

} // namespace m3::shell::kit
