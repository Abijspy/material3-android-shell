#include "build_config.h"
#include "core/EventLoop.h"
#include "ipc/IpcServer.h"
#include "shell/Shell.h"

#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/signalfd.h>
#include <unistd.h>
#include <vector>

namespace {

void usage() {
    std::cout << "material3ui " << M3_VERSION << " — Material 3 Wayland shell\n\n"
              << "usage:\n"
              << "  material3ui              run the shell\n"
              << "  material3ui msg <cmd>    send a command to the running shell (msg help)\n"
              << "  material3ui --version\n";
}

// SIGINT/SIGTERM through the event loop, so shutdown runs destructors
class SignalSource : public m3::EventSource {
public:
    SignalSource() {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigprocmask(SIG_BLOCK, &set, nullptr);
        m_fd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    }
    ~SignalSource() override { close(m_fd); }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        signalfd_siginfo info;
        while (read(m_fd, &info, sizeof info) == sizeof info) m3::EventLoop::get().quit(0);
    }

private:
    int m_fd = -1;
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty()) {
        if (args[0] == "msg") return m3::ipcSend({args.begin() + 1, args.end()});
        if (args[0] == "--version" || args[0] == "-v") {
            std::cout << M3_VERSION << '\n';
            return 0;
        }
        usage();
        return args[0] == "--help" || args[0] == "-h" ? 0 : 2;
    }

    // Launched apps are reaped by the kernel; we never wait on them
    std::signal(SIGCHLD, SIG_IGN);

    SignalSource signals;
    m3::EventLoop::get().add(&signals);

    m3::shell::Shell shell;
    if (!shell.start()) return 1;
    return m3::EventLoop::get().run();
}
