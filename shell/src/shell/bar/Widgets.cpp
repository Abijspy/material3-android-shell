#include "shell/bar/Widgets.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "services/Hyprland.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

// ── BarLayout ──

SizeF BarLayout::doMeasure(SizeF available) {
    for (auto& c : children()) c->measure(available);
    return available;
}

void BarLayout::doArrange() {
    const auto& r = rect();
    const auto& kids = children();
    auto sizeOf = [&](size_t i) { return i < kids.size() ? kids[i]->measure({r.w, r.h}) : SizeF{}; };
    const SizeF start = sizeOf(0), centre = sizeOf(1), end = sizeOf(2);

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

Flex* capsule(Node* parent, bool filled) {
    auto* f = parent->add<Flex>(Flex::Direction::Row);
    f->setPadding({12, 0})->setSpacing(8);
    f->fixedHeight = 32;
    f->collapseWhenEmpty = true;
    if (filled) pill(f->setBackground("surface_container_high", false));
    return f;
}

// ── Workspaces ──

// The indicator: a dot that stretches into a pill for the active workspace
class Workspaces::Dot : public Node {
public:
    Tween width{8, motion::mediumMs, Easing::emphasizedDecelerate()};
    ColorTween color{ColorRef("outline_variant")};

protected:
    SizeF doMeasure(SizeF) override { return {width.value(), 8}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect({b.x, b.y + (b.h - 8) / 2, b.w, 8}, color.value(), Radii(4));
        // Compare with the laid-out width, not running(): the animation can
        // end between this frame's layout and its paint
        if (std::abs(width.value() - b.w) > 0.01f) requestLayout();
        else if (color.running()) requestRedraw();
    }
    bool doAnimating() const override { return width.running() || color.running(); }
};

Workspaces::Workspaces(std::string monitor) : Flex(Direction::Row), m_monitor(std::move(monitor)) {
    setPadding({10, 0})->setSpacing(2);
    fixedHeight = 32;
    m_conn = Hyprland::get().workspacesChanged.connect([this] { sync(); });
    sync();
}

void Workspaces::sync() {
    auto& hypr = Hyprland::get();
    int shown = Config::get().value<int>("/bar/workspaces/shown", 5);
    for (const auto& ws : hypr.workspaces()) shown = std::max(shown, ws.id);
    shown = std::clamp(shown, 1, 20);

    // Grow or shrink the slot list; existing slots keep their animation state
    while (static_cast<int>(m_slots.size()) < shown) {
        const int id = static_cast<int>(m_slots.size()) + 1;
        auto* slot = add<Button>(Direction::Row);
        pill(slot)->setPadding({4, 0});
        slot->fixedHeight = 24;
        slot->onClicked = [id](uint32_t button) {
            if (button == BTN_LEFT) Hyprland::get().focusWorkspace(id);
        };
        slot->onScrolled = [](int d) { Hyprland::get().focusWorkspaceRelative(d); };
        m_dots.push_back(slot->add<Dot>());
        m_slots.push_back(slot);
    }
    for (size_t i = 0; i < m_slots.size(); ++i) m_slots[i]->setVisible(static_cast<int>(i) < shown);

    const int active = hypr.activeWorkspace(m_monitor);
    for (size_t i = 0; i < m_dots.size(); ++i) {
        const int id = static_cast<int>(i) + 1;
        const auto it = std::ranges::find(hypr.workspaces(), id, &Hyprland::Workspace::id);
        const bool occupied = it != hypr.workspaces().end() && it->windows > 0;
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
        if (take("hh") || take("HH")) out += twelve ? "%I" : "%H";
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

Clock::Clock() {
    setSize(15)->setWeight(500);
    tick();
}

Clock::~Clock() {
    if (m_timer) EventLoop::get().cancel(m_timer);
}

void Clock::tick() {
    const auto format = Config::get().value<std::string>("/time/format", "hh:mm");
    m_seconds = format.find('s') != std::string::npos;
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, toStrftime(format).c_str(), &tm);
    setText(buf);
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

WindowTitle::WindowTitle() {
    setSize(13)->setMaxWidth(360)->setColor("on_surface_variant", false);
    m_conn = Hyprland::get().activeWindowChanged.connect([this] { sync(); });
    sync();
}

void WindowTitle::sync() {
    setText(Hyprland::get().activeWindowTitle());
    setVisible(!text().empty());
}

// ── Battery ──

Battery::Battery() : Flex(Direction::Row) {
    setSpacing(4);
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/power_supply", ec)) {
        std::ifstream type(e.path() / "type");
        std::string t;
        if (type >> t && t == "Battery") {
            m_path = e.path();
            break;
        }
    }
    m_icon = add<Icon>("battery_full", 18);
    m_label = add<Label>();
    m_label->setSize(13)->setWeight(500);
    if (m_path.empty()) {
        setVisible(false);
        return;
    }
    sync();
    m_timer = EventLoop::get().addTimer(30000, [this] { sync(); }, true);
}

Battery::~Battery() {
    if (m_timer) EventLoop::get().cancel(m_timer);
}

void Battery::sync() {
    int capacity = 0;
    std::string status;
    std::ifstream(m_path + "/capacity") >> capacity;
    std::ifstream(m_path + "/status") >> status;
    const bool charging = status == "Charging" || status == "Full";
    // Material Symbols has battery_0_bar … battery_6_bar and battery_full
    const int bars = std::clamp(capacity * 7 / 100, 0, 6);
    m_icon->setText(charging ? "battery_charging_full" : capacity >= 95 ? "battery_full" : "battery_" + std::to_string(bars) + "_bar");
    m_icon->setColor(capacity <= 15 && !charging ? ColorRef("error") : ColorRef("on_surface"));
    m_label->setText(std::to_string(capacity) + "%");
}

} // namespace m3::shell
