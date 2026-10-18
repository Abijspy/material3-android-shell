#pragma once

#include "core/Signal.h"

#include <map>
#include <string>

namespace m3 {

// The session's polkit authentication agent. libpolkit-agent runs on its own
// GLib thread; requests and prompts reach the event loop as plain data.
class Polkit {
public:
    struct Request {
        int id = 0;
        std::string action, message, user;
        std::string prompt = "Password"; // PAM's question
        bool echo = false;               // show what is typed (non-secret prompts)
        std::string error, info;
        bool busy = false;
    };

    static Polkit& get();
    void start();

    const Request* find(int id) const;
    void respond(int id, const std::string& answer);
    void cancel(int id);

    Signal<int> requested; // show a dialog
    Signal<int> updated;   // prompt, error or busy state changed
    Signal<int> finished;  // close it

    // Called from the GLib thread through EventLoop::post
    void apply(const Request& r, bool isNew, bool done);

private:
    Polkit() = default;
    std::map<int, Request> m_requests;
    bool m_started = false;
};

} // namespace m3
