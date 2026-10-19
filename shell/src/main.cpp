#include "build_config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "ipc/IpcServer.h"
#include "greeter/Greeter.h"
#include "shell/Shell.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <string>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

void usage() {
    std::cout << "material3ui " << M3_VERSION << " — Material 3 Wayland shell\n\n"
              << "usage:\n"
              << "  material3ui              run the shell\n"
              << "  material3ui msg <cmd>    send a command to the running shell (msg help)\n"
              << "  material3ui greeter      run as the greetd login screen\n"
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
        sigaddset(&set, SIGCHLD); // children are reaped on the loop
        sigprocmask(SIG_BLOCK, &set, nullptr);
        m_fd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    }
    ~SignalSource() override { close(m_fd); }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        signalfd_siginfo info;
        while (read(m_fd, &info, sizeof info) == sizeof info) {
            if (info.ssi_signo == SIGCHLD)
                m3::reapChildren();
            else
                m3::EventLoop::get().quit(0);
        }
    }

private:
    int m_fd = -1;
};

// ── Crash reports ──
//
// A crashing shell appends the signal, the time and a backtrace to
// ~/.local/state/material3ui/crash.log (only async-signal-safe calls),
// then dies as before, so the supervisor sees it and restarts the shell.

char g_crashLog[512] = {};

void writeAll(int fd, const char* s) {
    [[maybe_unused]] auto n = write(fd, s, strlen(s));
}

void onCrash(int sig) {
    const int fd = open(g_crashLog, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        char line[96];
        // snprintf isn't async-signal-safe, but this process is ending anyway
        snprintf(line, sizeof line, "\n=== material3ui %s: signal %d (%s) at %ld ===\n", M3_VERSION, sig, strsignal(sig),
                 static_cast<long>(time(nullptr)));
        writeAll(fd, line);
        void* frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
        close(fd);
    }
    // Default action again: the crash ends the process as it would have
    signal(sig, SIG_DFL);
    raise(sig);
}

void installCrashHandlers() {
    const char* state = getenv("XDG_STATE_HOME");
    const std::string dir = (state && *state ? std::string(state) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.local/state") + "/material3ui";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    snprintf(g_crashLog, sizeof g_crashLog, "%s/crash.log", dir.c_str());
    // backtrace() loads libgcc on first use: not inside a signal handler
    void* warm[1];
    backtrace(warm, 1);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        signal(sig, onCrash);
    // An exception nothing caught: its message first, then the crash
    std::set_terminate([] {
        const int fd = open(g_crashLog, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) {
            std::string what = "unknown exception";
            if (auto e = std::current_exception()) {
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception& ex) {
                    what = ex.what();
                } catch (...) {
                }
            }
            writeAll(fd, ("\nuncaught exception: " + what + "\n").c_str());
            close(fd);
        }
        std::abort();
    });
}

// ── Supervisor ──
//
// `material3ui` runs the shell as a child and restarts it if it crashes, so
// a bug never leaves the desktop without its bar and panels. A clean exit
// (quit, SIGTERM from `pkill`, an update) ends both. Five crashes within a
// minute stop it, so a broken setup doesn't loop.

pid_t g_child = 0;

void forward(int sig) {
    if (g_child > 0) kill(g_child, sig);
}

void notify(const char* title, const char* body) {
    const char* argv[] = {"notify-send", "-a", "Material3UI", "-u", "critical", title, body, nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "notify-send", nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0)
        waitpid(pid, nullptr, 0);
}

bool crashSignal(int sig) {
    return sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS || sig == SIGFPE || sig == SIGILL || sig == SIGTRAP;
}

// Returns in the child (true: run the shell) or with the exit code to end with
bool supervise(int& exitCode) {
    std::vector<time_t> crashes;
    for (;;) {
        const pid_t pid = fork();
        if (pid < 0) return true; // can't supervise: run directly
        if (pid == 0) {
            for (int sig : {SIGTERM, SIGINT, SIGHUP}) signal(sig, SIG_DFL);
            return true;
        }
        g_child = pid;
        for (int sig : {SIGTERM, SIGINT, SIGHUP}) signal(sig, forward);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        g_child = 0;
        if (WIFEXITED(status)) {
            exitCode = WEXITSTATUS(status);
            return false;
        }
        const int sig = WTERMSIG(status);
        if (!crashSignal(sig)) {
            exitCode = 128 + sig;
            return false;
        }
        const time_t now = time(nullptr);
        std::erase_if(crashes, [now](time_t t) { return now - t > 60; });
        crashes.push_back(now);
        if (crashes.size() > 5) {
            notify("Material3UI keeps crashing", "Stopped restarting it. Details in ~/.local/state/material3ui/crash.log");
            exitCode = 1;
            return false;
        }
        notify("Material3UI restarted after a crash", "Details in ~/.local/state/material3ui/crash.log");
        sleep(1);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty()) {
        if (args[0] == "msg") return m3::ipcSend({args.begin() + 1, args.end()});
        if (args[0] == "greeter") {
            SignalSource signals;
            m3::EventLoop::get().add(&signals);
            m3::greeter::Greeter greeter;
            if (!greeter.start()) return 1;
            return m3::EventLoop::get().run();
        }
        if (args[0] == "--version" || args[0] == "-v") {
            std::cout << M3_VERSION << '\n';
            return 0;
        }
        usage();
        return args[0] == "--help" || args[0] == "-h" ? 0 : 2;
    }

    // Supervised unless asked not to (debugging, or already a child)
    if (!getenv("M3_NO_SUPERVISOR")) {
        int code = 0;
        if (!supervise(code)) return code;
    }
    installCrashHandlers();

    SignalSource signals;
    m3::EventLoop::get().add(&signals);

    m3::shell::Shell shell;
    if (!shell.start()) return 1;
    return m3::EventLoop::get().run();
}
