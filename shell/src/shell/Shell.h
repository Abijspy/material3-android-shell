#pragma once

#include "core/Signal.h"

#include <map>
#include <memory>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

class Bar;

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

    std::map<wl::Output*, std::unique_ptr<Bar>> m_bars;
    bool m_barsVisible = true;
};

} // namespace m3::shell
