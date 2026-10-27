// fwupdmgr's JSON read into devices, releases and sources; apart from the
// service for the unit tests
#include "services/Firmware.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace m3 {

namespace {

std::string str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::vector<std::string> strings(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    const auto it = j.find(key);
    if (it == j.end()) return out;
    if (it->is_string()) out.push_back(it->get<std::string>());
    else if (it->is_array())
        for (const auto& v : *it)
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

// Older fwupd wrote some states as numbers
std::string updateState(const nlohmann::json& j) {
    const auto it = j.find("UpdateState");
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (!it->is_number_integer()) return {};
    static const char* names[] = {"unknown", "pending", "success", "failed", "needs-reboot", "failed-transient"};
    const int n = it->get<int>();
    return n >= 0 && n < 6 ? names[n] : "";
}

Firmware::Release release(const nlohmann::json& r) {
    Firmware::Release out;
    out.version = str(r, "Version");
    out.summary = str(r, "Summary");
    out.description = Firmware::plainText(str(r, "Description"));
    out.urgency = str(r, "Urgency");
    out.remote = str(r, "RemoteId");
    if (const auto it = r.find("Size"); it != r.end() && it->is_number_unsigned()) out.size = it->get<uint64_t>();
    out.flags = strings(r, "Flags");
    out.upgrade = std::ranges::find(out.flags, "is-upgrade") != out.flags.end();
    return out;
}

} // namespace

bool Firmware::Device::has(const std::string& flag) const {
    return std::ranges::find(flags, flag) != flags.end();
}

std::string Firmware::Device::symbol() const {
    static const std::pair<const char*, const char*> map[] = {
        {"computer", "computer"},
        {"drive-harddisk", "hard_drive"},
        {"drive-harddisk-solidstate", "hard_drive"},
        {"input-touchpad", "touchpad_mouse"},
        {"input-mouse", "mouse"},
        {"input-keyboard", "keyboard"},
        {"preferences-desktop-keyboard", "keyboard"},
        {"input-gaming", "sports_esports"},
        {"input-tablet", "stylus"},
        {"battery", "battery_full"},
        {"audio-card", "speaker"},
        {"audio-headset", "headset_mic"},
        {"audio-headphones", "headphones"},
        {"camera-web", "videocam"},
        {"camera-photo", "photo_camera"},
        {"thunderbolt", "bolt"},
        {"network-wired", "lan"},
        {"network-wireless", "wifi"},
        {"network-modem", "cell_tower"},
        {"video-display", "monitor"},
        {"auth-fingerprint", "fingerprint"},
        {"media-removable", "usb"},
        {"usb-hub", "usb"},
        {"usb-receiver", "settings_input_antenna"},
        {"dock", "dock"},
        {"phone", "smartphone"},
        {"printer", "print"},
        {"security-high", "security"},
        {"bluetooth", "bluetooth"},
        {"gpu", "memory"},
        {"application-certificate", "verified_user"}, // Secure Boot's keys and lists
    };
    for (const auto& icon : icons)
        for (const auto& [name, symbol] : map)
            if (icon == name) return symbol;
    if (plugin == "uefi_capsule" || plugin == "uefi_dbx") return "developer_board";
    if (plugin == "cpu" || plugin == "intel_me" || plugin == "msr" || plugin == "amd_pmc") return "memory";
    if (plugin == "tpm") return "encrypted";
    if (plugin == "nvme" || plugin == "ata" || plugin == "scsi") return "hard_drive";
    return "developer_board";
}

std::vector<Firmware::Device> Firmware::parseDevices(const std::string& text) {
    std::vector<Device> out;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (...) {
        return out;
    }
    const auto it = j.find("Devices");
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& d : *it) {
        if (!d.is_object()) continue;
        Device x;
        x.id = str(d, "DeviceId");
        x.name = str(d, "Name");
        x.vendor = str(d, "Vendor");
        x.version = str(d, "Version");
        x.summary = str(d, "Summary");
        x.plugin = str(d, "Plugin");
        x.flags = strings(d, "Flags");
        x.icons = strings(d, "Icons");
        x.problems = strings(d, "Problems");
        x.updateState = updateState(d);
        x.updateError = str(d, "UpdateError");
        if (const auto rs = d.find("Releases"); rs != d.end() && rs->is_array())
            for (const auto& r : *rs)
                if (r.is_object()) x.releases.push_back(release(r));
        // Unnamed ones (a display's own connector) are nothing to show
        if (!x.id.empty() && !x.name.empty()) out.push_back(std::move(x));
    }
    return out;
}

std::string Firmware::problemText(const std::string& p) {
    if (p == "require-ac-power") return "Plug in the charger";
    if (p == "system-power-too-low" || p == "power-too-low") return "Charge the battery first";
    if (p == "lid-is-closed") return "Open the lid";
    if (p == "is-emulated") return "An emulated device";
    if (p == "missing-license") return "Its licence is missing";
    if (p == "system-inhibit") return "Something is keeping it from updating now";
    if (p == "update-in-progress") return "Already updating";
    if (p == "in-use") return "In use";
    if (p == "display-required") return "Connect a display";
    if (p == "lower-priority") return "Another device updates it";
    std::string out = p;
    std::ranges::replace(out, '-', ' ');
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::vector<Firmware::Remote> Firmware::parseRemotes(const std::string& text) {
    std::vector<Remote> out;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (...) {
        return out;
    }
    const auto it = j.find("Remotes");
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& r : *it) {
        if (!r.is_object()) continue;
        Remote x;
        x.id = str(r, "Id");
        x.title = str(r, "Title");
        x.kind = str(r, "Kind");
        // A flag in fwupd 2; a value (true, or "true") before
        const auto flags = strings(r, "Flags");
        x.enabled = std::ranges::find(flags, "enabled") != flags.end();
        if (const auto e = r.find("Enabled"); e != r.end())
            x.enabled = e->is_boolean() ? e->get<bool>() : e->is_string() && e->get<std::string>() == "true";
        // Its list's age: -1 when never downloaded
        for (const char* key : {"ModificationTime", "Mtime"})
            if (const auto m = r.find(key); m != r.end() && m->is_number_integer() && m->get<long long>() > 0) x.modified = m->get<std::time_t>();
        if (x.title.empty()) x.title = x.id;
        if (!x.id.empty()) out.push_back(std::move(x));
    }
    return out;
}

std::string Firmware::plainText(const std::string& markup) {
    std::string out;
    for (size_t i = 0; i < markup.size();) {
        if (markup[i] == '<') {
            const size_t close = markup.find('>', i);
            if (close == std::string::npos) break;
            const std::string tag = markup.substr(i + 1, close - i - 1);
            if (tag == "li") out += "• ";
            else if (tag == "/p" || tag == "/li" || tag == "br" || tag == "br/") out += '\n';
            i = close + 1;
            continue;
        }
        if (markup[i] == '&') {
            static const std::pair<const char*, const char*> entities[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}};
            bool done = false;
            for (const auto& [e, c] : entities)
                if (markup.compare(i, std::char_traits<char>::length(e), e) == 0) {
                    out += c;
                    i += std::char_traits<char>::length(e);
                    done = true;
                    break;
                }
            if (done) continue;
        }
        // Markup's own line breaks and indents are only spacing
        if (markup[i] == '\n' || markup[i] == '\t') {
            if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        } else if (markup[i] != ' ' || (!out.empty() && out.back() != ' ' && out.back() != '\n')) {
            out += markup[i];
        }
        ++i;
    }
    // Trailing spaces, then the lines' ends
    std::string clean;
    for (char c : out) {
        if (c == '\n')
            while (!clean.empty() && clean.back() == ' ') clean.pop_back();
        if (c == '\n' && (clean.empty() || clean.back() == '\n')) continue;
        clean += c;
    }
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
    return clean;
}

} // namespace m3
