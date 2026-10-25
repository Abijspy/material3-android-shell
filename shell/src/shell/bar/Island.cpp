#include "shell/bar/Island.h"

#include "core/Config.h"
#include "services/Hyprland.h"
#include "services/Mpris.h"
#include "shell/PanelKit.h"
#include "services/Notifications.h"
#include "services/Screenshot.h"
#include "shell/ClockFaces.h"
#include "shell/bar/Widgets.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
#include <format>
#include <ctime>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

namespace {
constexpr float kPadding = 14;

// How long the screen recording has run, ticking each second
class RecordingTime : public Label {
public:
    RecordingTime() {
        sync();
        m_ticker = std::make_unique<kit::SecondTicker>([this] { sync(); });
    }

private:
    void sync() {
        const long s = std::max<long>(0, std::time(nullptr) - Screenshot::get().recordingSince());
        setText(std::format("{}:{:02}", s / 60, s % 60));
    }
    std::unique_ptr<kit::SecondTicker> m_ticker;
};
}

Island::Island(OpenPopup open) : Button(Direction::Row), m_open(std::move(open)), m_vertical(barVertical()) {
    pill(this);
    if (const auto colour = pillColour()) {
        shapePill(setBackground(*colour, false));
        blurBehind = barBlur();
    }
    if (m_vertical) fixedWidth = 32;
    else fixedHeight = 32;
    onClicked = [this](uint32_t button) {
        // The dashboard (calendar, weather) also while music plays: the media
        // buttons take their own clicks, the rest of the island opens it
        if (button == BTN_LEFT) m_open("dashboard", alongBar(this));
        if (button == BTN_RIGHT) m_open("notifications", alongBar(this));
    };
    m_media = Mpris::get().changed.connect([this] {
        if (m_mode != Mode::Notice) show(m_hovered && Mpris::get().active() ? Mode::Media : Mode::Clock);
    });
    m_notify = Notifications::get().posted.connect([this](uint32_t id) {
        const auto* n = Notifications::get().find(id);
        if (n && !Notifications::get().doNotDisturb()) flash("notifications_active", n->summary, 3500);
    });
    m_ws = Hyprland::get().workspacesChanged.connect([this] {
        const int ws = Hyprland::get().activeWorkspace("");
        if (m_lastWorkspace && ws != m_lastWorkspace && ws > 0 && Config::get().value<bool>("/statusBar/islandWorkspace", true))
            flash("view_carousel", "Workspace " + std::to_string(ws), 1200);
        m_lastWorkspace = ws;
    });
    m_lastWorkspace = Hyprland::get().activeWorkspace("");
    // A screen recording shows beside the clock, with its time; a click stops it
    m_recording = Screenshot::get().recordingChanged.connect([this] {
        if (m_mode == Mode::Clock) show(Mode::Clock);
    });
    build();
    m_width.set(contentLength() + kPadding * 2, false);
    tick();
}

float Island::contentLength() {
    return m_vertical ? m_content->measure({32, 800}).h : m_content->measure({800, 32}).w;
}

Island::~Island() {
    if (m_clock) EventLoop::get().cancel(m_clock);
    if (m_noticeTimer) EventLoop::get().cancel(m_noticeTimer);
}

void Island::onHoverWithin(bool hovered) {
    // Within, not just on: the media buttons are children of the island
    m_hovered = hovered;
    if (m_mode == Mode::Notice) return;
    show(hovered && Mpris::get().active() ? Mode::Media : Mode::Clock);
}

void Island::flash(const std::string& icon, const std::string& text, int ms) {
    m_noticeIcon = icon;
    m_noticeText = text;
    show(Mode::Notice);
    if (m_noticeTimer) EventLoop::get().cancel(m_noticeTimer);
    m_noticeTimer = EventLoop::get().addTimer(ms, [this] {
        m_noticeTimer = 0;
        m_mode = Mode::Clock; // force the rebuild below
        show(m_hovered && Mpris::get().active() ? Mode::Media : Mode::Clock);
    });
}

void Island::show(Mode mode) {
    if (mode == m_mode && mode != Mode::Notice && m_content) {
        build(); // same mode, fresh data (track change)
    } else {
        m_mode = mode;
        build();
        // Cross-fade the new content in while the width animates
        m_content->setOpacity(0, false);
        m_content->setOpacity(1);
    }
    m_width.set(contentLength() + kPadding * 2);
    requestLayout();
}

void Island::build() {
    // Rebuilding may drop the hovered child; the surface clears its pointers
    clearChildren();
    if (m_vertical) tooltip.clear(); // the track's, in Media mode
    m_content = add<Flex>(m_vertical ? Direction::Column : Direction::Row);
    m_content->setSpacing(8);
    switch (m_mode) {
    case Mode::Clock: {
        if (Screenshot::get().recording()) {
            auto* rec = m_content->add<Button>(Direction::Row);
            pill(rec->setBackground("error", false))->setPadding({8, 2})->setSpacing(4)->setAlignItems(Align::Center);
            rec->fixedHeight = 24;
            rec->add<Icon>("stop_circle", 16)->setFill(true)->setColor("on_error", false);
            // A side bar has room for the button alone
            if (m_vertical) rec->setPadding(Insets(4));
            else rec->add<RecordingTime>()->setSize(12)->setWeight(600)->setColor("on_error", false);
            rec->tooltip = "Stop recording";
            rec->onClicked = [](uint32_t b) {
                if (b == BTN_LEFT) Screenshot::get().stopRecording();
            };
        }
        const auto* p = Mpris::get().active();
        // Music playing: its progress rings the equaliser beside the clock
        if (p && p->status == "Playing") m_content->add<kit::WavyRing>(26)->add<Icon>("graphic_eq", 14)->setColor("primary", false);
        auto* clock = m_content->add<Clock>("island");
        if (m_vertical) clock->setStacked(true);
        break;
    }
    case Mode::Media: {
        const auto* p = Mpris::get().active();
        if (!p) break;
        m_content->add<kit::WavyRing>(26)->add<Icon>("music_note", 14)->setColor("primary", false);
        const std::string what = p->artist.empty() ? p->title : p->title + " · " + p->artist;
        // Down a side bar: the controls alone, the track in the tooltip
        if (m_vertical) tooltip = what;
        else
            m_content->add<Marquee>(200, what)
                ->setMode(Marquee::modeOf(Config::get().value<std::string>("/bar/scrolling/islandMedia", "loop")))
                ->setSpeed(Config::get().value<float>("/bar/scrolling/speed", 30))
                ->label()->setSize(13);
        auto control = [this](const char* icon, std::function<void()> fn) {
            auto* b = m_content->add<Button>();
            pill(b)->setPadding(Insets(3));
            b->add<Icon>(icon, 20)->setColor("on_surface", false);
            b->onClicked = [fn = std::move(fn)](uint32_t) { fn(); };
        };
        control("skip_previous", [] { Mpris::get().previous(); });
        control(p->status == "Playing" ? "pause" : "play_arrow", [] { Mpris::get().playPause(); });
        control("skip_next", [] { Mpris::get().next(); });
        break;
    }
    case Mode::Notice:
        m_content->add<Icon>(m_noticeIcon, 18)->setColor("primary", false);
        if (m_vertical) {
            // Reading down the bar, cut short at its length
            m_content->add<Label>(m_noticeText)->setTurned(true)->setSize(13)->setWeight(500)->setMaxWidth(200);
            break;
        }
        m_content->add<Marquee>(260, m_noticeText)
            ->setMode(Marquee::modeOf(Config::get().value<std::string>("/bar/scrolling/islandNotice", "loop")))
            ->setSpeed(Config::get().value<float>("/bar/scrolling/speed", 30))
            ->label()->setSize(13)->setWeight(500);
        break;
    }
}

void Island::tick() {
    // The clock label ticks itself; this keeps the island's width in step
    // when the text width changes (e.g. 9:59 → 10:00)
    const std::time_t now = std::time(nullptr);
    m_clock = EventLoop::get().addTimer(static_cast<int>((60 - now % 60) * 1000 + 50), [this] {
        m_clock = 0;
        if (m_mode == Mode::Clock) m_width.set(contentLength() + kPadding * 2);
        requestLayout();
        tick();
    });
}

SizeF Island::doMeasure(SizeF) {
    if (m_content) contentLength();
    return m_vertical ? SizeF{32, m_width.value()} : SizeF{m_width.value(), 32};
}

void Island::doArrange() {
    if (!m_content) return;
    const auto& r = rect();
    const SizeF s = m_vertical ? m_content->measure({32, 800}) : m_content->measure({800, 32});
    // Centred; while the island is narrower than its content, the edges clip
    m_content->arrange({r.x + (r.w - s.w) / 2, r.y + (r.h - s.h) / 2, s.w, s.h});
}

void Island::doPaint(Renderer& r) {
    Button::doPaint(r);
    if (m_width.running()) requestLayout();
}

} // namespace m3::shell
