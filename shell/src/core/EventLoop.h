#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace m3 {

// Anything with a file descriptor the loop should wait on.
class EventSource {
public:
    virtual ~EventSource() = default;

    virtual int fd() const = 0;
    virtual short events() const { return 0x001; } // POLLIN
    // Called before poll(); the place to flush pending writes (Wayland).
    virtual void prepare() {}
    // Milliseconds until this source needs dispatching anyway; -1: never
    virtual int timeoutMs() const { return -1; }
    // Called after poll() with the returned revents (may be 0).
    virtual void dispatch(short revents) = 0;
};

// Single-threaded poll(2) loop with timers. Everything in the shell runs on it.
class EventLoop {
public:
    using Clock = std::chrono::steady_clock;
    using TimerId = std::uint64_t;

    static EventLoop& get();

    void add(EventSource* source);
    void remove(EventSource* source);

    // One-shot unless repeat; returns an id for cancel().
    TimerId addTimer(int ms, std::function<void()> fn, bool repeat = false);
    void cancel(TimerId id);
    // Restart-or-start a named one-shot: the usual debounce.
    void debounce(TimerId& id, int ms, std::function<void()> fn);

    // Run on the next iteration, after the current dispatch finishes.
    void defer(std::function<void()> fn);
    // Thread-safe defer: how worker threads (audio) hand results to the loop.
    void post(std::function<void()> fn);

    int run();
    void quit(int code = 0);

private:
    struct Timer {
        Clock::time_point due;
        int intervalMs;
        bool repeat;
        std::function<void()> fn;
    };

    EventLoop();
    int nextTimeoutMs() const;
    void runTimers();
    void drainPosted();

    std::vector<EventSource*> m_sources;
    std::map<TimerId, Timer> m_timers;
    std::vector<std::function<void()>> m_deferred;
    TimerId m_nextId = 1;
    std::mutex m_postMutex;
    std::vector<std::function<void()>> m_posted;
    int m_wakeFd = -1;
    bool m_running = false;
    int m_exitCode = 0;
};

} // namespace m3
