#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace m3 {

// Disconnects on destruction; widgets hold these so a destroyed widget can
// never be called back.
class Connection {
public:
    Connection() = default;
    explicit Connection(std::shared_ptr<bool> alive) : m_alive(std::move(alive)) {}
    Connection(Connection&&) = default;
    Connection& operator=(Connection&& o) noexcept {
        disconnect();
        m_alive = std::move(o.m_alive);
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { disconnect(); }

    void disconnect() {
        if (m_alive) *m_alive = false;
        m_alive.reset();
    }

private:
    std::shared_ptr<bool> m_alive;
};

template <typename... Args>
class Signal {
public:
    [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
        auto alive = std::make_shared<bool>(true);
        m_slots.push_back({alive, std::move(fn)});
        return Connection(alive);
    }

    // For owners that live as long as the program (services, the shell)
    void connectForever(std::function<void(Args...)> fn) { m_slots.push_back({std::make_shared<bool>(true), std::move(fn)}); }

    void emit(Args... args) {
        std::erase_if(m_slots, [](auto& s) { return !*s.alive; });
        // Copy: a slot may connect or disconnect while we iterate
        auto slots = m_slots;
        for (auto& s : slots)
            if (*s.alive) s.fn(args...);
    }

private:
    struct Slot {
        std::shared_ptr<bool> alive;
        std::function<void(Args...)> fn;
    };
    std::vector<Slot> m_slots;
};

} // namespace m3
