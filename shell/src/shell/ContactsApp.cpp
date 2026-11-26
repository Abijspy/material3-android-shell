// Contacts (material3ui-contacts): your address book, kept as vCards (one
// file other apps and phones read), with the contacts your phone shares
// through KDE Connect alongside, read-only. Search, favourites, each
// contact's numbers, addresses and notes, importing and exporting .vcf.
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
#include "ui/Theme.h"

#include <glib.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> contactsGo;

namespace {

std::string dataHome() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return x && *x ? std::string(x) : paths::home() + "/.local/share";
}
std::string bookFile() {
    return dataHome() + "/material3ui/contacts/contacts.vcf";
}
std::string photoDir() {
    return dataHome() + "/material3ui/contacts/photos";
}

struct Contact {
    std::string uid, name, org, address, birthday, note, photo;
    std::vector<std::pair<std::string, std::string>> phones, emails; // label, value
    bool favourite = false, fromPhone = false;
};

// ── vCard

std::string unescape(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[++i];
            out += n == 'n' || n == 'N' ? '\n' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}
std::string escape(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '\n') out += "\\n";
        else if (c == ',' || c == ';' || c == '\\') out += '\\', out += c;
        else if (c != '\r') out += c;
    }
    return out;
}
std::string labelOf(const std::string& params, bool phone) {
    std::string p = params;
    for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (phone && (p.find("cell") != std::string::npos || p.find("mobile") != std::string::npos)) return "Mobile";
    if (p.find("work") != std::string::npos) return "Work";
    if (p.find("home") != std::string::npos) return "Home";
    return phone ? "Phone" : "Email";
}

std::vector<Contact> parseVcf(const std::string& text, bool fromPhone) {
    // Folded lines (a space or tab at the start) joined first
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!lines.empty() && !l.empty() && (l[0] == ' ' || l[0] == '\t')) lines.back() += l.substr(1);
        else lines.push_back(l);
    }
    std::vector<Contact> out;
    Contact c;
    bool in_card = false;
    for (const auto& l : lines) {
        const size_t colon = l.find(':');
        if (colon == std::string::npos) continue;
        std::string key = l.substr(0, colon), value = l.substr(colon + 1);
        std::string params;
        if (const size_t semi = key.find(';'); semi != std::string::npos) params = key.substr(semi + 1), key = key.substr(0, semi);
        if (const size_t dot = key.find('.'); dot != std::string::npos) key = key.substr(dot + 1); // item1.TEL
        for (char& ch : key) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (key == "BEGIN") {
            c = Contact{};
            c.fromPhone = fromPhone;
            in_card = true;
        } else if (!in_card) {
            continue;
        } else if (key == "END") {
            if (c.name.empty() && !c.phones.empty()) c.name = c.phones.front().second;
            if (!c.name.empty()) {
                if (c.uid.empty()) {
                    gchar* u = g_uuid_string_random();
                    c.uid = u;
                    g_free(u);
                }
                out.push_back(std::move(c));
            }
            in_card = false;
        } else if (key == "UID") c.uid = value;
        else if (key == "FN") c.name = unescape(value);
        else if (key == "N" && c.name.empty()) {
            // Family;Given;…
            const size_t s = value.find(';');
            const std::string family = unescape(value.substr(0, s)), rest = s == std::string::npos ? "" : value.substr(s + 1);
            const std::string given = unescape(rest.substr(0, rest.find(';')));
            c.name = given.empty() ? family : family.empty() ? given : given + " " + family;
        } else if (key == "TEL") c.phones.push_back({labelOf(params, true), unescape(value)});
        else if (key == "EMAIL") c.emails.push_back({labelOf(params, false), unescape(value)});
        else if (key == "ORG") c.org = unescape(value.substr(0, value.find(';')));
        else if (key == "ADR") {
            std::string a;
            std::istringstream parts(value);
            for (std::string part; std::getline(parts, part, ';');)
                if (!part.empty()) a += (a.empty() ? "" : ", ") + unescape(part);
            c.address = a;
        } else if (key == "BDAY") c.birthday = value;
        else if (key == "NOTE") c.note = unescape(value);
        else if (key == "X-M3-FAVOURITE") c.favourite = value == "1";
        else if (key == "PHOTO") {
            // A file, or the picture itself (base64): kept as a file
            if (value.starts_with("file://")) c.photo = value.substr(7);
            else if (!value.starts_with("http")) {
                std::string b64 = value;
                if (const size_t comma = b64.find(','); b64.starts_with("data:") && comma != std::string::npos) b64 = b64.substr(comma + 1);
                gsize n = 0;
                guchar* bytes = g_base64_decode(b64.c_str(), &n);
                if (bytes && n > 64) {
                    std::error_code ec;
                    fs::create_directories(photoDir(), ec);
                    gchar* h = g_compute_checksum_for_data(G_CHECKSUM_MD5, bytes, n);
                    c.photo = photoDir() + "/" + h + ".jpg";
                    g_free(h);
                    if (!fs::exists(c.photo, ec)) std::ofstream(c.photo, std::ios::binary).write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(n));
                }
                g_free(bytes);
            }
        }
    }
    return out;
}

std::string toVcf(const Contact& c) {
    std::string v = "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:" + c.uid + "\r\nFN:" + escape(c.name) + "\r\n";
    const size_t sp = c.name.rfind(' ');
    v += "N:" + escape(sp == std::string::npos ? c.name : c.name.substr(sp + 1)) + ";" + escape(sp == std::string::npos ? "" : c.name.substr(0, sp)) + ";;;\r\n";
    for (const auto& [l, n] : c.phones) v += "TEL;TYPE=" + std::string(l == "Mobile" ? "CELL" : l == "Work" ? "WORK" : l == "Home" ? "HOME" : "VOICE") + ":" + escape(n) + "\r\n";
    for (const auto& [l, e] : c.emails) v += "EMAIL;TYPE=" + std::string(l == "Work" ? "WORK" : "HOME") + ":" + escape(e) + "\r\n";
    if (!c.org.empty()) v += "ORG:" + escape(c.org) + "\r\n";
    if (!c.address.empty()) v += "ADR;TYPE=HOME:;;" + escape(c.address) + ";;;;\r\n";
    if (!c.birthday.empty()) v += "BDAY:" + c.birthday + "\r\n";
    if (!c.note.empty()) v += "NOTE:" + escape(c.note) + "\r\n";
    if (!c.photo.empty()) v += "PHOTO;VALUE=uri:file://" + c.photo + "\r\n";
    if (c.favourite) v += "X-M3-FAVOURITE:1\r\n";
    return v + "END:VCARD\r\n";
}

std::string readAll(const std::string& f) {
    std::ifstream in(f);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct State {
    std::vector<Contact> mine, phone;
    std::string selected, query, armed, note; // selected: a uid
    bool editing = false, importing = false;
    Contact draft;
    Flex *list = nullptr, *detail = nullptr;
    std::function<void()> refresh;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void saveBook(const std::shared_ptr<State>& st) {
    std::error_code ec;
    fs::create_directories(fs::path(bookFile()).parent_path(), ec);
    std::string all;
    for (const auto& c : st->mine) all += toVcf(c);
    paths::writeFile(bookFile(), all);
}

void loadBook(const std::shared_ptr<State>& st) {
    st->mine = parseVcf(readAll(bookFile()), false);
    st->phone.clear();
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(dataHome() + "/kpeoplevcard", ec)) {
        if (!d.path().filename().string().starts_with("kdeconnect-")) continue;
        for (const auto& f : fs::directory_iterator(d.path(), ec))
            if (f.path().extension() == ".vcf")
                for (auto& c : parseVcf(readAll(f.path().string()), true)) st->phone.push_back(std::move(c));
    }
    auto byName = [](const Contact& a, const Contact& b) { return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0; };
    std::ranges::sort(st->mine, byName);
    std::ranges::sort(st->phone, byName);
}

const Contact* find(const std::shared_ptr<State>& st, const std::string& uid) {
    for (const auto* v : {&st->mine, &st->phone})
        for (const auto& c : *v)
            if (c.uid == uid) return &c;
    return nullptr;
}

size_t importFile(const std::shared_ptr<State>& st, const std::string& file) {
    auto found = parseVcf(readAll(file), false);
    size_t added = 0;
    for (auto& c : found) {
        // The same uid, or the same name and number: already here
        const bool dup = std::ranges::any_of(st->mine, [&](const Contact& m) {
            return m.uid == c.uid || (m.name == c.name && m.phones == c.phones && m.emails == c.emails);
        });
        if (dup) continue;
        st->mine.push_back(std::move(c));
        ++added;
    }
    saveBook(st);
    loadBook(st);
    return added;
}

void avatar(Node* parent, const Contact& c, float size) {
    std::error_code ec;
    if (!c.photo.empty() && fs::exists(c.photo, ec)) {
        auto* v = parent->add<ImageView>(size, size, [file = c.photo, size](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().picture(file, static_cast<int>(size * scale), static_cast<int>(size * scale), true);
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        v->cover = true;
        v->radius = size / 2;
        return;
    }
    // Initials in a tint picked by the name
    static const char* tints[] = {"primary_container", "secondary_container", "tertiary_container"};
    static const char* inks[] = {"on_primary_container", "on_secondary_container", "on_tertiary_container"};
    const size_t k = std::hash<std::string>{}(c.name) % 3;
    auto* box = parent->add<Flex>(Flex::Direction::Row);
    box->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(size / 2))->setBackground(tints[k], false);
    box->fixedWidth = box->fixedHeight = size;
    std::string initials;
    std::istringstream words(c.name);
    for (std::string w; words >> w && initials.size() < 8;) {
        const gunichar ch = g_utf8_get_char(w.c_str());
        if (!g_unichar_isalnum(ch)) continue;
        char buf[8];
        initials.append(buf, static_cast<size_t>(g_unichar_to_utf8(g_unichar_toupper(ch), buf)));
        if (g_utf8_strlen(initials.c_str(), -1) >= 2) break;
    }
    box->add<Label>(initials.empty() ? "?" : initials)->setSize(size * 0.38f)->setWeight(600)->setColor(inks[k], false);
}

void copy(const std::string& text, const std::string& what) {
    m3::run({"wl-copy"}, nullptr, text);
    m3::run({"notify-send", "-a", "Contacts", "-i", "x-office-address-book", what + " copied", text}, nullptr);
}

void buildList(const std::shared_ptr<State>& st);
void buildDetail(const std::shared_ptr<State>& st);

void refresh(const std::shared_ptr<State>& st) {
    EventLoop::get().defer([st] {
        if (!*st->alive) return;
        buildList(st);
        buildDetail(st);
    });
}

bool matches(const Contact& c, const std::string& q) {
    if (q.empty()) return true;
    auto low = [](std::string s) {
        gchar* f = g_utf8_casefold(s.c_str(), -1);
        std::string out = f;
        g_free(f);
        return out;
    };
    std::string hay = c.name + " " + c.org + " " + c.note;
    for (const auto& [l, v] : c.phones) hay += " " + v;
    for (const auto& [l, v] : c.emails) hay += " " + v;
    return low(hay).find(low(q)) != std::string::npos;
}

void buildList(const std::shared_ptr<State>& st) {
    auto* l = st->list;
    l->clearChildren();
    auto rowFor = [&](const Contact& c) {
        const bool here = st->selected == c.uid && !st->editing;
        auto* b = l->add<Button>(Flex::Direction::Row);
        b->setPadding({12, 8})->setSpacing(12)->setAlignItems(Align::Center)->setRadius(Radii(16));
        if (here) b->setBackground("secondary_container", false);
        avatar(b, c, 36);
        auto* t = b->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(1);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(c.name)->setSize(14)->setWeight(here ? 600 : 500)->setMaxWidth(200);
        const std::string sub = !c.phones.empty() ? c.phones.front().second : !c.emails.empty() ? c.emails.front().second : c.org;
        if (!sub.empty()) t->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(200);
        if (c.favourite) b->add<Icon>("star", 16)->setFill(true)->setColor("primary", false);
        b->onClicked = [st, uid = c.uid](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            st->selected = uid;
            st->editing = false;
            st->armed.clear();
            refresh(st);
        };
    };
    size_t shown = 0;
    // Favourites, then A to Z under their letters
    bool anyFav = false;
    for (const auto& c : st->mine)
        if (c.favourite && matches(c, st->query)) {
            if (!anyFav) caption(l, "  Favourites"), anyFav = true;
            rowFor(c), ++shown;
        }
    std::string letter;
    for (const auto& c : st->mine) {
        if (!matches(c, st->query)) continue;
        const gunichar ch = g_unichar_toupper(g_utf8_get_char(c.name.c_str()));
        char buf[8];
        std::string first(buf, static_cast<size_t>(g_unichar_to_utf8(g_unichar_isalpha(ch) ? ch : '#', buf)));
        if (first != letter) {
            letter = first;
            caption(l, "  " + letter);
        }
        rowFor(c), ++shown;
    }
    bool anyPhone = false;
    for (const auto& c : st->phone)
        if (matches(c, st->query)) {
            if (!anyPhone) caption(l, "  From your phone (KDE Connect)"), anyPhone = true;
            rowFor(c), ++shown;
        }
    if (shown == 0) caption(l, st->query.empty() ? "  No contacts yet" : "  Nobody matches");
}

void field(Flex* form, const std::string& icon, const std::string& placeholder, std::string& value) {
    auto* r = form->add<Flex>(Flex::Direction::Row);
    r->setSpacing(12)->setAlignItems(Align::Center);
    r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
    auto* f = r->add<TextField>();
    f->grow = true;
    f->setPlaceholder(placeholder);
    f->setText(value);
    f->onChanged = [&value](const std::string& t) { value = t; };
}

void editor(const std::shared_ptr<State>& st) {
    auto* d = st->detail;
    auto& c = st->draft;
    // Three numbers and two addresses, as many as most need
    while (c.phones.size() < 3) c.phones.push_back({c.phones.empty() ? "Mobile" : c.phones.size() == 1 ? "Home" : "Work", ""});
    while (c.emails.size() < 2) c.emails.push_back({c.emails.empty() ? "Home" : "Work", ""});
    d->add<Label>(c.uid.empty() ? "New contact" : "Edit contact")->setSize(24)->setWeight(500);
    auto* form = d->add<Flex>(Flex::Direction::Column);
    form->setSpacing(10)->setAlignItems(Align::Stretch)->setPadding({16, 14})->setRadius(Radii(20))->setBackground("surface_container_high", false);
    field(form, "person", "Name", c.name);
    for (auto& [label, number] : c.phones) field(form, label == "Mobile" ? "smartphone" : label == "Work" ? "business" : "call", label + " number", number);
    for (auto& [label, mail] : c.emails) field(form, "mail", label + " email", mail);
    field(form, "business", "Organisation", c.org);
    field(form, "home", "Address", c.address);
    field(form, "cake", "Birthday (YYYY-MM-DD)", c.birthday);
    field(form, "notes", "Notes", c.note);
    field(form, "image", "A picture (a file's path)", c.photo);
    if (!st->note.empty()) caption(d, st->note)->setColor("error", false);
    auto* btns = d->add<Flex>(Flex::Direction::Row);
    btns->setSpacing(8);
    pillButton(btns, "Save", true, [st] {
        auto c2 = st->draft;
        auto trimmed = [](std::string s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
            return s;
        };
        c2.name = trimmed(c2.name);
        std::erase_if(c2.phones, [&](auto& p) { return (p.second = trimmed(p.second)).empty(); });
        std::erase_if(c2.emails, [&](auto& p) { return (p.second = trimmed(p.second)).empty(); });
        if (c2.photo.starts_with("~/")) c2.photo = paths::home() + c2.photo.substr(1);
        if (c2.name.empty()) {
            st->note = "A contact needs a name";
            return refresh(st);
        }
        st->note.clear();
        if (c2.uid.empty()) {
            gchar* u = g_uuid_string_random();
            c2.uid = u;
            g_free(u);
            st->mine.push_back(c2);
        } else {
            for (auto& m : st->mine)
                if (m.uid == c2.uid) m = c2;
        }
        saveBook(st);
        loadBook(st);
        st->selected = c2.uid;
        st->editing = false;
        refresh(st);
    }, "check");
    pillButton(btns, "Cancel", false, [st] {
        st->editing = false;
        st->note.clear();
        refresh(st);
    });
}

void importer(const std::shared_ptr<State>& st) {
    auto* d = st->detail;
    d->add<Label>("Import contacts")->setSize(24)->setWeight(500);
    caption(d, "From a .vcf file: one exported from a phone, Google Contacts or another address book. Ones already here are skipped.");
    std::vector<std::string> files;
    std::error_code ec;
    for (const char* sub : {"/Downloads", "/Documents", ""})
        for (const auto& e : fs::directory_iterator(paths::home() + sub, ec))
            if (e.path().extension() == ".vcf" || e.path().extension() == ".VCF") files.push_back(e.path().string());
    auto* g = d->add<Flex>(Flex::Direction::Column);
    g->setSpacing(4)->setAlignItems(Align::Stretch);
    for (const auto& f : files) {
        auto* row = g->add<Flex>(Flex::Direction::Row);
        row->setPadding({14, 8})->setSpacing(12)->setAlignItems(Align::Center)->setRadius(Radii(16))->setBackground("surface_container_high", false);
        row->add<Icon>("contact_page", 22)->setColor("primary", false);
        auto* name = row->add<Label>(fileops::displayPath(f));
        name->setSize(13)->setMaxWidth(380);
        name->grow = true;
        name->givesWay = true;
        pillButton(row, "Import", true, [st, f] {
            const size_t n = importFile(st, f);
            st->importing = false;
            st->note = n ? std::format("{} contacts imported", n) : "Nothing new in that file";
            refresh(st);
        });
    }
    if (files.empty()) caption(d, "No .vcf files in Downloads or Documents: open one from Files, or type its path");
    auto* r = d->add<Flex>(Flex::Direction::Row);
    r->setSpacing(10)->setAlignItems(Align::Center);
    auto* f = r->add<TextField>();
    f->grow = true;
    f->setPlaceholder("Or a file's path, then Enter");
    f->onSubmit = [st](const std::string& p) {
        std::string path = p.starts_with("~/") ? paths::home() + p.substr(1) : p;
        std::error_code ec2;
        if (!fs::is_regular_file(path, ec2)) {
            st->note = "No such file";
            return refresh(st);
        }
        const size_t n = importFile(st, path);
        st->importing = false;
        st->note = n ? std::format("{} contacts imported", n) : "Nothing new in that file";
        refresh(st);
    };
    pillButton(d, "Cancel", false, [st] {
        st->importing = false;
        refresh(st);
    });
}

void buildDetail(const std::shared_ptr<State>& st) {
    auto* d = st->detail;
    d->clearChildren();
    if (st->editing) return editor(st);
    if (st->importing) return importer(st);
    if (!st->note.empty()) caption(d, st->note);
    const Contact* c = find(st, st->selected);
    if (!c) {
        auto* e = d->add<Flex>(Flex::Direction::Column);
        e->setSpacing(10)->setPadding({0, 80})->setAlignItems(Align::Center);
        e->add<Icon>("contacts", 56)->setColor("primary", false);
        e->add<Label>(st->mine.empty() && st->phone.empty() ? "Your contacts go here" : "Pick someone")->setSize(20)->setWeight(600);
        caption(e, std::format("{} contacts{}", st->mine.size(), st->phone.empty() ? "" : std::format(", and {} from your phone", st->phone.size())));
        return;
    }
    auto* head = d->add<Flex>(Flex::Direction::Column);
    head->setSpacing(8)->setAlignItems(Align::Center)->setPadding({0, 12});
    avatar(head, *c, 96);
    head->add<Label>(c->name)->setSize(24)->setWeight(600)->setMaxWidth(460)->setCentered(true)->setMaxLines(2);
    if (!c->org.empty()) head->add<Label>(c->org)->setSize(14)->setColor("on_surface_variant", false);
    auto* actions = head->add<Flex>(Flex::Direction::Row);
    actions->setSpacing(8);
    if (!c->fromPhone) {
        auto* star = pillButton(actions, c->favourite ? "Favourite" : "Add to favourites", c->favourite, [st, uid = c->uid] {
            for (auto& m : st->mine)
                if (m.uid == uid) m.favourite = !m.favourite;
            saveBook(st);
            refresh(st);
        }, "star");
        (void)star;
        pillButton(actions, "Edit", false, [st, uid = c->uid] {
            if (const Contact* x = find(st, uid)) st->draft = *x;
            st->editing = true;
            refresh(st);
        }, "edit");
    } else {
        pillButton(actions, "Keep a copy", false, [st, uid = c->uid] {
            if (const Contact* x = find(st, uid)) {
                Contact copyOf = *x;
                copyOf.fromPhone = false;
                gchar* u = g_uuid_string_random();
                copyOf.uid = u;
                g_free(u);
                st->mine.push_back(copyOf);
                saveBook(st);
                loadBook(st);
                st->selected = copyOf.uid;
                refresh(st);
            }
        }, "person_add");
    }
    auto* g = d->add<Flex>(Flex::Direction::Column);
    g->setRadius(Radii(20))->setBackground("surface_container_high", false)->setPadding({6, 6})->setAlignItems(Align::Stretch);
    auto line = [&](const std::string& icon, const std::string& value, const std::string& label, std::function<void()> extra = {}, const std::string& extraIcon = {}) {
        auto* r = g->add<Flex>(Flex::Direction::Row);
        r->setPadding({14, 8})->setSpacing(14)->setAlignItems(Align::Center);
        r->add<Icon>(icon, 22)->setColor("primary", false);
        auto* t = r->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(1);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(value)->setSize(15)->setMaxWidth(420)->setMaxLines(kAllLines);
        t->add<Label>(label)->setSize(12)->setColor("on_surface_variant", false);
        if (extra) iconButton(r, extraIcon, extra, 20)->tooltip = extraIcon == "mail" ? "Write an email" : "Open";
        iconButton(r, "content_copy", [value, label] { copy(value, label); }, 20)->tooltip = "Copy";
    };
    for (const auto& [l, n] : c->phones) line(l == "Mobile" ? "smartphone" : "call", n, l);
    for (const auto& [l, e] : c->emails) line("mail", e, l + " email", [e] { m3::run({"xdg-open", "mailto:" + e}, nullptr); }, "mail");
    if (!c->address.empty()) line("home", c->address, "Address");
    if (!c->birthday.empty()) line("cake", c->birthday, "Birthday");
    if (!c->note.empty()) line("notes", c->note, "Notes");
    if (g->children().empty()) caption(g, "  Nothing else saved for them");
    if (c->fromPhone) {
        caption(d, "  Shared by your phone through KDE Connect: change it there");
        return;
    }
    const bool armed = st->armed == c->uid;
    auto* del = pillButton(d, armed ? "Tap again to delete" : "Delete", false, [st, uid = c->uid] {
        if (st->armed != uid) {
            st->armed = uid;
            return refresh(st);
        }
        std::erase_if(st->mine, [&](const Contact& m) { return m.uid == uid; });
        st->armed.clear();
        st->selected.clear();
        saveBook(st);
        refresh(st);
    }, "delete");
    if (armed) del->setBackground("error_container", false);
}

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override { *m_st->alive = false; }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void contactsApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    loadBook(st);
    card->setPadding(Insets(12))->setSpacing(8);
    card->add<Keeper>(st);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(14)->setAlignItems(Align::Stretch);
    columns->grow = true;
    auto* side = columns->add<Flex>(Flex::Direction::Column);
    side->setSpacing(8)->setAlignItems(Align::Stretch);
    side->fixedWidth = 320;
    auto* top = side->add<Flex>(Flex::Direction::Row);
    top->setSpacing(6)->setAlignItems(Align::Center);
    auto* brand = top->add<Label>("Contacts");
    brand->setSize(26)->setWeight(400);
    brand->grow = true;
    iconButton(top, "person_add", [st] {
        st->draft = Contact{};
        st->editing = true;
        st->importing = false;
        st->note.clear();
        refresh(st);
    }, 22)->tooltip = "New contact (Ctrl+N)";
    Button* more = iconButton(top, "more_vert", [] {}, 22);
    more->onClicked = [st, more](uint32_t b) {
        if (b != BTN_LEFT) return;
        std::vector<MenuItem> m;
        m.emplace_back("upload_file", "Import a .vcf file…", "", [st] {
            st->importing = true;
            st->editing = false;
            st->note.clear();
            refresh(st);
        });
        m.emplace_back("download", "Export all as a .vcf file", "", [st] {
            const char* docs = std::getenv("XDG_DOCUMENTS_DIR");
            const std::string dir = docs && *docs ? docs : paths::home() + "/Documents";
            std::error_code ec;
            fs::create_directories(dir, ec);
            const std::string name = fileops::freeName(dir, "Contacts.vcf");
            std::string all;
            for (const auto& c : st->mine) all += toVcf(c);
            paths::writeFile(dir + "/" + name, all);
            m3::run({"notify-send", "-a", "Contacts", "-i", "x-office-address-book", std::format("{} contacts exported", st->mine.size()), fileops::displayPath(dir + "/" + name)}, nullptr);
        }, false, false, !st->mine.empty());
        floatingMenu(more, m);
    };
    auto* box = side->add<Flex>(Flex::Direction::Row);
    pill(box->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({14, 4, 8, 4})->setSpacing(8);
    box->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* search = box->add<TextField>();
    search->bare = true;
    search->grow = true;
    search->setPlaceholder("Search names, numbers and emails");
    search->onChanged = [st](const std::string& q) {
        st->query = q;
        EventLoop::get().defer([st] {
            if (*st->alive) buildList(st);
        });
    };
    popup.initialFocus = search;
    auto* listScroll = side->add<ScrollView>();
    listScroll->maxHeight = 1e6f;
    listScroll->grow = true;
    st->list = listScroll->add<Flex>(Flex::Direction::Column);
    st->list->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({0, 0, 8, 8});
    auto* detailScroll = columns->add<ScrollView>();
    detailScroll->maxHeight = 1e6f;
    detailScroll->grow = true;
    st->detail = detailScroll->add<Flex>(Flex::Direction::Column);
    st->detail->setSpacing(12)->setAlignItems(Align::Stretch)->setPadding({8, 8, 16, 16});
    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.baseSym == 'n' || e.baseSym == 'N')) {
            st->draft = Contact{};
            st->editing = true;
            refresh(st);
            return true;
        }
        if (e.sym == XKB_KEY_Escape && (st->editing || st->importing)) {
            st->editing = st->importing = false;
            refresh(st);
            return true;
        }
        return false;
    };
    contactsGo = [st](const std::string& f) {
        if (!*st->alive || f.empty()) return;
        const size_t n = importFile(st, f);
        st->note = n ? std::format("{} contacts imported from {}", n, fs::path(f).filename().string()) : "Nothing new in " + fs::path(f).filename().string();
        refresh(st);
    };
    buildList(st);
    buildDetail(st);
    if (!arg.empty()) contactsGo(arg);
}

void contactsClosed() {
    contactsGo = nullptr;
}

void contactsSettings(Flex* page) {
    auto* g = group(page, "Your contacts");
    row(g, "contact_page", "Kept as vCards", fileops::displayPath(bookFile()));
    caption(page, "  Contacts your phone shares through KDE Connect show too, read-only. Export them all from the menu (⋮) to move them elsewhere.");
}

} // namespace m3::shell::panels
