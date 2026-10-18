#include "core/EventLoop.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>

namespace m3 {

EventLoop& EventLoop::get() {
    static EventLoop instance;
    return instance;
}

void EventLoop::add(EventSource* source) {
    if (std::find(m_sources.begin(), m_sources.end(), source) == m_sources.end()) m_sources.push_back(source);
}

void EventLoop::remove(EventSource* source) {
    // Null the slot rather than erase: remove() may run inside dispatch()
    std::replace(m_sources.begin(), m_sources.end(), source, static_cast<EventSource*>(nullptr));
}

EventLoop::TimerId EventLoop::addTimer(int ms, std::function<void()> fn, bool repeat) {
    const TimerId id = m_nextId++;
    m_timers[id] = Timer{Clock::now() + std::chrono::milliseconds(ms), ms, repeat, std::move(fn)};
    return id;
}

void EventLoop::cancel(TimerId id) {
    m_timers.erase(id);
}

void EventLoop::debounce(TimerId& id, int ms, std::function<void()> fn) {
    if (id) cancel(id);
    id = addTimer(ms, [&id, fn = std::move(fn)] {
        id = 0;
        fn();
    });
}

void EventLoop::defer(std::function<void()> fn) {
    m_deferred.push_back(std::move(fn));
}

int EventLoop::nextTimeoutMs() const {
    if (!m_deferred.empty()) return 0;
    if (m_timers.empty()) return -1;
    auto due = Clock::time_point::max();
    for (const auto& [_, t] : m_timers) due = std::min(due, t.due);
    const auto ms = std::chrono::ceil<std::chrono::milliseconds>(due - Clock::now()).count();
    return static_cast<int>(std::max<long long>(0, ms));
}

void EventLoop::runTimers() {
    const auto now = Clock::now();
    std::vector<TimerId> due;
    for (const auto& [id, t] : m_timers)
        if (t.due <= now) due.push_back(id);
    for (TimerId id : due) {
        auto it = m_timers.find(id);
        if (it == m_timers.end()) continue; // cancelled by an earlier timer
        auto fn = it->second.fn;
        if (it->second.repeat)
            it->second.due = now + std::chrono::milliseconds(it->second.intervalMs);
        else
            m_timers.erase(it);
        fn();
    }
}

int EventLoop::run() {
    m_running = true;
    std::vector<pollfd> fds;
    std::vector<EventSource*> polled;
    while (m_running) {
        auto deferred = std::move(m_deferred);
        m_deferred.clear();
        for (auto& fn : deferred) fn();

        std::erase(m_sources, nullptr);
        fds.clear();
        polled.clear();
        for (auto* s : m_sources) {
            s->prepare();
            fds.push_back(pollfd{s->fd(), s->events(), 0});
            polled.push_back(s);
        }

        if (::poll(fds.data(), fds.size(), nextTimeoutMs()) < 0 && errno != EINTR) {
            std::cerr << "material3ui: poll: " << std::strerror(errno) << '\n';
            return 1;
        }

        for (size_t i = 0; i < polled.size(); ++i) {
            // Skip sources removed by an earlier dispatch this iteration
            if (std::find(m_sources.begin(), m_sources.end(), polled[i]) == m_sources.end()) continue;
            // Always called: sources like Wayland must finish a prepared read
            polled[i]->dispatch(fds[i].revents);
        }
        runTimers();
    }
    return m_exitCode;
}

void EventLoop::quit(int code) {
    m_exitCode = code;
    m_running = false;
}

} // namespace m3
