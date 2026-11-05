#include "services/LockKeys.h"

#include <filesystem>
#include <fstream>

namespace m3 {

LockKeys& LockKeys::get() {
    static LockKeys instance;
    return instance;
}

void LockKeys::start(std::function<void(const std::string& which, bool on)> changed) {
    if (m_timer) return;
    m_changed = std::move(changed);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/leds", ec)) {
        const std::string name = e.path().filename().string();
        if (m_caps.empty() && name.ends_with("::capslock")) m_caps = (e.path() / "brightness").string();
        if (m_num.empty() && name.ends_with("::numlock")) m_num = (e.path() / "brightness").string();
    }
    if (m_caps.empty() && m_num.empty()) return; // no lights to read
    poll();
    m_timer = EventLoop::get().addTimer(300, [this] { poll(); }, true);
}

void LockKeys::poll() {
    auto read = [](const std::string& file) {
        int v = -1;
        if (!file.empty()) std::ifstream(file) >> v;
        return v < 0 ? -1 : v > 0 ? 1 : 0;
    };
    const int caps = read(m_caps), num = read(m_num);
    // The first look only learns how they are
    if (m_capsOn >= 0 && caps >= 0 && caps != m_capsOn && m_changed) m_changed("caps", caps == 1);
    if (m_numOn >= 0 && num >= 0 && num != m_numOn && m_changed) m_changed("num", num == 1);
    m_capsOn = caps;
    m_numOn = num;
}

} // namespace m3
