#pragma once

#include "dbus/Bus.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// The keyring, through the Secret Service (GNOME Keyring, KWallet's
// compatibility, KeePassXC): what apps saved there, each one's password on
// request, and deleting one. A locked keyring asks to be unlocked with its
// own prompt.
class Secrets {
public:
    struct Item {
        std::string path, label;
        std::map<std::string, std::string> attributes;
        uint64_t modified = 0; // seconds since 1970
    };

    static Secrets& get();
    bool available() const;

    // Every item, by label; `error` set when the keyring couldn't be read
    void list(std::function<void(std::vector<Item> items, std::string error)> done);
    // One item's password (nothing when it couldn't be had)
    void secret(const std::string& item, std::function<void(std::optional<std::string>)> done);
    void remove(const std::string& item, std::function<void(bool ok)> done);
    // An app's own: saved (replacing one with the same attributes) in the
    // default keyring, and found again by those attributes
    void store(const std::string& label, const std::map<std::string, std::string>& attributes, const std::string& secret,
               std::function<void(bool ok)> done);
    void lookup(const std::map<std::string, std::string>& attributes, std::function<void(std::optional<std::string>)> done);

private:
    Secrets() = default;
    void withSession(std::function<void(bool ok)> then);
    void prompt(const std::string& path, std::function<void(bool done)> then);
    void readItems(std::vector<std::string> paths, std::function<void(std::vector<Item>, std::string)> done);

    std::string m_session;
    std::vector<dbus::Subscription> m_prompts;
};

} // namespace m3
