// Notes: the shell's note-taking app (material3ui-notes), in the spirit of
// a Material You notes app. Notes and checklists as cards, each in a tint
// of its own, pinned to the top, labelled, searched, archived, and kept in
// the trash for a week. Kept in ~/.local/share/material3ui/notes/notes.json.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/TextArea.h"
#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// Days in the trash before a note's gone (Settings → Notes; 0 keeps them)
std::time_t trashDays() {
    return std::max(0, Config::get().value<int>("/notes/trashDays", 7));
}

struct Note {
    std::string id, title, body;
    int tint = 0;
    bool pinned = false, archived = false, checklist = false;
    std::time_t trashed = 0, created = 0, modified = 0;
    std::vector<std::pair<std::string, bool>> items;
    std::vector<std::string> labels;
    std::vector<std::string> images; // copies in the notes' images folder
    std::time_t remind = 0;          // a reminder (a systemd user timer)

    bool empty() const {
        if (!title.empty() || !body.empty() || !images.empty()) return false;
        return std::ranges::all_of(items, [](const auto& i) { return i.first.empty(); });
    }
    // All its words, for search
    std::string haystack() const {
        std::string s = title + "\n" + body;
        for (const auto& [t, d] : items) s += "\n" + t;
        for (const auto& l : labels) s += "\n" + l;
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::string asText() const {
        std::string s = title.empty() ? "" : title + "\n\n";
        if (checklist)
            for (const auto& [t, d] : items) s += (d ? "- [x] " : "- [ ] ") + t + "\n";
        else
            s += body;
        return s;
    }
};

// The tints: the palette's own, and the classic note colours (pastel in a
// light theme, deep in a dark one), with the theme's text on all
struct Tint {
    const char *name, *light, *dark;
};
const Tint kTints[] = {
    {"Default", "", ""},           {"Coral", "#faafa8", "#77172e"}, {"Peach", "#f39f76", "#692b17"},   {"Sand", "#fff8b8", "#7c4a03"},
    {"Mint", "#e2f6d3", "#264d3b"}, {"Sage", "#b4ddd3", "#0c625d"},  {"Fog", "#d4e4ed", "#256377"},    {"Storm", "#aeccdc", "#284255"},
    {"Dusk", "#d3bfdb", "#472e5b"}, {"Blossom", "#f6e2dd", "#6c394f"}, {"Clay", "#e9e3d4", "#4b443a"}, {"Chalk", "#efeff1", "#3a3a3f"},
};
constexpr int kTintCount = sizeof kTints / sizeof kTints[0];

bool darkTheme() {
    const Color s = Theme::get().color("surface");
    return 0.2126f * s.r + 0.7152f * s.g + 0.0722f * s.b < 0.5f;
}
ColorRef tintOf(int i) {
    if (i <= 0 || i >= kTintCount) return ColorRef("surface_container_high");
    return ColorRef(*Color::fromHex(darkTheme() ? kTints[i].dark : kTints[i].light));
}

std::string storePath() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/material3ui/notes/notes.json";
}

std::string newId() {
    static std::mt19937 rng{std::random_device{}()};
    return std::format("{:x}{:04x}", std::time(nullptr), rng() & 0xffff);
}

class Store {
public:
    std::vector<Note> notes;
    std::vector<std::string> labels;

    void load() {
        bool broken = false;
        std::ifstream in(storePath());
        if (!in) return;
        try {
            const json j = json::parse(in);
            for (const auto& n : j.value("notes", json::array())) {
                Note x;
                x.id = n.value("id", newId());
                x.title = n.value("title", "");
                x.body = n.value("body", "");
                x.tint = n.value("tint", 0);
                x.pinned = n.value("pinned", false);
                x.archived = n.value("archived", false);
                x.checklist = n.value("checklist", false);
                x.trashed = n.value("trashed", std::time_t{0});
                x.created = n.value("created", std::time_t{0});
                x.modified = n.value("modified", std::time_t{0});
                for (const auto& i : n.value("items", json::array())) x.items.push_back({i.value("text", ""), i.value("done", false)});
                for (const auto& l : n.value("labels", json::array())) x.labels.push_back(l.get<std::string>());
                for (const auto& im : n.value("images", json::array())) x.images.push_back(im.get<std::string>());
                x.remind = n.value("remind", std::time_t{0});
                notes.push_back(std::move(x));
            }
            for (const auto& l : j.value("labels", json::array())) labels.push_back(l.get<std::string>());
        } catch (...) {
            // A broken file is kept aside, not overwritten
            std::error_code ec;
            fs::copy_file(storePath(), storePath() + ".broken", fs::copy_options::overwrite_existing, ec);
            broken = true;
        }
        // A week in the trash, then gone
        const std::time_t now = std::time(nullptr);
        const std::time_t days = trashDays();
        if (days > 0) std::erase_if(notes, [now, days](const Note& n) { return n.trashed && now - n.trashed > days * 86400; });
        // Pictures no note holds any more (deleted for good) go too
        if (broken) return;
        std::error_code ec;
        const fs::path dir = fs::path(storePath()).parent_path() / "images";
        std::set<std::string> held;
        for (const auto& n : notes) held.insert(n.images.begin(), n.images.end());
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (!held.contains(e.path().string())) fs::remove(e.path(), ec);
    }

    void saveSoon() {
        EventLoop::get().debounce(m_timer, 500, [this] { save(); });
    }
    void save() {
        if (m_timer) EventLoop::get().cancel(m_timer), m_timer = 0;
        json j;
        j["version"] = 1;
        j["labels"] = labels;
        json arr = json::array();
        for (const auto& n : notes) {
            if (n.empty() && !n.trashed) continue;
            json items = json::array();
            for (const auto& [t, d] : n.items) items.push_back({{"text", t}, {"done", d}});
            arr.push_back({{"id", n.id},           {"title", n.title},         {"body", n.body},         {"tint", n.tint},
                           {"pinned", n.pinned},   {"archived", n.archived},   {"checklist", n.checklist}, {"trashed", n.trashed},
                           {"created", n.created}, {"modified", n.modified},   {"items", items},          {"labels", n.labels},
                           {"images", n.images},   {"remind", n.remind}});
        }
        j["notes"] = arr;
        std::error_code ec;
        fs::create_directories(fs::path(storePath()).parent_path(), ec);
        paths::writeFile(storePath() + ".tmp", j.dump(1));
        fs::rename(storePath() + ".tmp", storePath(), ec);
    }

    Note* find(const std::string& id) {
        for (auto& n : notes)
            if (n.id == id) return &n;
        return nullptr;
    }
    ~Store() {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    EventLoop::TimerId m_timer = 0;
};

enum class View { Notes, Archive, Trash, Label };

struct State {
    Store store;
    View view = View::Notes;
    std::string label, query, editing, asking; // asking: a note's "time:" or "image:" prompt
    Flex *side = nullptr, *content = nullptr;
    TextField* search = nullptr;
    std::function<void()> show, sidebar;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override {
        *m_st->alive = false;
        // An empty note left open isn't kept
        if (Note* n = m_st->store.find(m_st->editing); n && n->empty()) std::erase_if(m_st->store.notes, [&](const Note& x) { return x.id == m_st->editing; });
        m_st->store.save();
        m_st->show = m_st->sidebar = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

void refresh(const std::shared_ptr<State>& st) {
    EventLoop::get().defer([st] {
        if (!*st->alive) return;
        if (st->sidebar) st->sidebar();
        if (st->show) st->show();
    });
}

void touch(const std::shared_ptr<State>& st, Note& n) {
    n.modified = std::time(nullptr);
    st->store.saveSoon();
}

void openNote(const std::shared_ptr<State>& st, const std::string& id) {
    // Leaving an empty one drops it
    if (Note* old = st->store.find(st->editing); old && old->empty() && old->id != id) std::erase_if(st->store.notes, [&](const Note& x) { return x.id == st->editing; });
    st->editing = id;
    refresh(st);
}

void newNote(const std::shared_ptr<State>& st, bool checklist) {
    Note n;
    n.id = newId();
    n.created = n.modified = std::time(nullptr);
    n.checklist = checklist;
    n.tint = std::clamp(Config::get().value<int>("/notes/tint", 0), 0, kTintCount - 1);
    if (checklist) n.items.push_back({"", false});
    if (st->view == View::Label && !st->label.empty()) n.labels.push_back(st->label);
    st->store.notes.insert(st->store.notes.begin(), n);
    openNote(st, n.id);
}

void toChecklist(Note& n, bool on) {
    if (on == n.checklist) return;
    if (on) {
        n.items.clear();
        std::string line;
        std::istringstream in(n.body);
        while (std::getline(in, line))
            if (!line.empty()) n.items.push_back({line, false});
        if (n.items.empty()) n.items.push_back({"", false});
        n.body.clear();
    } else {
        std::string b;
        for (const auto& [t, d] : n.items) b += t + "\n";
        if (!b.empty()) b.pop_back();
        n.body = b;
        n.items.clear();
    }
    n.checklist = on;
}

// The colour menu: a dot of each tint
void tintMenu(const std::shared_ptr<State>& st, Node* from, const std::string& id) {
    std::vector<MenuItem> items;
    Note* note = st->store.find(id);
    if (!note) return;
    for (int i = 0; i < kTintCount; ++i) {
        MenuItem m("", kTints[i].name, "", [st, id, i] {
            if (Note* n = st->store.find(id)) n->tint = i, touch(st, *n), refresh(st);
        }, note->tint == i);
        m.lead = [i](Node* row) {
            auto* dot = row->add<Flex>(Flex::Direction::Row);
            dot->fixedWidth = dot->fixedHeight = 20;
            dot->setRadius(Radii(10))->setBackground(tintOf(i), false);
        };
        items.push_back(std::move(m));
    }
    floatingMenu(from, items);
}

void labelMenu(const std::shared_ptr<State>& st, Node* from, const std::string& id) {
    std::vector<MenuItem> items;
    Note* note = st->store.find(id);
    if (!note) return;
    for (const auto& l : st->store.labels) {
        const bool on = std::ranges::find(note->labels, l) != note->labels.end();
        items.emplace_back("label", l, "", [st, id, l, on] {
            Note* n = st->store.find(id);
            if (!n) return;
            if (on) std::erase(n->labels, l);
            else n->labels.push_back(l);
            touch(st, *n);
            refresh(st);
        }, on);
    }
    if (items.empty()) items.emplace_back("info", "No labels yet: make one in the side bar", "", [] {}, false, false, false);
    floatingMenu(from, items);
}

void trashNote(const std::shared_ptr<State>& st, const std::string& id) {
    if (Note* n = st->store.find(id)) {
        n->trashed = std::time(nullptr);
        n->pinned = false;
        touch(st, *n);
    }
    if (st->editing == id) st->editing.clear();
    refresh(st);
}

void exportNote(const Note& n) {
    const char* docs = std::getenv("XDG_DOCUMENTS_DIR");
    const std::string dir = docs && *docs ? docs : paths::home() + "/Documents";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string name = n.title.empty() ? "Note" : n.title;
    for (auto& c : name)
        if (c == '/') c = '-';
    const std::string file = dir + "/" + fileops::freeName(dir, name + ".md");
    paths::writeFile(file, n.asText());
    m3::run({"notify-send", "-a", "Notes", "-i", "document-save", "Saved as Markdown", fileops::displayPath(file)}, nullptr);
}

void cardMenu(const std::shared_ptr<State>& st, Node* from, const std::string& id) {
    Note* n = st->store.find(id);
    if (!n) return;
    std::vector<MenuItem> items;
    if (n->trashed) {
        items.emplace_back("restore_from_trash", "Restore", "", [st, id] {
            if (Note* x = st->store.find(id)) x->trashed = 0, touch(st, *x), refresh(st);
        });
        items.emplace_back("delete_forever", "Delete for good", "", [st, id] {
            std::erase_if(st->store.notes, [&](const Note& x) { return x.id == id; });
            st->store.saveSoon();
            refresh(st);
        }, false, true);
    } else {
        items.emplace_back(n->pinned ? "keep_off" : "keep", n->pinned ? "Unpin" : "Pin", "", [st, id] {
            if (Note* x = st->store.find(id)) x->pinned = !x->pinned, touch(st, *x), refresh(st);
        });
        items.emplace_back(n->archived ? "unarchive" : "archive", n->archived ? "Unarchive" : "Archive", "", [st, id] {
            if (Note* x = st->store.find(id)) x->archived = !x->archived, x->pinned = false, touch(st, *x), refresh(st);
        });
        items.emplace_back("content_copy", "Copy text", "", [st, id] {
            if (Note* x = st->store.find(id)) m3::run({"wl-copy"}, nullptr, x->asText());
        });
        items.emplace_back("library_add", "Make a copy", "", [st, id] {
            Note* x = st->store.find(id);
            if (!x) return;
            Note c = *x;
            c.id = newId();
            c.created = c.modified = std::time(nullptr);
            c.pinned = false;
            st->store.notes.insert(st->store.notes.begin(), c);
            st->store.saveSoon();
            refresh(st);
        });
        items.emplace_back("download", "Save as Markdown", "", [st, id] {
            if (Note* x = st->store.find(id)) exportNote(*x);
        });
        items.emplace_back("delete", "Move to the trash", "", [st, id] { trashNote(st, id); }, false, true, true, true);
    }
    floatingMenu(from, items);
}

// ── Pictures and reminders ──

std::string imagesDir() {
    const std::string d = fs::path(storePath()).parent_path().string() + "/images";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

std::string newImagePath(const std::string& id, const std::string& ext) {
    return imagesDir() + "/" + id + "-" + std::to_string(std::time(nullptr)) + "-" + newId().substr(0, 4) + ext;
}

void addImageFile(const std::shared_ptr<State>& st, const std::string& id, const std::string& from) {
    Note* n = st->store.find(id);
    std::error_code ec;
    if (!n || !fs::is_regular_file(from, ec)) return refresh(st);
    const std::string to = newImagePath(id, fs::path(from).extension().string());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (!ec) n->images.push_back(to), touch(st, *n);
    refresh(st);
}

// The clipboard's picture into the note
void pasteImage(const std::shared_ptr<State>& st, const std::string& id) {
    const std::string to = newImagePath(id, ".png");
    m3::run({"sh", "-c", "wl-paste --no-newline --type image/png > \"$1\" && [ -s \"$1\" ] || { rm -f \"$1\"; exit 1; }", "sh", to},
            [st, id, to](int code, const std::string&) {
                if (!*st->alive) return;
                if (code != 0) {
                    m3::run({"notify-send", "-a", "Notes", "No picture on the clipboard", "Copy one (a screenshot, an image in a browser) first"}, nullptr);
                    return;
                }
                if (Note* n = st->store.find(id)) n->images.push_back(to), touch(st, *n), refresh(st);
            });
}

std::string unitName(const std::string& id) {
    std::string u = "material3ui-note-";
    for (char c : id) u += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return u;
}

// A reminder: a transient systemd user timer that sends a notification,
// with Notes open or not
void setReminder(const std::shared_ptr<State>& st, const std::string& id, std::time_t when) {
    Note* n = st->store.find(id);
    if (!n) return;
    const std::string unit = unitName(id);
    const std::string title = n->title.empty() ? "A note" : n->title;
    const std::string body = n->checklist ? std::format("{} items", n->items.size()) : n->body.substr(0, 160);
    n->remind = when;
    touch(st, *n);
    refresh(st);
    // The old timer goes first, then the new one is set
    m3::run({"systemctl", "--user", "stop", unit + ".timer"}, [when, unit, title, body](int, const std::string&) {
        if (when <= std::time(nullptr)) return;
        char at[32];
        std::strftime(at, sizeof at, "%Y-%m-%d %H:%M:%S", std::localtime(&when));
        m3::run({"systemd-run", "--user", "--unit=" + unit, "--on-calendar=" + std::string(at), "--timer-property=AccuracySec=1s", "--collect",
                 "notify-send", "-a", "Notes", "-i", "accessories-text-editor", "-u", "critical", "Reminder: " + title, body},
                [](int code, const std::string& out) {
                    if (code != 0) m3::run({"notify-send", "-a", "Notes", "Couldn't set the reminder", out.substr(0, 200)}, nullptr);
                });
    });
}

std::time_t todayAt(int dayOffset, int hour) {
    const std::time_t now = std::time(nullptr);
    std::tm x = *std::localtime(&now);
    x.tm_mday += dayOffset, x.tm_hour = hour, x.tm_min = 0, x.tm_sec = 0, x.tm_isdst = -1;
    return std::mktime(&x);
}

void reminderMenu(const std::shared_ptr<State>& st, Node* from, const std::string& id) {
    const std::time_t now = std::time(nullptr);
    const std::tm t = *std::localtime(&now);
    const int toMonday = (8 - t.tm_wday) % 7 == 0 ? 7 : (8 - t.tm_wday) % 7;
    std::vector<MenuItem> items;
    items.emplace_back("schedule", "In an hour", "", [st, id, now] { setReminder(st, id, now + 3600); });
    if (t.tm_hour < 18) items.emplace_back("wb_twilight", "This evening", "18:00", [st, id] { setReminder(st, id, todayAt(0, 18)); });
    items.emplace_back("light_mode", "Tomorrow morning", "08:00", [st, id] { setReminder(st, id, todayAt(1, 8)); });
    items.emplace_back("date_range", "Next week", "Mon 08:00", [st, id, toMonday] { setReminder(st, id, todayAt(toMonday, 8)); });
    items.emplace_back("edit_calendar", "Pick a time…", "", [st, id] { st->asking = "time:" + id, refresh(st); });
    const Note* n = st->store.find(id);
    if (n && n->remind) items.emplace_back("alarm_off", "Remove the reminder", "", [st, id] { setReminder(st, id, 0); }, false, true, true, true);
    floatingMenu(from, items);
}

std::string whenText(std::time_t t) {
    char b[48];
    std::strftime(b, sizeof b, "%a %d %b, %H:%M", std::localtime(&t));
    return b;
}

// ── The grid ──

void card(const std::shared_ptr<State>& st, Node* parent, const Note& n) {
    auto* b = parent->add<Button>(Flex::Direction::Column);
    b->setPadding({16, 14})->setSpacing(8)->setRadius(Radii(20));
    b->setAlignItems(Align::Start);
    b->setBackground(tintOf(n.tint), false);
    b->fixedWidth = 236;
    if (n.tint == 0) b->setBorder(1, ColorRef("outline_variant", 0.6f));
    if (!n.images.empty()) {
        const std::string pic = n.images.front();
        auto* v = b->add<ImageView>(204, 130, [pic](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().picture(pic, static_cast<int>(204 * scale), static_cast<int>(130 * scale), true);
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        v->cover = true;
        v->radius = 12;
        if (n.images.size() > 1) caption(b, std::format("+ {} more pictures", n.images.size() - 1));
    }
    if (!n.title.empty() || n.pinned) {
        auto* head = b->add<Flex>(Flex::Direction::Row);
        head->setSpacing(6)->setAlignItems(Align::Start);
        auto* t = head->add<Label>(n.title);
        t->setSize(15)->setWeight(600)->setMaxWidth(n.pinned ? 178 : 204)->setMaxLines(3);
        t->grow = true;
        if (n.pinned) head->add<Icon>("keep", 16)->setFill(true)->setColor("on_surface_variant", false);
    }
    if (n.checklist) {
        int shown = 0, done = 0;
        for (const auto& [text, d] : n.items) {
            if (d) {
                ++done;
                continue;
            }
            if (text.empty() || ++shown > 8) continue;
            auto* r = b->add<Flex>(Flex::Direction::Row);
            r->setSpacing(6)->setAlignItems(Align::Start);
            r->add<Icon>("check_box_outline_blank", 16)->setColor("on_surface_variant", false);
            r->add<Label>(text)->setSize(13)->setMaxWidth(180)->setMaxLines(2);
        }
        if (shown > 8) caption(b, std::format("+ {} more", shown - 8));
        if (done) caption(b, std::format("{} ticked", done));
    } else if (!n.body.empty()) {
        b->add<Label>(n.body.size() > 600 ? n.body.substr(0, 600) + "…" : n.body)->setSize(13)->setMaxWidth(204)->setMaxLines(10)->setColor("on_surface", false);
    }
    if (n.remind && !n.trashed) {
        auto* chip = b->add<Flex>(Flex::Direction::Row);
        pill(chip->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({8, 3})->setSpacing(4)->setAlignItems(Align::Center);
        chip->add<Icon>(n.remind < std::time(nullptr) ? "alarm_on" : "alarm", 13);
        chip->add<Label>(whenText(n.remind))->setSize(11)->setWeight(500);
    }
    if (n.empty()) caption(b, "Empty note");
    if (!n.labels.empty()) {
        auto* chips = b->add<Flow>();
        chips->spacing = 4;
        for (const auto& l : n.labels) {
            auto* c = chips->add<Flex>(Flex::Direction::Row);
            pill(c->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({8, 2});
            c->add<Label>(l)->setSize(11)->setWeight(500);
        }
    }
    if (n.trashed && trashDays() > 0) caption(b, std::format("Deleted for good in {} days", std::max<long>(1, (trashDays() * 86400 - (std::time(nullptr) - n.trashed)) / 86400 + 1)));
    const std::string id = n.id;
    b->onClicked = [st, id, b](uint32_t button) {
        if (button == BTN_RIGHT) return cardMenu(st, b, id);
        if (button != BTN_LEFT) return;
        Note* x = st->store.find(id);
        if (x && x->trashed) return cardMenu(st, b, id); // read-only there
        openNote(st, id);
    };
}

void gridView(const std::shared_ptr<State>& st, Flex* c) {
    // Search, and what to start
    auto* tools = c->add<Flex>(Flex::Direction::Row);
    tools->setSpacing(10)->setAlignItems(Align::Center);
    auto* search = tools->add<Flex>(Flex::Direction::Row);
    pill(search->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    search->grow = true;
    search->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    st->search = search->add<TextField>();
    st->search->setPlaceholder("Search your notes");
    st->search->bare = true;
    st->search->grow = true;
    st->search->setText(st->query);
    st->search->onChanged = [st](const std::string& t) {
        st->query = t;
        EventLoop::get().defer([st] {
            if (*st->alive && st->show) {
                st->show();
                // Typing goes on in the new field
                if (st->search) st->search->focus();
            }
        });
    };
    if (st->view != View::Trash && st->view != View::Archive) {
        auto* take = c->add<Button>(Flex::Direction::Row);
        take->setPadding({20, 14})->setSpacing(12)->setRadius(Radii(20));
        take->setBackground("surface_container_high", false);
        take->setBorder(1, ColorRef("outline_variant", 0.6f));
        auto* l = take->add<Label>("Take a note…");
        l->setSize(15)->setColor("on_surface_variant", false);
        l->grow = true;
        take->onClicked = [st](uint32_t b) {
            if (b == BTN_LEFT) newNote(st, false);
        };
        iconButton(take, "check_box", [st] { newNote(st, true); }, 22)->tooltip = "New checklist";
    } else if (st->view == View::Trash) {
        const bool any = std::ranges::any_of(st->store.notes, [](const Note& n) { return n.trashed; });
        auto* r = c->add<Flex>(Flex::Direction::Row);
        r->setSpacing(10)->setAlignItems(Align::Center);
        auto* l = r->add<Label>(trashDays() > 0 ? std::format("Notes in the trash are deleted after {} days", trashDays()) : "Notes stay in the trash until you empty it");
        l->setSize(13)->setColor("on_surface_variant", false);
        l->grow = true;
        if (any)
            pillButton(r, "Empty the trash", false, [st] {
                std::erase_if(st->store.notes, [](const Note& n) { return n.trashed; });
                st->store.saveSoon();
                refresh(st);
            }, "delete_forever");
    }

    std::string q = st->query;
    for (auto& ch : q) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::vector<const Note*> pinned, others;
    for (const auto& n : st->store.notes) {
        if (n.id == st->editing && n.empty()) continue;
        const bool in = st->view == View::Trash   ? n.trashed != 0
                        : n.trashed                ? false
                        : st->view == View::Archive ? n.archived
                        : st->view == View::Label   ? std::ranges::find(n.labels, st->label) != n.labels.end()
                                                    : !n.archived;
        if (!in || (!q.empty() && n.haystack().find(q) == std::string::npos)) continue;
        (n.pinned && st->view != View::Trash ? pinned : others).push_back(&n);
    }
    const std::string order = Config::get().value<std::string>("/notes/sort", "edited");
    auto byTime = [order](const Note* a, const Note* b) {
        if (order == "created") return a->created > b->created;
        if (order == "title") {
            std::string x = a->title, y = b->title;
            for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return x < y;
        }
        return a->modified > b->modified;
    };
    std::ranges::sort(pinned, byTime);
    std::ranges::sort(others, byTime);
    auto section = [&](const char* name, const std::vector<const Note*>& list) {
        if (list.empty()) return;
        if (name) caption(c, name);
        auto* flow = c->add<Flow>();
        flow->spacing = 12;
        for (const Note* n : list) card(st, flow, *n);
    };
    if (!pinned.empty()) {
        section("Pinned", pinned);
        section("Others", others);
    } else {
        section(nullptr, others);
    }
    if (pinned.empty() && others.empty()) {
        auto* e = c->add<Flex>(Flex::Direction::Column);
        e->setAlignItems(Align::Center)->setSpacing(10)->setPadding({0, 60});
        const char* icon = !q.empty() ? "search_off" : st->view == View::Trash ? "delete" : st->view == View::Archive ? "archive" : "lightbulb";
        e->add<Icon>(icon, 64)->setColor("outline", false);
        e->add<Label>(!q.empty() ? "Nothing matches" : st->view == View::Trash ? "The trash is empty" : st->view == View::Archive ? "Archived notes show here" : "Notes you add appear here")
            ->setSize(16)
            ->setColor("on_surface_variant", false);
    }
}

// ── A note, open ──

void editorView(const std::shared_ptr<State>& st, Flex* c) {
    Note* n = st->store.find(st->editing);
    if (!n) {
        st->editing.clear();
        return gridView(st, c);
    }
    const std::string id = n->id;
    auto* sheet = c->add<Flex>(Flex::Direction::Column);
    sheet->setPadding({20, 14, 20, 18})->setSpacing(10)->setRadius(Radii(26))->setAlignItems(Align::Stretch);
    sheet->setBackground(tintOf(n->tint), false);
    if (n->tint == 0) sheet->setBorder(1, ColorRef("outline_variant", 0.6f));

    // Pictures dropped on the note go in it
    sheet->onDropFiles = [st, id](const std::vector<std::string>& paths) {
        for (const auto& p : paths)
            if (fileops::isPicture(p)) addImageFile(st, id, p);
    };
    sheet->onDropHover = [sheet, plain = n->tint == 0](bool over) {
        if (over) sheet->setBorder(2, ColorRef("primary"));
        else sheet->setBorder(plain ? 1 : 0, ColorRef("outline_variant", 0.6f));
    };

    auto* bar = sheet->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(2)->setAlignItems(Align::Center);
    iconButton(bar, "arrow_back", [st] { openNote(st, ""); }, 22)->tooltip = "Back (Escape)";
    bar->add<Node>()->grow = true;
    auto* pin = iconButton(bar, "keep", [st, id] {
        if (Note* x = st->store.find(id)) x->pinned = !x->pinned, touch(st, *x), refresh(st);
    }, 22);
    for (auto& ch : pin->children())
        if (auto* i = dynamic_cast<Icon*>(ch.get())) i->setFill(n->pinned);
    pin->tooltip = n->pinned ? "Unpin" : "Pin to the top";
    Button* tint = iconButton(bar, "palette", [] {}, 22);
    tint->onClicked = [st, id, tint](uint32_t b) {
        if (b == BTN_LEFT) tintMenu(st, tint, id);
    };
    tint->tooltip = "Colour";
    Button* lab = iconButton(bar, "label", [] {}, 22);
    lab->onClicked = [st, id, lab](uint32_t b) {
        if (b == BTN_LEFT) labelMenu(st, lab, id);
    };
    lab->tooltip = "Labels";
    iconButton(bar, n->checklist ? "notes" : "checklist", [st, id] {
        if (Note* x = st->store.find(id)) toChecklist(*x, !x->checklist), touch(st, *x), refresh(st);
    }, 22)->tooltip = n->checklist ? "Plain text" : "Make it a checklist";
    Button* rem = iconButton(bar, n->remind ? "alarm_on" : "add_alert", [] {}, 22);
    rem->onClicked = [st, id, rem](uint32_t b) {
        if (b == BTN_LEFT) reminderMenu(st, rem, id);
    };
    rem->tooltip = "Remind me";
    Button* pic = iconButton(bar, "add_photo_alternate", [] {}, 22);
    pic->onClicked = [st, id, pic](uint32_t b) {
        if (b != BTN_LEFT) return;
        floatingMenu(pic, {MenuItem("content_paste", "Paste a picture", "from the clipboard", [st, id] { pasteImage(st, id); }),
                           MenuItem("folder_open", "From a file…", "", [st, id] { st->asking = "image:" + id, refresh(st); })});
    };
    pic->tooltip = "Add a picture";
    iconButton(bar, n->archived ? "unarchive" : "archive", [st, id] {
        if (Note* x = st->store.find(id)) x->archived = !x->archived, x->pinned = false, touch(st, *x);
        openNote(st, "");
    }, 22)->tooltip = n->archived ? "Unarchive" : "Archive";
    iconButton(bar, "delete", [st, id] { trashNote(st, id); }, 22)->tooltip = "Move to the trash";
    Button* more = iconButton(bar, "more_vert", [] {}, 22);
    more->onClicked = [st, id, more](uint32_t b) {
        if (b == BTN_LEFT) cardMenu(st, more, id);
    };

    auto* title = sheet->add<TextArea>();
    title->singleLine = true;
    title->setSize(22)->setMinLines(1)->setPlaceholder("Title");
    title->setText(n->title);
    title->onChanged = [st, id](const std::string& t) {
        if (Note* x = st->store.find(id)) x->title = t, touch(st, *x);
    };
    TextArea* body = nullptr;
    if (!n->checklist) {
        body = sheet->add<TextArea>();
        body->setSize(16)->setMinLines(10)->setPlaceholder("Note");
        body->setText(n->body);
        body->onChanged = [st, id](const std::string& t) {
            if (Note* x = st->store.find(id)) x->body = t, touch(st, *x);
        };
        body->onOtherKey = [st](const KeyEvent& e) {
            if (e.sym == XKB_KEY_Escape) return openNote(st, ""), true;
            return false;
        };
    } else {
        // The list: unticked first, then the ticked ones
        auto row = [&](size_t i, bool done) {
            auto* r = sheet->add<Flex>(Flex::Direction::Row);
            r->setSpacing(6)->setAlignItems(Align::Center);
            iconButton(r, done ? "check_box" : "check_box_outline_blank", [st, id, i] {
                Note* x = st->store.find(id);
                if (!x || i >= x->items.size()) return;
                x->items[i].second = !x->items[i].second;
                touch(st, *x);
                refresh(st);
            }, 22);
            auto* f = r->add<TextField>();
            f->bare = true;
            f->grow = true;
            f->setText(n->items[i].first);
            f->setPlaceholder("List item");
            f->onChanged = [st, id, i](const std::string& t) {
                Note* x = st->store.find(id);
                if (x && i < x->items.size()) x->items[i].first = t, touch(st, *x);
            };
            f->onSubmit = [st, id, i](const std::string&) {
                // Enter: a new item after this one
                Note* x = st->store.find(id);
                if (!x) return;
                x->items.insert(x->items.begin() + static_cast<long>(std::min(i + 1, x->items.size())), {"", false});
                touch(st, *x);
                refresh(st);
            };
            if (done) r->setOpacity(0.55f, false);
            iconButton(r, "close", [st, id, i] {
                Note* x = st->store.find(id);
                if (!x || i >= x->items.size()) return;
                x->items.erase(x->items.begin() + static_cast<long>(i));
                touch(st, *x);
                refresh(st);
            }, 18)->tooltip = "Remove";
        };
        size_t ticked = 0;
        for (size_t i = 0; i < n->items.size(); ++i)
            if (!n->items[i].second) row(i, false);
            else ++ticked;
        auto* add = sheet->add<Button>(Flex::Direction::Row);
        add->setPadding({8, 8})->setSpacing(10)->setRadius(Radii(12));
        add->add<Icon>("add", 22)->setColor("on_surface_variant", false);
        add->add<Label>("List item")->setSize(15)->setColor("on_surface_variant", false);
        add->onClicked = [st, id](uint32_t b) {
            if (b != BTN_LEFT) return;
            if (Note* x = st->store.find(id)) x->items.push_back({"", false}), touch(st, *x), refresh(st);
        };
        if (ticked) {
            caption(sheet, std::format("{} ticked", ticked));
            for (size_t i = 0; i < n->items.size(); ++i)
                if (n->items[i].second) row(i, true);
        }
    }
    title->onOtherKey = [st, body](const KeyEvent& e) {
        if (e.sym == XKB_KEY_Escape) return openNote(st, ""), true;
        if ((e.sym == XKB_KEY_Return || e.sym == XKB_KEY_Tab) && body) return body->focus(), true;
        return false;
    };
    // A time or a picture's path, asked for in the sheet
    if (st->asking == "time:" + id || st->asking == "image:" + id) {
        const bool image = st->asking.starts_with("image:");
        auto* ask = sheet->add<Flex>(Flex::Direction::Row);
        pill(ask->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({14, 4, 6, 4})->setSpacing(8)->setAlignItems(Align::Center);
        ask->add<Icon>(image ? "image" : "edit_calendar", 20);
        auto* f = ask->add<TextField>();
        f->bare = true;
        f->grow = true;
        f->setPlaceholder(image ? "The picture's path, like ~/Pictures/photo.jpg" : "When: YYYY-MM-DD HH:MM");
        if (!image) {
            char def[32];
            const std::time_t soon = n->remind > std::time(nullptr) ? n->remind : std::time(nullptr) + 3600;
            std::strftime(def, sizeof def, "%Y-%m-%d %H:%M", std::localtime(&soon));
            f->setText(def);
        }
        auto done = [st, id, f, image] {
            std::string v = f->text();
            st->asking.clear();
            if (image) {
                if (v.starts_with("~/")) v = paths::home() + v.substr(1);
                if (v.starts_with("file://")) v = v.substr(7);
                return addImageFile(st, id, v);
            }
            std::tm tm{};
            const char* end = strptime(v.c_str(), "%Y-%m-%d %H:%M", &tm);
            if (!end) return refresh(st);
            tm.tm_isdst = -1;
            setReminder(st, id, std::mktime(&tm));
        };
        f->onSubmit = [done](const std::string&) { done(); };
        pillButton(ask, image ? "Add" : "Set", true, done);
        iconButton(ask, "close", [st] { st->asking.clear(), refresh(st); }, 18);
        EventLoop::get().defer([st, f] {
            if (*st->alive) f->focus();
        });
    }
    if (n->remind) {
        auto* chip = sheet->add<Button>(Flex::Direction::Row);
        pill(chip->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({10, 5})->setSpacing(6)->setAlignItems(Align::Center);
        chip->alignSelf = Align::Start;
        chip->add<Icon>(n->remind < std::time(nullptr) ? "alarm_on" : "alarm", 16);
        chip->add<Label>((n->remind < std::time(nullptr) ? "Reminded " : "Remind me ") + whenText(n->remind))->setSize(12)->setWeight(500);
        chip->onClicked = [st, id, chip](uint32_t b) {
            if (b == BTN_LEFT) reminderMenu(st, chip, id);
        };
    }
    if (!n->images.empty()) {
        auto* grid = sheet->add<Flow>();
        grid->spacing = 8;
        for (size_t i = 0; i < n->images.size(); ++i) {
            const std::string pic = n->images[i];
            auto* box = grid->add<Button>(Flex::Direction::Column);
            box->setPadding(Insets(0))->setRadius(Radii(14));
            box->tooltip = "Open · right click to take it out";
            auto* v = box->add<ImageView>(180, 130, [pic](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().picture(pic, static_cast<int>(180 * scale), static_cast<int>(130 * scale), true);
                if (!t) return 0;
                w = t->width, h = t->height;
                return t->id;
            });
            v->cover = true;
            v->radius = 14;
            box->onClicked = [st, id, pic, box](uint32_t b) {
                if (b == BTN_LEFT && open) open("viewer " + pic);
                if (b != BTN_RIGHT) return;
                floatingMenu(box, {MenuItem("delete", "Take the picture out", "", [st, id, pic] {
                                       Note* x = st->store.find(id);
                                       if (!x) return;
                                       std::error_code ec;
                                       if (pic.starts_with(imagesDir())) fs::remove(pic, ec);
                                       std::erase(x->images, pic);
                                       touch(st, *x);
                                       refresh(st);
                                   }, false, true)});
            };
        }
    }
    if (!n->labels.empty()) {
        auto* chips = sheet->add<Flow>();
        chips->spacing = 6;
        for (const auto& l : n->labels) {
            auto* chip = chips->add<Button>(Flex::Direction::Row);
            pill(chip->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({10, 4, 6, 4})->setSpacing(4);
            chip->add<Label>(l)->setSize(12)->setWeight(500);
            chip->add<Icon>("close", 14);
            chip->onClicked = [st, id, l](uint32_t b) {
                if (b != BTN_LEFT) return;
                if (Note* x = st->store.find(id)) std::erase(x->labels, l), touch(st, *x), refresh(st);
            };
        }
    }
    caption(sheet, std::string("Edited ") + fileops::humanTime(n->modified));
    // Typing begins where it should: a new note's title, else its text
    EventLoop::get().defer([st, title, body, fresh = n->empty()] {
        if (!*st->alive || !st->asking.empty()) return;
        if (fresh || !body) title->focus();
        else body->focus();
    });
}

} // namespace

void notesApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    st->store.load();
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Stretch)->setSpacing(12);
    columns->grow = true;
    auto* sideScroll = columns->add<ScrollView>();
    sideScroll->maxHeight = 1e6f;
    sideScroll->fixedWidth = 230;
    st->side = sideScroll->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8, 4, 8});
    auto* scroll = columns->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    scroll->blurEdges = true;
    st->content = scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({4, 4, 16, 16});
    card->add<Keeper>(st);

    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Notes");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        auto item = [&](const std::string& icon, const std::string& name, bool here, std::function<void()> go, std::function<void(Button*)> extra = {}) {
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({16, 11})->setSpacing(14);
            b->pressedRadius = 12;
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 22)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            auto* l = b->add<Label>(name);
            l->setSize(14)->setWeight(here ? 600 : 500)->setColor(here ? "on_secondary_container" : "on_surface", false)->setMaxWidth(130);
            l->grow = true;
            if (extra) extra(b);
            b->onClicked = [st, go](uint32_t button) {
                if (button != BTN_LEFT) return;
                st->editing.clear();
                go();
                refresh(st);
            };
        };
        const bool editing = !st->editing.empty();
        item("lightbulb", "Notes", st->view == View::Notes && !editing, [st] { st->view = View::Notes; });
        if (!st->store.labels.empty()) caption(s, "  Labels");
        for (const auto& l : st->store.labels)
            item("label", l, st->view == View::Label && st->label == l && !editing, [st, l] {
                st->view = View::Label;
                st->label = l;
            }, [st, l](Button* b) {
                iconButton(b, "close", [st, l] {
                    std::erase(st->store.labels, l);
                    for (auto& n : st->store.notes) std::erase(n.labels, l);
                    if (st->view == View::Label && st->label == l) st->view = View::Notes;
                    st->store.saveSoon();
                    refresh(st);
                }, 16)->tooltip = "Delete the label (its notes stay)";
            });
        // A new label
        auto* add = s->add<Flex>(Flex::Direction::Row);
        add->setPadding({16, 4, 8, 4})->setSpacing(10)->setAlignItems(Align::Center);
        add->add<Icon>("new_label", 22)->setColor("on_surface_variant", false);
        auto* f = add->add<TextField>();
        f->bare = true;
        f->grow = true;
        f->setPlaceholder("New label");
        f->onSubmit = [st](const std::string& t) {
            std::string l = t;
            l.erase(0, l.find_first_not_of(' '));
            l.erase(l.find_last_not_of(' ') + 1);
            if (l.empty() || std::ranges::find(st->store.labels, l) != st->store.labels.end()) return;
            st->store.labels.push_back(l);
            // The open note gets it too
            if (Note* n = st->store.find(st->editing)) n->labels.push_back(l), touch(st, *n);
            st->store.saveSoon();
            refresh(st);
        };
        s->add<Node>()->fixedHeight = 6;
        item("archive", "Archive", st->view == View::Archive && !editing, [st] { st->view = View::Archive; });
        item("delete", "Trash", st->view == View::Trash && !editing, [st] { st->view = View::Trash; });
    };
    st->show = [st] {
        auto* c = st->content;
        c->clearChildren();
        if (!st->editing.empty()) editorView(st, c);
        else gridView(st, c);
    };

    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.baseSym == 'n' || e.baseSym == 'N')) return newNote(st, e.shift()), true;
        if (e.ctrl() && (e.baseSym == 'f' || e.baseSym == 'F')) {
            if (!st->editing.empty()) openNote(st, "");
            EventLoop::get().defer([st] {
                if (*st->alive && st->search) st->search->focus();
            });
            return true;
        }
        if (e.sym == XKB_KEY_Escape && !st->editing.empty()) return openNote(st, ""), true;
        return false;
    };
    notesGo = [st](const std::string& what) {
        if (!*st->alive) return;
        if (what == "new") newNote(st, false);
        else if (what == "checklist") newNote(st, true);
        else if (what.starts_with("note:")) openNote(st, what.substr(5));
    };
    st->sidebar();
    st->show();
    if (arg == "new") newNote(st, false);
    else if (arg == "checklist") newNote(st, true);
    else if (arg.starts_with("note:")) openNote(st, arg.substr(5));
}

// Settings → Notes
void notesSettings(Flex* page) {
    auto* g = group(page, "Notes");
    choiceRow<std::string>(g, "sort", "Order", "Pinned notes stay on top", "/notes/sort", "edited", {{"edited", "Last edited"}, {"created", "Created"}, {"title", "Title"}});
    auto r = row(g, "palette", "Colour of a new note", "");
    std::vector<std::pair<std::string, std::string>> tints;
    for (int i = 0; i < kTintCount; ++i) tints.push_back({std::to_string(i), kTints[i].name});
    dropdown(r.row, g, kTints[std::clamp(Config::get().value<int>("/notes/tint", 0), 0, kTintCount - 1)].name, tints,
             [](const std::string& v) { Config::get().set("/notes/tint", std::stoi(v)); });
    auto* t = group(page, "Trash");
    choiceRow<int>(t, "delete", "Keep notes in the trash", "Then they're deleted for good", "/notes/trashDays", 7, {{7, "7 days"}, {30, "30 days"}, {0, "Until emptied"}});
    caption(page, std::string("Your notes are kept in ") + fileops::displayPath(storePath()));
}

void notesAppClosed() {
    notesGo = nullptr;
}

std::function<void(const std::string&)> notesGo;

} // namespace m3::shell::panels
