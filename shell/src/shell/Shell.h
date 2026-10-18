#pragma once

#include "core/Signal.h"

#include <map>
#include <memory>
#include <string>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

class Bar;
class Popup;

// Owns every surface and wires services to them: one bar per output, kept in
// step with hotplug, plus the IPC commands the shell itself answers.
class Shell {
public:
    Shell();
    ~Shell();

    bool start();

private:
    void addOutput(wl::Output* output);
    void removeOutput(wl::Output* output);
    void registerIpc();
    void restyle(bool relayout);
    void togglePopup(const std::string& name, wl::Output* output, float anchorX);
    wl::Output* focusedOutput() const;

    std::map<wl::Output*, std::unique_ptr<Bar>> m_bars;
    std::unique_ptr<Popup> m_popup;
    bool m_barsVisible = true;
};

} // namespace m3::shell
