#include "services/Notes.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace m3 {

namespace {
std::string file() {
    return paths::stateDir() + "/notes.json";
}
} // namespace

Notes& Notes::get() {
    static Notes instance;
    return instance;
}

Notes::Notes() {
    try {
        std::ifstream in(file());
        if (!in) return;
        for (const auto& i : nlohmann::json::parse(in).value("items", nlohmann::json::array()))
            if (i.is_object() && !i.value("text", "").empty()) m_items.push_back({i.value("text", ""), i.value("done", false)});
    } catch (const std::exception& e) {
        log::warn("notes.json: {}", e.what());
    }
}

void Notes::save() const {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& i : m_items) items.push_back({{"text", i.text}, {"done", i.done}});
    std::error_code ec;
    std::filesystem::create_directories(paths::stateDir(), ec);
    const std::string f = file();
    std::ofstream(f + ".tmp") << nlohmann::json{{"items", items}}.dump(2) << "\n";
    std::filesystem::rename(f + ".tmp", f, ec);
}

void Notes::add(const std::string& text) {
    const auto a = text.find_first_not_of(" \t\n");
    if (a == std::string::npos) return;
    m_items.insert(m_items.begin(), {text.substr(a, text.find_last_not_of(" \t\n") - a + 1), false});
    save();
    changed.emit();
}

void Notes::toggle(size_t i) {
    if (i >= m_items.size()) return;
    m_items[i].done = !m_items[i].done;
    save();
    changed.emit();
}

void Notes::remove(size_t i) {
    if (i >= m_items.size()) return;
    m_items.erase(m_items.begin() + static_cast<long>(i));
    save();
    changed.emit();
}

void Notes::clearDone() {
    if (std::erase_if(m_items, [](const Item& i) { return i.done; }) == 0) return;
    save();
    changed.emit();
}

size_t Notes::open() const {
    return static_cast<size_t>(std::ranges::count(m_items, false, &Item::done));
}

} // namespace m3
