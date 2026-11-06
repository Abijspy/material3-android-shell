#include "core/Config.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Clipboard.h"
#include "services/Currency.h"
#include "services/Search.h"
#include "services/Session.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"

#include <algorithm>
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
        // Exchange rates fetched for a conversion being typed: answered now
        m_rates = currency::Service::get().ready.connect([this] {
            EventLoop::get().defer([this, alive = m_alive] {
                if (*alive) refresh();
            });
        });
        // Clipboard thumbnails arrive after the list: show them as they do
        m_clipboard = Clipboard::get().changed.connect([this] {
            EventLoop::get().defer([this, alive = m_alive] {
                const std::string text = *alive ? m_field->text() : "";
                if (!text.starts_with('#')) return;
                std::string q = text.substr(1);
                q.erase(0, q.find_first_not_of(' '));
                const int selected = m_selected;
                show(search::clipboard(q));
                if (selected > 0 && selected < static_cast<int>(m_results.size())) select(selected);
            });
        });
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
        auto results = search::query(text);
        powerResults(text, results);
        settingsResults(text, results);
        show(std::move(results));
        if (text.starts_with('#') || text.starts_with('@') || search::asyncWord(text)) {
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

    // Lock, sleep, restart, shut down and log out, by any of their names,
    // ranked with the rest (Settings → Spotlight). Restart, shut down and
    // log out ask first
    void powerResults(const std::string& raw, std::vector<SearchResult>& out) {
        std::string q = raw;
        q.erase(0, q.find_first_not_of(' '));
        while (!q.empty() && q.back() == ' ') q.pop_back();
        if (q.size() < 3 || !Config::get().value<bool>("/spotlight/power", true)) return;
        struct Action {
            const char *title, *subtitle, *glyph, *confirm;
            std::vector<const char*> names;
            std::function<void()> run;
        };
        const Action actions[] = {
            {"Lock", "Lock the screen", "lock", "", {"lock", "lock screen"}, [] { if (lock) lock(); }},
            {"Sleep", "Suspend the computer", "bedtime", "", {"sleep", "suspend"}, [] { Session::get().suspend(); }},
            {"Restart", "Turns the computer off and on again", "restart_alt", "Press Enter again to restart", {"restart", "reboot"},
             [] { Session::get().reboot(); }},
            {"Shut down", "Turns the computer off", "power_settings_new", "Press Enter again to shut down",
             {"shut down", "shutdown", "power off", "poweroff", "turn off"}, [] { Session::get().powerOff(); }},
            {"Log out", "Ends your session and closes your apps", "logout", "Press Enter again to log out",
             {"log out", "logout", "log off", "sign out"}, [] { Session::get().logout(); }},
        };
        for (const auto& a : actions) {
            double best = 0;
            for (const char* n : a.names) best = std::max(best, fuzzyScore(q, n));
            if (best < 60) continue; // a start of one of its names, not letters scattered through it
            SearchResult r{a.title, a.subtitle, "", a.glyph, "", best + 60, a.run};
            r.confirm = a.confirm;
            // In its place by score, before the web search (which stays last)
            auto at = std::ranges::find_if(out, [&](const SearchResult& o) { return o.score < r.score || o.glyph == "travel_explore"; });
            out.insert(at, std::move(r));
        }
    }

    // Settings pages and settings whose name has what's typed in it, above
    // the web search (Settings → Spotlight)
    void settingsResults(const std::string& raw, std::vector<SearchResult>& out) {
        std::string q = raw;
        q.erase(0, q.find_first_not_of(' '));
        while (!q.empty() && q.back() == ' ') q.pop_back();
        if (q.size() < 3 || !Config::get().value<bool>("/spotlight/settings", true)) return;
        if (std::string("=:#@>").find(q[0]) != std::string::npos || search::asyncWord(q)) return;
        for (auto& c : q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::pair<int, const SettingsHit*>> found;
        for (const auto& h : settingsIndex(m_popup)) {
            // A page by its title or what it's for ("bluetooth": Connected devices)
            std::string t = h.text, b = h.isPage ? h.blurb : std::string();
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto at = t.find(q);
            if (at == std::string::npos && b.find(q) != std::string::npos) at = 1; // in the description: after title matches
            if (at == std::string::npos) continue;
            // Pages first, then names that start with it
            found.push_back({(h.isPage ? 0 : 2) + (at == 0 ? 0 : 1), &h});
        }
        std::ranges::stable_sort(found, {}, &std::pair<int, const SettingsHit*>::first);
        std::vector<SearchResult> rows;
        for (size_t i = 0; i < found.size() && i < 4; ++i) {
            const auto* h = found[i].second;
            const std::string page = h->page, setting = h->isPage ? std::string() : h->text;
            rows.push_back({h->text, h->isPage ? "Settings · " + h->blurb : "Settings → " + h->pageTitle, "", h->icon, "", 1,
                            [page, setting] { openSetting(page, setting); }});
        }
        // Before the web search, which stays last
        const bool web = !out.empty() && out.back().glyph == "travel_explore";
        out.insert(web ? out.end() - 1 : out.end(), rows.begin(), rows.end());
    }

    void show(std::vector<SearchResult> results) {
        m_results = std::move(results);
        m_list->clearChildren();
        m_rows.clear();
        m_titles.clear();
        m_subtitles.clear();
        m_armed = -1;
        for (size_t i = 0; i < m_results.size(); ++i) {
            const auto& r = m_results[i];
            auto* row = m_list->add<Button>(Direction::Row);
            row->setPadding({14, 8})->setSpacing(14)->setRadius(Radii(18));
            // A long answer (a definition) wraps, its row growing with it
            if (r.wrap) row->minHeight = kRowHeight;
            else row->fixedHeight = kRowHeight;
            if (!r.image.empty()) {
                // A copied picture, a screenshot or a video's frame
                const std::string image = r.image;
                auto* thumb = row->add<ImageView>(64, 40, [image](float scale, int& w, int& h) -> GLuint {
                    auto* t = Images::get().picture(image, static_cast<int>(64 * scale), static_cast<int>(40 * scale), true);
                    if (!t) return 0;
                    w = t->width;
                    h = t->height;
                    return t->id;
                });
                thumb->cover = true;
                thumb->radius = 10;
            } else if (!r.emoji.empty()) {
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
            // A long answer shows whole: as many lines as its sentence takes
            m_titles.push_back(texts->add<Label>(r.title));
            m_titles.back()->setSize(15)->setWeight(500)->setMaxWidth(520)->setMaxLines(r.wrap ? 40 : 1);
            Label* sub = nullptr;
            if (!r.subtitle.empty() || !r.confirm.empty()) {
                sub = texts->add<Label>(r.subtitle);
                sub->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(520)->setMaxLines(r.wrap ? 40 : 1);
            }
            m_subtitles.push_back(sub);
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
        if (index != m_armed) disarm();
        if (m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()))
            m_rows[static_cast<size_t>(m_selected)]->setBackground(ColorRef::none());
        m_selected = index;
        m_rows[static_cast<size_t>(index)]->setBackground("secondary_container");
        // Keep the selection inside the visible window (rows may differ in height)
        const auto* sel = m_rows[static_cast<size_t>(index)];
        const float top = sel->rect().h > 0 ? sel->rect().y - m_list->rect().y : static_cast<float>(index) * (kRowHeight + kRowGap);
        const float height = sel->rect().h > 0 ? sel->rect().h : kRowHeight;
        const float view = m_scroll->maxHeight;
        const float current = m_scrollTarget;
        if (top < current) m_scrollTarget = top;
        else if (top + height > current + view) m_scrollTarget = top + height - view;
        m_scroll->scrollTo(m_scrollTarget);
    }

    void activate() {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_results.size())) return;
        const auto& r = m_results[static_cast<size_t>(m_selected)];
        auto fn = r.activate;
        if (!fn) return;
        // One that asks first: the row says so, in the error colours, and
        // waits for a second Enter
        if (!r.confirm.empty() && m_armed != m_selected) {
            const auto i = static_cast<size_t>(m_selected);
            m_armed = m_selected;
            m_rows[i]->setBackground("error_container");
            m_titles[i]->setColor("on_error_container");
            if (m_subtitles[i]) m_subtitles[i]->setText(r.confirm)->setColor("on_error_container");
            return;
        }
        m_popup.close();
        fn();
    }

    // The row asking first, back as it was (the selection moved on)
    void disarm() {
        if (m_armed < 0 || m_armed >= static_cast<int>(m_rows.size())) return;
        const auto i = static_cast<size_t>(m_armed);
        m_armed = -1;
        m_titles[i]->setColor("on_surface");
        if (m_subtitles[i]) m_subtitles[i]->setText(m_results[i].subtitle)->setColor("on_surface_variant");
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
    std::vector<Label*> m_titles, m_subtitles; // each row's (no subtitle: null)
    int m_selected = -1, m_token = 0, m_armed = -1; // m_armed: the row asking "again?"
    float m_scrollTarget = 0;
    EventLoop::TimerId m_debounce = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    Connection m_clipboard, m_rates;
};

} // namespace

void launcher(Flex* card, Popup& popup, const std::string& initial) {
    card->setPadding(Insets(16));
    card->add<LauncherView>(popup, initial);
}

} // namespace m3::shell::panels
