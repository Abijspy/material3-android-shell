#include "core/EventLoop.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace m3 {

EventLoop& EventLoop::get() {
    static EventLoop instance;
    return instance;
}

EventLoop::EventLoop() : m_wakeFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {}

void EventLoop::post(std::function<void()> fn) {
    {
        std::lock_guard lock(m_postMutex);
        m_posted.push_back(std::move(fn));
    }
    const uint64_t one = 1;
    [[maybe_unused]] auto n = ::write(m_wakeFd, &one, sizeof one);
}

void EventLoop::drainPosted() {
    uint64_t count;
    [[maybe_unused]] auto n = ::read(m_wakeFd, &count, sizeof count);
    std::vector<std::function<void()>> posted;
    {
        std::lock_guard lock(m_postMutex);
        posted.swap(m_posted);
    }
    for (auto& fn : posted) fn();
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
        fds.push_back(pollfd{m_wakeFd, POLLIN, 0});
        for (auto* s : m_sources) {
            s->prepare();
            fds.push_back(pollfd{s->fd(), s->events(), 0});
            polled.push_back(s);
        }

        int timeout = nextTimeoutMs();
        for (auto* s : polled) {
            const int t = s->timeoutMs();
            if (t >= 0 && (timeout < 0 || t < timeout)) timeout = t;
        }
        if (::poll(fds.data(), fds.size(), timeout) < 0 && errno != EINTR) {
            std::cerr << "material3ui: poll: " << std::strerror(errno) << '\n';
            return 1;
        }

        if (fds[0].revents) drainPosted();
        for (size_t i = 0; i < polled.size(); ++i) {
            // Skip sources removed by an earlier dispatch this iteration
            if (std::find(m_sources.begin(), m_sources.end(), polled[i]) == m_sources.end()) continue;
            // Always called: sources like Wayland must finish a prepared read
            polled[i]->dispatch(fds[i + 1].revents);
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
