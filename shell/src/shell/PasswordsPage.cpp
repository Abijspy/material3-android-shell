// Settings → Passwords. Saved Wi-Fi networks (NetworkManager) and the
// keyring's items (the Secret Service: browsers, mail, git and others keep
// passwords there). A password shows only when you ask, and one copied is
// taken off the clipboard, and out of its history, after 30 seconds.
#include "shell/PasswordsPage.h"

#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Clipboard.h"
#include "services/Secrets.h"
#include "shell/PanelKit.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <map>
#include <memory>
#include <sstream>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

struct PassState {
    std::vector<std::string> wifi;          // saved Wi-Fi connections, by name
    std::map<std::string, std::string> wifiShown; // name → its password, while shown
    std::vector<Secrets::Item> items;
    std::map<std::string, std::string> shown; // item → its password, while shown
    std::string error, query, armed;          // armed: the item a second Delete removes
    bool loading = true, wifiRead = false;
};

// On the clipboard for 30 seconds, then gone from it and its history
void copySecret(const std::string& secret) {
    m3::run({"wl-copy"}, nullptr, secret);
    EventLoop::get().addTimer(30000, [secret] {
        m3::run({"wl-paste", "--no-newline"}, [secret](int code, const std::string& now) {
            if (code == 0 && now == secret) m3::run({"wl-copy", "--clear"}, nullptr);
            auto& c = Clipboard::get();
            c.refresh([secret] {
                auto& cl = Clipboard::get();
                const auto entries = cl.entries();
                for (const auto& e : entries)
                    if (!e.image && !e.preview.empty() && secret.starts_with(e.preview)) cl.remove(e);
            });
        });
    });
}

// nmcli's terse output: fields split at unescaped colons
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out{""};
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) out.back() += line[++i];
        else if (line[i] == ':') out.emplace_back();
        else out.back() += line[i];
    }
    return out;
}

void readWifi(const std::shared_ptr<PassState>& st, Live* live) {
    m3::run({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, [st, live, alive = live->alive()](int, const std::string& out) {
        if (!*alive) return;
        st->wifi.clear();
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
            const auto f = fields(l);
            if (f.size() >= 2 && f[1] == "802-11-wireless") st->wifi.push_back(f[0]);
        }
        std::ranges::sort(st->wifi);
        st->wifiRead = true;
        live->syncSoon();
    });
}

void readKeyring(const std::shared_ptr<PassState>& st, Live* live) {
    st->loading = true;
    Secrets::get().list([st, live, alive = live->alive()](std::vector<Secrets::Item> items, std::string error) {
        if (!*alive) return;
        st->items = std::move(items);
        st->error = std::move(error);
        st->loading = false;
        live->syncSoon();
    });
}

// What an item is for, in words: the app or site, and the account
std::pair<std::string, std::string> describe(const Secrets::Item& it) {
    auto attr = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (auto f = it.attributes.find(k); f != it.attributes.end() && !f->second.empty()) return f->second;
        return std::string();
    };
    std::string account = attr({"username_value", "username", "user", "account", "email", "login"});
    std::string where = attr({"origin_url", "signon_realm", "url", "server", "host", "domain", "service", "application"});
    std::string sub = account;
    if (!where.empty() && where != it.label) sub += (sub.empty() ? "" : "  ·  ") + where;
    if (it.modified > 0) {
        const std::time_t t = static_cast<std::time_t>(it.modified);
        char buf[32];
        std::strftime(buf, sizeof buf, "%-d %b %Y", std::localtime(&t));
        sub += (sub.empty() ? "Saved " : "  ·  ") + std::string(buf);
    }
    return {it.label.empty() ? "(no name)" : it.label, sub};
}

std::string iconFor(const Secrets::Item& it) {
    const auto schema = it.attributes.contains("xdg:schema") ? it.attributes.at("xdg:schema") : "";
    if (it.attributes.contains("origin_url") || schema.find("chrome") != std::string::npos || schema.find("mozilla") != std::string::npos) return "language";
    if (schema.find("Network") != std::string::npos || it.attributes.contains("server")) return "dns";
    if (it.label.find("git") != std::string::npos) return "code";
    return "key";
}

} // namespace

void passwordsPage(Flex* page, Popup&) {
    auto st = std::make_shared<PassState>();
    auto* live = page->add<Live>();
    live->setSpacing(6)->setAlignItems(Align::Stretch);
    live->sync = [live, st] {
        live->clearChildren();

        // ── Wi-Fi
        auto* w = group(live, "Wi-Fi networks", "Saved networks' passwords, to give to a guest or another device");
        if (!st->wifiRead) caption(w, "  Looking…");
        else if (st->wifi.empty()) row(w, "wifi_off", "No saved Wi-Fi networks");
        for (const auto& name : st->wifi) {
            const auto shown = st->wifiShown.find(name);
            auto r = row(w, "wifi", name, shown != st->wifiShown.end() ? shown->second : "••••••••");
            iconButton(r.row, shown != st->wifiShown.end() ? "visibility_off" : "visibility", [st, live, name] {
                if (st->wifiShown.erase(name)) return live->syncSoon();
                // nmcli asks NetworkManager (and polkit, if it must)
                m3::run({"nmcli", "-s", "-g", "802-11-wireless-security.psk", "connection", "show", name}, [st, live, name, alive = live->alive()](int code, const std::string& out) {
                    if (!*alive) return;
                    std::string p = out;
                    while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
                    st->wifiShown[name] = code != 0 ? "Couldn't read it" : p.empty() ? "No password (an open network, or one using a certificate)" : p;
                    live->syncSoon();
                });
            }, 20)->tooltip = shown != st->wifiShown.end() ? "Hide" : "Show the password";
            iconButton(r.row, "content_copy", [name] {
                m3::run({"nmcli", "-s", "-g", "802-11-wireless-security.psk", "connection", "show", name}, [](int code, const std::string& out) {
                    std::string p = out;
                    while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
                    if (code == 0 && !p.empty()) copySecret(p);
                });
            }, 20)->tooltip = "Copy the password (cleared after 30 s)";
        }

    };
    // ── The keyring: the search stays put, the list below it redraws
    auto* g = group(page, "Saved passwords", "What apps saved in your keyring: browsers, mail, git and others");
    auto* box = g->add<Flex>(Flex::Direction::Row);
    box->setPadding({16, 10})->setSpacing(10)->setAlignItems(Align::Center);
    auto* field = box->add<TextField>();
    field->grow = true;
    field->setPlaceholder("Search by name, site or account");
    auto* list = g->add<Live>();
    list->setSpacing(0)->setAlignItems(Align::Stretch);
    field->onChanged = [st, list](const std::string& q) {
        st->query = q;
        list->syncSoon();
    };
    iconButton(box, "refresh", [st, list] { readKeyring(st, list); }, 20)->tooltip = "Read the keyring again";
    caption(page, "  Passwords show only when you ask. One you copy leaves the clipboard, and its history, after 30 seconds.")->setMaxWidth(560)->setMaxLines(2);
    list->sync = [list, st] {
        auto* live = list;
        auto* g = list;
        live->clearChildren();
        if (st->loading) {
            caption(g, "  Reading the keyring…");
            return;
        }
        if (!st->error.empty()) {
            row(g, "lock", "The keyring couldn't be read", st->error);
            return;
        }
        auto lower = [](std::string s) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        const std::string q = lower(st->query);
        size_t shownCount = 0;
        for (const auto& it : st->items) {
            const auto [title, sub] = describe(it);
            if (!q.empty() && lower(title + " " + sub).find(q) == std::string::npos) continue;
            if (++shownCount > 300) break;
            const auto secret = st->shown.find(it.path);
            auto r = row(g, iconFor(it), title, secret != st->shown.end() ? secret->second : sub);
            const std::string path = it.path;
            iconButton(r.row, secret != st->shown.end() ? "visibility_off" : "visibility", [st, live, path] {
                if (st->shown.erase(path)) return live->syncSoon();
                Secrets::get().secret(path, [st, live, path, alive = live->alive()](std::optional<std::string> s) {
                    if (!*alive) return;
                    st->shown[path] = s ? (s->empty() ? "(empty)" : *s) : "Couldn't read it";
                    live->syncSoon();
                });
            }, 20)->tooltip = secret != st->shown.end() ? "Hide" : "Show the password";
            iconButton(r.row, "content_copy", [path] {
                Secrets::get().secret(path, [](std::optional<std::string> s) {
                    if (s && !s->empty()) copySecret(*s);
                });
            }, 20)->tooltip = "Copy the password (cleared after 30 s)";
            const bool armed = st->armed == path;
            auto* del = iconButton(r.row, armed ? "delete_forever" : "delete", [st, live, path] {
                if (st->armed != path) {
                    st->armed = path;
                    return live->syncSoon();
                }
                st->armed.clear();
                Secrets::get().remove(path, [st, live, alive = live->alive()](bool) {
                    if (*alive) readKeyring(st, live);
                });
            }, 20);
            del->tooltip = armed ? "Tap again to delete it for good" : "Delete";
            if (armed) del->setBackground("error_container", false);
        }
        if (st->items.empty()) row(g, "key_off", "Nothing saved in the keyring");
        else if (shownCount == 0) caption(g, "  Nothing matches");
    };
    live->sync();
    list->sync();
    readWifi(st, live);
    readKeyring(st, list);
}

} // namespace m3::shell::panels
