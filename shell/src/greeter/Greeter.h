#pragma once

#include "core/EventLoop.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace m3::wl {
struct Output;
class LayerSurface;
} // namespace m3::wl

namespace m3::shell {
class LockView;
}

namespace m3::greeter {

// `material3ui greeter`: the login screen for greetd. Same face as the lock
// screen, on a full-screen overlay per output, talking to greetd over
// $GREETD_SOCK. Without greetd it runs as a preview that accepts nothing.
class Greeter : public EventSource {
public:
    Greeter();
    ~Greeter() override;
    bool start();

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    void addOutput(wl::Output* output);
    void send(const nlohmann::json& request);
    void handle(const nlohmann::json& response);
    void beginSession();
    void submit(const std::string& answer);
    void remember() const;
    void forEachView(const std::function<void(shell::LockView*)>& fn);

    // greetd answers requests in order; remember what each reply is for
    std::deque<std::string> m_expect;
    bool m_inSession = false, m_prompting = false;
    int m_fd = -1;
    std::string m_buffer;
    std::string m_user, m_session;
    std::map<wl::Output*, std::unique_ptr<wl::LayerSurface>> m_surfaces;
    std::map<wl::Output*, shell::LockView*> m_views;
};

} // namespace m3::greeter
