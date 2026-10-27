#include "shell/bar/Widgets.h"

#include "core/Config.h"
#include "services/Search.h"
#include "render/Images.h"
#include "core/EventLoop.h"
#include "services/Compositor.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

BarEdge barEdge() {
    const auto p = Config::get().value<std::string>("/statusBar/position", "top");
    return p == "bottom" ? BarEdge::Bottom : p == "left" ? BarEdge::Left : p == "right" ? BarEdge::Right : BarEdge::Top;
}

bool barVertical() {
    const auto e = barEdge();
    return e == BarEdge::Left || e == BarEdge::Right;
}

float alongBar(const Node* n) {
    const auto& r = n->rect();
    return barVertical() ? r.y + r.h / 2 : r.x + r.w / 2;
}

// ── BarLayout ──

SizeF BarLayout::doMeasure(SizeF available) {
    for (auto& c : children()) c->measure(available);
    return available;
}

void BarLayout::onClick(uint32_t button) {
    if (button == BTN_RIGHT && onContextMenu) onContextMenu();
}

void BarLayout::doArrange() {
    const auto& r = rect();
    const auto& kids = children();
    auto sizeOf = [&](size_t i) { return i < kids.size() ? kids[i]->measure({r.w, r.h}) : SizeF{}; };
    const SizeF start = sizeOf(0), centre = sizeOf(1), end = sizeOf(2);
    if (vertical) {
        // The same, down the bar. A side bar is short of length: the middle
        // gets what the ends leave (a long title gives way, cut short)
        const float room = r.h - start.h - end.h - margin * 4;
        const SizeF mid = centre.h > room && kids.size() > 1 ? kids[1]->measure({r.w, std::max(0.0f, room)}) : centre;
        const float startBottom = r.y + margin + start.h;
        const float endTop = r.bottom() - margin - end.h;
        float cy = r.y + (r.h - mid.h) / 2;
        cy = std::clamp(cy, startBottom + margin, std::max(startBottom + margin, endTop - margin - mid.h));
        auto place = [&](size_t i, float y, const SizeF& s) {
            if (i < kids.size()) kids[i]->arrange({r.x + (r.w - s.w) / 2, y, s.w, s.h});
        };
        place(0, r.y + margin, start);
        place(1, cy, mid);
        place(2, endTop, end);
        return;
    }

    const float startRight = r.x + margin + start.w;
    const float endLeft = r.right() - margin - end.w;
    float cx = r.x + (r.w - centre.w) / 2;
    cx = std::clamp(cx, startRight + margin, std::max(startRight + margin, endLeft - margin - centre.w));

    auto place = [&](size_t i, float x, const SizeF& s) {
        if (i < kids.size()) kids[i]->arrange({x, r.y + (r.h - s.h) / 2, s.w, s.h});
    };
    place(0, r.x + margin, start);
    place(1, cx, centre);
    place(2, endLeft, end);
}

std::optional<ColorRef> pillColour() {
    const auto& c = Config::get();
    std::string kind = c.value<std::string>("/statusBar/pillColor", "auto");
    if (kind == "auto") {
        const int style = c.value<int>("/statusBar/backgroundStyle", 0);
        kind = style == 0 || style == 3 ? "surface" : "none";
    }
    const float alpha = Theme::get().barOpacity();
    if (kind == "surface") return ColorRef("surface_container_high", alpha);
    // Coloured pills stay readable over a busy wallpaper
    if (kind == "tonal") return ColorRef("secondary_container", std::max(alpha, 0.85f));
    if (kind == "accent") return ColorRef("primary_container", std::max(alpha, 0.85f));
    if (kind == "outline") return ColorRef("surface_container_highest", std::min(alpha, 0.35f));
    return std::nullopt;
}

bool barBlur() {
    return Config::get().value<bool>("/statusBar/blur", true);
}

void shapePill(Flex* f) {
    const auto& c = Config::get();
    const float radius = static_cast<float>(c.value<double>("/statusBar/pillRadius", 100));
    if (radius >= 100) pill(f);
    else f->setRadius(Radii(radius));
    // The Outline highlight has a border even without a width set
    const bool outline = c.value<std::string>("/statusBar/pillColor", "auto") == "outline";
    const float border = static_cast<float>(c.value<double>("/statusBar/pillBorder", outline ? 1.5 : 0));
    if (border > 0) f->setBorder(border, ColorRef(outline ? "outline" : "primary", outline ? 0.7f : 0.85f));
}

Flex* capsule(Node* parent, bool filled) {
    const bool vertical = barVertical();
    auto* f = parent->add<Flex>(vertical ? Flex::Direction::Column : Flex::Direction::Row);
    if (vertical) {
        f->setPadding({0, 10})->setSpacing(6);
        f->fixedWidth = 32;
    } else {
        f->setPadding({12, 0})->setSpacing(8);
        f->fixedHeight = 32;
    }
    f->collapseWhenEmpty = true;
    if (const auto colour = filled ? pillColour() : std::nullopt) {
        shapePill(f->setBackground(*colour, false));
        f->blurBehind = barBlur();
    }
    return f;
}

// ── Workspaces ──

// The indicator: a dot that stretches into a pill for the active workspace
// (down the bar, on a side one)
class Workspaces::Dot : public Node {
public:
    Tween width{8, Spring::bouncy()}; // stretches with a little bounce
    ColorTween color{ColorRef("outline_variant")};
    bool vertical = false;

protected:
    SizeF doMeasure(SizeF) override { return vertical ? SizeF{8, width.value()} : SizeF{width.value(), 8}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        if (vertical) r.rect({b.x + (b.w - 8) / 2, b.y, 8, b.h}, color.value(), Radii(4));
        else r.rect({b.x, b.y + (b.h - 8) / 2, b.w, 8}, color.value(), Radii(4));
        // Compare with the laid-out length, not running(): the animation can
        // end between this frame's layout and its paint
        if (std::abs(width.value() - (vertical ? b.h : b.w)) > 0.01f) requestLayout();
        else if (color.running()) requestRedraw();
    }
    bool doAnimating() const override { return width.running() || color.running(); }
};

Workspaces::Workspaces(std::string monitor, bool vertical)
    : Flex(vertical ? Direction::Column : Direction::Row), m_monitor(std::move(monitor)), m_vertical(vertical) {
    if (vertical) {
        setPadding({0, 10})->setSpacing(2);
        fixedWidth = 32;
    } else {
        setPadding({10, 0})->setSpacing(2);
        fixedHeight = 32;
    }
    m_conn = Compositor::get().workspacesChanged.connect([this] { sync(); });
    sync();
}

void Workspaces::sync() {
    auto& comp = Compositor::get();
    // This bar's: the desktop's numbers, or this monitor's own (niri)
    const auto workspaces = comp.workspacesOn(m_monitor);
    int shown = Config::get().value<int>("/bar/workspaces/shown", 5);
    for (const auto& ws : workspaces) shown = std::max(shown, ws.number);
    shown = std::clamp(shown, 1, 20);

    // Grow or shrink the slot list; existing slots keep their animation state
    while (static_cast<int>(m_slots.size()) < shown) {
        const int id = static_cast<int>(m_slots.size()) + 1;
        auto* slot = add<Button>(m_vertical ? Direction::Column : Direction::Row);
        if (m_vertical) {
            pill(slot)->setPadding({0, 4});
            slot->fixedWidth = 24;
        } else {
            pill(slot)->setPadding({4, 0});
            slot->fixedHeight = 24;
        }
        slot->tooltip = std::format("Workspace {}", id);
        slot->onClicked = [id](uint32_t button) {
            if (button == BTN_LEFT) Compositor::get().focusWorkspace(id);
        };
        slot->onScrolled = [](int d) { Compositor::get().focusWorkspaceRelative(d); };
        m_dots.push_back(slot->add<Dot>());
        m_dots.back()->vertical = m_vertical;
        m_slots.push_back(slot);
    }
    for (size_t i = 0; i < m_slots.size(); ++i) m_slots[i]->setVisible(static_cast<int>(i) < shown);

    const int active = comp.activeWorkspace(m_monitor);
    for (size_t i = 0; i < m_dots.size(); ++i) {
        const int id = static_cast<int>(i) + 1;
        const auto it = std::ranges::find(workspaces, id, &Compositor::Workspace::number);
        const bool occupied = it != workspaces.end() && it->windows > 0;
        auto* dot = m_dots[i];
        dot->width.set(id == active ? 24.0f : 8.0f);
        dot->color.set(id == active ? ColorRef("primary") : occupied ? ColorRef("on_surface_variant") : ColorRef("outline_variant"));
    }
    requestLayout();
}

// ── Clock ──

std::string Clock::toStrftime(const std::string& qt) {
    const bool twelve = qt.find("AP") != std::string::npos || qt.find("ap") != std::string::npos;
    std::string out;
    for (size_t i = 0; i < qt.size();) {
        auto take = [&](const char* tok) {
            const size_t n = std::char_traits<char>::length(tok);
            if (qt.compare(i, n, tok) != 0) return false;
            i += n;
            return true;
        };
        // Dates first: their letters are longer runs of the same
        if (take("dddd")) out += "%A";
        else if (take("ddd")) out += "%a";
        else if (take("dd")) out += "%d";
        else if (take("d")) out += "%-d";
        else if (take("MMMM")) out += "%B";
        else if (take("MMM")) out += "%b";
        else if (take("MM")) out += "%m";
        else if (take("M")) out += "%-m";
        else if (take("yyyy")) out += "%Y";
        else if (take("yy")) out += "%y";
        else if (take("hh") || take("HH")) out += twelve ? "%I" : "%H";
        else if (take("h") || take("H")) out += twelve ? "%-I" : "%-H";
        else if (take("mm")) out += "%M";
        else if (take("m")) out += "%-M";
        else if (take("ss")) out += "%S";
        else if (take("s")) out += "%-S";
        else if (take("AP") || take("A")) out += "%p";
        else if (take("ap") || take("a")) out += "%P";
        else if (qt[i] == '%') { out += "%%"; ++i; }
        else out += qt[i++];
    }
    return out;
}

std::string Clock::sample(const std::string& qt) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[96];
    std::strftime(buf, sizeof buf, toStrftime(qt).c_str(), &tm);
    return buf;
}

Clock::Clock(std::string place) : m_place(std::move(place)) {
    setSize(15)->setWeight(500);
    // A new format shows at once, not at the next minute
    m_config = Config::get().changed.connect([this] {
        if (m_timer) EventLoop::get().cancel(m_timer);
        m_timer = 0;
        tick();
    });
    tick();
}

Clock::~Clock() {
    if (m_timer) EventLoop::get().cancel(m_timer);
}

Clock* Clock::setStacked(bool stacked) {
    m_stacked = stacked;
    // Each part on a line of its own, centred under the one before
    setMaxLines(stacked ? 4 : 1)->setCentered(stacked)->setMaxWidth(stacked ? 40 : 0);
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = 0;
    tick();
    return this;
}

void Clock::tick() {
    auto& c = Config::get();
    const std::string base = "/bar/clock/" + m_place;
    // Each place's own time format, else the shell's
    const auto time = c.value<std::string>(base + "/timeFormat", c.value<std::string>("/time/format", "hh:mm"));
    std::string format = time;
    if (c.value<bool>(base + "/showDate", false) && !m_stacked)
        format = c.value<std::string>(base + "/dateFormat", "ddd d MMM") + "  ·  " + time;
    m_seconds = time.find('s') != std::string::npos;
    std::string text = sample(format);
    // Stacked: "15:42" reads 15 over 42 (and "3:42 PM" 3, 42, PM)
    if (m_stacked) std::ranges::replace_if(text, [](char ch) { return ch == ':' || ch == ' ' || ch == '.'; }, '\n');
    setText(text);
    schedule();
}

void Clock::schedule() {
    // Wake exactly on the next second or minute boundary
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto step = m_seconds ? duration_cast<milliseconds>(seconds(1)) : duration_cast<milliseconds>(minutes(1));
    const auto since = duration_cast<milliseconds>(now.time_since_epoch());
    const auto wait = step - since % step + milliseconds(5);
    m_timer = EventLoop::get().addTimer(static_cast<int>(wait.count()), [this] {
        m_timer = 0;
        tick();
    });
}

// ── WindowTitle ──

// Long titles scroll in a narrower space instead of taking the bar's;
// the icon stays put beside them
WindowTitle::WindowTitle(bool vertical) : Flex(vertical ? Direction::Column : Direction::Row) {
    setSpacing(8);
    m_icon = add<ImageView>(18, [this](float scale, int& w, int& h) -> GLuint {
        auto* t = m_iconName.empty() ? nullptr : Images::get().icon(m_iconName, static_cast<int>(std::lround(18 * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    if (vertical) {
        // Down the bar, cut short at its length (no scrolling sideways)
        m_turned = add<Label>();
        m_turned->setTurned(true)->setSize(13)->setMaxWidth(220)->setColor("on_surface_variant", false);
    } else {
        m_title = add<Marquee>(240);
        m_title->label()->setSize(13)->setColor("on_surface_variant", false);
        // How it scrolls (Settings → Status bar → Scrolling text)
        auto style = [this] {
            m_title->setMode(Marquee::modeOf(Config::get().value<std::string>("/bar/scrolling/windowTitle", "loop")));
            m_title->setSpeed(Config::get().value<float>("/bar/scrolling/speed", 30));
        };
        style();
        m_style = Config::get().changed.connect(style);
    }
    m_conn = Compositor::get().activeWindowChanged.connect([this] { sync(); });
    sync();
}

void WindowTitle::sync() {
    auto& c = Compositor::get();
    if (m_title) m_title->setText(c.activeTitle());
    if (m_turned) {
        m_turned->setText(c.activeTitle());
        tooltip = c.activeTitle();
    }
    // The app's icon from its desktop entry, by window class (as the dock finds it)
    m_iconName = c.activeAppId().empty() ? "" : Apps::get().iconForClass(c.activeAppId());
    m_icon->setVisible(!m_iconName.empty() && Images::get().icon(m_iconName, 18));
    m_icon->requestRedraw(); // its provider reads the new name
    setVisible(!c.activeTitle().empty());
}

} // namespace m3::shell
