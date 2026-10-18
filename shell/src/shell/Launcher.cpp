#include "render/Images.h"
#include "services/Search.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"

#include <cmath>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::panels {

using namespace m3::ui;

namespace {

constexpr float kRowHeight = 56;
constexpr float kRowGap = 2;

// The launcher column: owns the results and handles navigation keys that
// the text field passes up
class LauncherView : public Flex {
public:
    LauncherView(Popup& popup, const std::string& initial) : Flex(Direction::Column), m_popup(popup) {
        setAlignItems(Align::Stretch)->setSpacing(12);
        m_field = add<TextField>();
        m_field->setPlaceholder("Search apps   ·   = maths   : emoji   # clipboard   @ files   > run");
        m_field->onChanged = [this](const std::string&) { refresh(); };
        m_field->onSubmit = [this](const std::string&) { activate(); };
        m_field->onCancel = [this] { m_popup.close(); };
        m_scroll = add<ScrollView>();
        m_scroll->maxHeight = 7 * (kRowHeight + kRowGap);
        m_list = m_scroll->add<Flex>(Direction::Column);
        m_list->setAlignItems(Align::Stretch)->setSpacing(kRowGap);
        m_field->setText(initial);
        popup.initialFocus = m_field;
        refresh();
    }

    bool onKey(const KeyEvent& e) override {
        const int n = static_cast<int>(m_results.size());
        if (n == 0) return false;
        if (e.sym == XKB_KEY_Down || (e.sym == XKB_KEY_Tab && !e.shift()) || (e.ctrl() && e.sym == XKB_KEY_n))
            select((m_selected + 1) % n);
        else if (e.sym == XKB_KEY_Up || e.sym == XKB_KEY_ISO_Left_Tab || (e.sym == XKB_KEY_Tab && e.shift()) ||
                 (e.ctrl() && e.sym == XKB_KEY_p))
            select((m_selected - 1 + n) % n);
        else
            return false;
        return true;
    }

private:
    void refresh() {
        const std::string text = m_field->text();
        const int token = ++m_token;
        show(search::query(text));
        if (text.starts_with('#') || text.starts_with('@')) {
            // Process-backed providers: wait for typing to pause, drop stale replies
            auto alive = m_alive;
            EventLoop::get().debounce(m_debounce, 150, [this, text, token, alive] {
                if (!*alive) return;
                search::queryAsync(text, [this, token, alive](std::vector<SearchResult> r) {
                    if (*alive && token == m_token) show(std::move(r));
                });
            });
        }
    }

    void show(std::vector<SearchResult> results) {
        m_results = std::move(results);
        m_list->clearChildren();
        m_rows.clear();
        for (size_t i = 0; i < m_results.size(); ++i) {
            const auto& r = m_results[i];
            auto* row = m_list->add<Button>(Direction::Row);
            row->setPadding({14, 8})->setSpacing(14)->setRadius(Radii(18));
            row->fixedHeight = kRowHeight;
            if (!r.emoji.empty()) {
                auto* e = row->add<Label>(r.emoji);
                e->setSize(26)->setFamily("Noto Color Emoji, sans-serif");
                e->fixedWidth = 32;
            } else if (!r.icon.empty()) {
                const std::string icon = r.icon;
                row->add<ImageView>(32, [icon](float scale, int& w, int& h) -> GLuint {
                    auto* t = Images::get().icon(icon, static_cast<int>(std::lround(32 * scale)));
                    if (!t) return 0;
                    w = t->width;
                    h = t->height;
                    return t->id;
                });
            } else {
                auto* bubble = row->add<Flex>(Direction::Row);
                pill(bubble->setBackground("secondary_container", false))->setJustify(Align::Center);
                bubble->fixedWidth = bubble->fixedHeight = 32;
                bubble->add<Icon>(r.glyph.empty() ? "apps" : r.glyph, 18)->setColor("on_secondary_container", false);
            }
            auto* texts = row->add<Flex>(Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(r.title)->setSize(15)->setWeight(500)->setMaxWidth(520);
            if (!r.subtitle.empty())
                texts->add<Label>(r.subtitle)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(520);
            row->onClicked = [this, i](uint32_t) {
                m_selected = static_cast<int>(i);
                EventLoop::get().defer([this, alive = m_alive] {
                    if (*alive) activate();
                });
            };
            m_rows.push_back(row);
        }
        if (m_results.empty()) {
            auto* none = m_list->add<Label>(m_field->text().empty() ? "" : "No results");
            none->setSize(14)->setColor("on_surface_variant", false);
        }
        m_selected = -1;
        select(0);
        m_scroll->scrollTo(0, false);
    }

    void select(int index) {
        if (m_rows.empty()) return;
        if (m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()))
            m_rows[static_cast<size_t>(m_selected)]->setBackground(ColorRef::none());
        m_selected = index;
        m_rows[static_cast<size_t>(index)]->setBackground("secondary_container");
        // Keep the selection inside the visible window
        const float top = static_cast<float>(index) * (kRowHeight + kRowGap);
        const float view = m_scroll->maxHeight;
        const float current = m_scrollTarget;
        if (top < current) m_scrollTarget = top;
        else if (top + kRowHeight > current + view) m_scrollTarget = top + kRowHeight - view;
        m_scroll->scrollTo(m_scrollTarget);
    }

    void activate() {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_results.size())) return;
        auto fn = m_results[static_cast<size_t>(m_selected)].activate;
        if (!fn) return;
        m_popup.close();
        fn();
    }

public:
    ~LauncherView() override {
        *m_alive = false;
        if (m_debounce) EventLoop::get().cancel(m_debounce);
    }

private:
    Popup& m_popup;
    TextField* m_field;
    ScrollView* m_scroll;
    Flex* m_list;
    std::vector<SearchResult> m_results;
    std::vector<Button*> m_rows;
    int m_selected = -1, m_token = 0;
    float m_scrollTarget = 0;
    EventLoop::TimerId m_debounce = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace

void launcher(Flex* card, Popup& popup, const std::string& initial) {
    card->setPadding(Insets(16));
    card->add<LauncherView>(popup, initial);
}

} // namespace m3::shell::panels
