#include "services/Secrets.h"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <memory>

namespace m3 {

namespace {
constexpr const char* kService = "org.freedesktop.secrets";
constexpr const char* kPath = "/org/freedesktop/secrets";
constexpr const char* kIface = "org.freedesktop.Secret.Service";

std::string text(const dbus::Value& v) {
    return v.is_string() ? v.get<std::string>() : std::string();
}
} // namespace

Secrets& Secrets::get() {
    static Secrets s;
    return s;
}

bool Secrets::available() const {
    return dbus::Bus::session().connected();
}

// A "plain" session: secrets come back as they are, over your own session bus
void Secrets::withSession(std::function<void(bool ok)> then) {
    if (!m_session.empty()) return then(true);
    dbus::Bus::session().call(kService, kPath, kIface, "OpenSession",
        [](sd_bus_message* m) {
            sd_bus_message_append(m, "s", "plain");
            sd_bus_message_append(m, "v", "s", "");
        },
        [this, then](const dbus::Value& args, const std::string& error) {
            if (error.empty() && args.is_array() && args.size() >= 2) m_session = text(args[1]);
            then(!m_session.empty());
        });
}

// The keyring's own dialog (unlocking, confirming a delete); `then(true)`
// once it's answered yes
void Secrets::prompt(const std::string& path, std::function<void(bool)> then) {
    if (path.empty() || path == "/") return then(true);
    auto fired = std::make_shared<bool>(false);
    m_prompts.push_back(dbus::Bus::session().subscribe(kService, path, "org.freedesktop.Secret.Prompt", "Completed",
        [then, fired](const dbus::Value& args, sd_bus_message*) {
            if (*fired) return;
            *fired = true;
            const bool dismissed = args.is_array() && !args.empty() && args[0].is_boolean() && args[0].get<bool>();
            then(!dismissed);
        }));
    dbus::Bus::session().call(kService, path, "org.freedesktop.Secret.Prompt", "Prompt", dbus::args(std::string()));
}

void Secrets::list(std::function<void(std::vector<Item>, std::string)> done) {
    if (!available()) return done({}, "No session bus");
    withSession([this, done](bool ok) {
        if (!ok) return done({}, "No keyring answers (GNOME Keyring, KWallet or KeePassXC's Secret Service)");
        dbus::Bus::session().call(kService, kPath, kIface, "SearchItems", [](sd_bus_message* m) { sd_bus_message_append(m, "a{ss}", 0); },
            [this, done](const dbus::Value& args, const std::string& error) {
                if (!error.empty() || !args.is_array() || args.size() < 2) return done({}, error.empty() ? "The keyring didn't answer" : error);
                std::vector<std::string> unlocked, locked;
                for (const auto& p : args[0]) unlocked.push_back(text(p));
                for (const auto& p : args[1]) locked.push_back(text(p));
                if (locked.empty()) return readItems(unlocked, done);
                // Locked ones: unlocked with the keyring's own prompt, then all read
                dbus::Bus::session().call(kService, kPath, kIface, "Unlock",
                    [locked](sd_bus_message* m) {
                        sd_bus_message_open_container(m, 'a', "o");
                        for (const auto& p : locked) sd_bus_message_append(m, "o", p.c_str());
                        sd_bus_message_close_container(m);
                    },
                    [this, done, unlocked, locked](const dbus::Value& a, const std::string&) {
                        std::vector<std::string> now = unlocked;
                        if (a.is_array() && !a.empty())
                            for (const auto& p : a[0]) now.push_back(text(p));
                        const std::string pr = a.is_array() && a.size() >= 2 ? text(a[1]) : "/";
                        if (pr == "/") return readItems(now, done);
                        prompt(pr, [this, done, unlocked, locked](bool yes) {
                            auto all = unlocked;
                            if (yes) all.insert(all.end(), locked.begin(), locked.end());
                            readItems(all, done);
                        });
                    });
            });
    });
}

void Secrets::readItems(std::vector<std::string> paths, std::function<void(std::vector<Item>, std::string)> done) {
    if (paths.empty()) return done({}, "");
    auto items = std::make_shared<std::vector<Item>>();
    auto left = std::make_shared<size_t>(paths.size());
    for (const auto& path : paths) {
        dbus::Bus::session().getAll(kService, path, "org.freedesktop.Secret.Item", [items, left, done, path](const dbus::Value& p, bool ok) {
            if (ok && !(p.contains("Locked") && p["Locked"].is_boolean() && p["Locked"].get<bool>())) {
                Item it;
                it.path = path;
                it.label = p.contains("Label") ? text(p["Label"]) : "";
                if (p.contains("Attributes") && p["Attributes"].is_object())
                    for (const auto& [k, v] : p["Attributes"].items()) it.attributes[k] = text(v);
                if (p.contains("Modified") && p["Modified"].is_number()) it.modified = p["Modified"].get<uint64_t>();
                items->push_back(std::move(it));
            }
            if (--*left == 0) {
                std::ranges::sort(*items, [](const Item& a, const Item& b) { return a.label < b.label; });
                done(std::move(*items), "");
            }
        });
    }
}

void Secrets::secret(const std::string& item, std::function<void(std::optional<std::string>)> done) {
    withSession([this, item, done](bool ok) {
        if (!ok) return done(std::nullopt);
        dbus::Bus::session().call(kService, item, "org.freedesktop.Secret.Item", "GetSecret", dbus::args(dbus::ObjectPath{m_session}),
            [done](const dbus::Value& args, const std::string& error) {
                // (session, parameters, value, content type)
                if (!error.empty() || !args.is_array() || args.empty() || !args[0].is_array() || args[0].size() < 3) return done(std::nullopt);
                const auto& v = args[0][2];
                if (v.is_binary()) {
                    const auto& b = v.get_binary();
                    return done(std::string(b.begin(), b.end()));
                }
                if (v.is_array()) {
                    std::string s;
                    for (const auto& c : v)
                        if (c.is_number()) s += static_cast<char>(c.get<int>());
                    return done(s);
                }
                done(std::nullopt);
            });
    });
}

void Secrets::store(const std::string& label, const std::map<std::string, std::string>& attributes, const std::string& secret,
                    std::function<void(bool)> done) {
    withSession([this, label, attributes, secret, done](bool ok) {
        if (!ok) return done(false);
        const std::string session = m_session;
        dbus::Bus::session().call(kService, "/org/freedesktop/secrets/aliases/default", "org.freedesktop.Secret.Collection", "CreateItem",
            [label, attributes, secret, session](sd_bus_message* m) {
                // a{sv}: the label and the attributes (a{ss} in a variant)
                sd_bus_message_open_container(m, 'a', "{sv}");
                sd_bus_message_append(m, "{sv}", "org.freedesktop.Secret.Item.Label", "s", label.c_str());
                sd_bus_message_open_container(m, 'e', "sv");
                sd_bus_message_append(m, "s", "org.freedesktop.Secret.Item.Attributes");
                sd_bus_message_open_container(m, 'v', "a{ss}");
                sd_bus_message_open_container(m, 'a', "{ss}");
                for (const auto& [k, v] : attributes) sd_bus_message_append(m, "{ss}", k.c_str(), v.c_str());
                sd_bus_message_close_container(m);
                sd_bus_message_close_container(m);
                sd_bus_message_close_container(m);
                sd_bus_message_close_container(m);
                // (oayays): the session, no parameters, the secret, its type
                sd_bus_message_open_container(m, 'r', "oayays");
                sd_bus_message_append(m, "o", session.c_str());
                sd_bus_message_append_array(m, 'y', nullptr, 0);
                sd_bus_message_append_array(m, 'y', secret.data(), secret.size());
                sd_bus_message_append(m, "s", "text/plain");
                sd_bus_message_close_container(m);
                sd_bus_message_append(m, "b", 1); // replace
            },
            [this, done](const dbus::Value& args, const std::string& error) {
                if (!error.empty()) return done(false);
                const std::string pr = args.is_array() && args.size() >= 2 ? text(args[1]) : "/";
                prompt(pr, done);
            });
    });
}

void Secrets::lookup(const std::map<std::string, std::string>& attributes, std::function<void(std::optional<std::string>)> done) {
    withSession([this, attributes, done](bool ok) {
        if (!ok) return done(std::nullopt);
        dbus::Bus::session().call(kService, kPath, kIface, "SearchItems",
            [attributes](sd_bus_message* m) {
                sd_bus_message_open_container(m, 'a', "{ss}");
                for (const auto& [k, v] : attributes) sd_bus_message_append(m, "{ss}", k.c_str(), v.c_str());
                sd_bus_message_close_container(m);
            },
            [this, done](const dbus::Value& args, const std::string& error) {
                if (!error.empty() || !args.is_array() || args.size() < 2) return done(std::nullopt);
                std::string item;
                if (!args[0].empty()) item = text(args[0][0]);
                if (item.empty() && !args[1].empty()) {
                    // Locked: unlocked with the keyring's prompt first
                    const std::string locked = text(args[1][0]);
                    dbus::Bus::session().call(kService, kPath, kIface, "Unlock",
                        [locked](sd_bus_message* m) {
                            sd_bus_message_open_container(m, 'a', "o");
                            sd_bus_message_append(m, "o", locked.c_str());
                            sd_bus_message_close_container(m);
                        },
                        [this, done, locked](const dbus::Value& a, const std::string&) {
                            const std::string pr = a.is_array() && a.size() >= 2 ? text(a[1]) : "/";
                            prompt(pr, [this, done, locked](bool yes) {
                                if (!yes) return done(std::nullopt);
                                secret(locked, done);
                            });
                        });
                    return;
                }
                if (item.empty()) return done(std::nullopt);
                secret(item, done);
            });
    });
}

void Secrets::remove(const std::string& item, std::function<void(bool)> done) {
    dbus::Bus::session().call(kService, item, "org.freedesktop.Secret.Item", "Delete", {}, [this, done](const dbus::Value& args, const std::string& error) {
        if (!error.empty()) return done(false);
        const std::string pr = args.is_array() && !args.empty() ? text(args[0]) : "/";
        prompt(pr, done);
    });
}

} // namespace m3
