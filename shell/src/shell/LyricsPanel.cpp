// The lyrics panel: the whole song in any language, the line sung now lit
// and kept in view as it plays, and Identify, which listens to what's
// playing and recognises the song (services/Lyrics)
#include "shell/Panels.h"

#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Lyrics.h"
#include "services/Mpris.h"
#include "shell/PanelKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::panels {

using namespace m3::ui;
using kit::Live;

namespace {

// Every line, the one being sung in the accent; woken at each line's time,
// never per frame
class SongLines : public Flex {
public:
    explicit SongLines(ScrollView* scroll) : Flex(Direction::Column), m_scroll(scroll) {
        setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({10, 8});
        m_lease = Mpris::get().watchPosition();
        m_lyrics = Lyrics::get().changed.connect([this] { build(); });
        m_media = Mpris::get().changed.connect([this] { tick(); }); // played, paused, sought
        build();
    }
    ~SongLines() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
        *m_alive = false;
    }

private:
    void build() {
        const auto& ly = Lyrics::get();
        // The same lines: just where the song is
        if (ly.lines() == m_built && !m_labels.empty()) return tick();
        m_built = ly.lines();
        clearChildren();
        m_labels.clear();
        m_lit = -2;
        for (const auto& l : m_built) {
            auto* label = add<Label>(l.text.empty() ? "♪" : l.text);
            label->setSize(19)->setWeight(600)->setMaxWidth(460)->setMaxLines(kAllLines);
            label->setColor("on_surface_variant", false)->setOpacity(0.7f, false);
            m_labels.push_back(label);
        }
        tick();
    }

    void tick() {
        if (m_timer) EventLoop::get().cancel(m_timer);
        m_timer = 0;
        const auto& ly = Lyrics::get();
        if (m_labels.empty()) return;
        const long long at = ly.position();
        const int i = std::min(ly.lineAt(at), static_cast<int>(m_labels.size()) - 1);
        if (i != m_lit) {
            if (m_lit >= 0) m_labels[static_cast<size_t>(m_lit)]->setColor("on_surface_variant")->setOpacity(0.7f);
            if (i >= 0) m_labels[static_cast<size_t>(i)]->setColor("primary")->setOpacity(1);
            m_lit = i;
            // Kept in view, once it has a place on screen
            EventLoop::get().defer([this, alive = m_alive, i] {
                if (!*alive || i != m_lit || i < 0) return;
                m_scroll->reveal(m_labels[static_cast<size_t>(i)]->rect());
            });
        }
        if (ly.playing() && static_cast<size_t>(i + 1) < ly.lines().size()) {
            const long long wait = ly.lines()[static_cast<size_t>(i + 1)].at - at;
            m_timer = EventLoop::get().addTimer(static_cast<int>(std::max(30LL, wait / 1000 + 15)), [this] {
                m_timer = 0;
                tick();
            });
        }
    }

    ScrollView* m_scroll;
    std::vector<Lyrics::Line> m_built;
    std::vector<Label*> m_labels;
    int m_lit = -2;
    EventLoop::TimerId m_timer = 0;
    Connection m_lyrics, m_media;
    std::shared_ptr<void> m_lease;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// Where the lines came from, or why there are none
std::string sourceNote(const Lyrics& ly) {
    using S = Lyrics::State;
    switch (ly.state()) {
    case S::Listening: return "Listening to the song to recognise it…";
    case S::Searching: return "Looking for the lyrics…";
    case S::None:
        return ly.recognising() || ly.heard() ? "No lyrics found for this song" : "No lyrics found. Identify listens to it and recognises the song";
    case S::Idle: return "Nothing playing. Identify listens to what's playing and recognises the song";
    case S::Found: break;
    }
    std::string note = ly.heard() ? "Recognised by Shazam · " : "";
    if (ly.source() == "Shazam") note += "lyrics from Shazam";
    else note += ly.synced() ? "synced lyrics from LRCLIB" : "lyrics from LRCLIB";
    if (!ly.synced()) note += ", not timed: the lines move along roughly";
    note[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(note[0])));
    return note;
}

} // namespace

void lyrics(Flex* card, Popup&) {
    auto* head = kit::header(card, "lyrics", "Lyrics");
    // Identify: ten seconds of what's playing, recognised by Shazam
    auto* identify = head->add<Button>(Flex::Direction::Row);
    pill(identify->setBackground("secondary_container", false))->setPadding({12, 6})->setSpacing(6);
    identify->add<Icon>("graphic_eq", 18)->setColor("on_secondary_container", false);
    auto* identifyLabel = identify->add<Label>("Identify");
    identifyLabel->setSize(12)->setWeight(500)->setColor("on_secondary_container", false);
    identify->tooltip = "Listen to what's playing and recognise the song (Shazam)";
    identify->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT) Lyrics::get().identify();
    };
    identify->setVisible(haveCommand("songrec") && haveCommand("parec") && Lyrics::get().enabled());

    // The song, and where its lines came from
    auto* info = card->add<Live>();
    info->setSpacing(2)->setAlignItems(Align::Start);
    info->sync = [info, identifyLabel] {
        info->clearChildren();
        const auto& ly = Lyrics::get();
        identifyLabel->setText(ly.state() == Lyrics::State::Listening ? "Listening…" : "Identify");
        if (!ly.enabled()) {
            kit::caption(info, "Lyrics are off (Settings → Sound → Synced lyrics)");
            return;
        }
        if (ly.hasTrack()) {
            info->add<Label>(ly.title())->setSize(20)->setWeight(600)->setMaxWidth(460)->setMaxLines(2);
            if (!ly.artist().empty()) info->add<Label>(ly.artist())->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(460);
        }
        kit::caption(info, sourceNote(ly))->setMaxWidth(460)->setMaxLines(kAllLines);
    };
    info->watch(Lyrics::get().changed);
    info->sync();

    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 480;
    scroll->blurEdges = true;
    scroll->add<SongLines>(scroll);
}

} // namespace m3::shell::panels
