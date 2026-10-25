#pragma once

#include "core/Signal.h"

#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace m3 {

// Hooks: your own commands, run when something happens (Settings → Hooks):
// the screen locks, the wallpaper changes, the battery runs low, a
// notification arrives… Each event takes a list of commands, run one after
// another through sh. What happened reaches them as environment variables
// (M3_EVENT, and the event's own: $M3_NOTIFICATION_SUMMARY…), never written
// into the command, so a notification's words can't run as one. Their
// output goes to a log (logFile()). In config.json:
//   "hooks": {"session_locked": ["playerctl pause"], "started": "…"}
// (a single command as a string is taken too).
class Hooks {
public:
    struct Variable {
        const char* name;   // M3_BATTERY_PERCENT
        const char* sample; // what Run sends, trying it out
    };
    struct Event {
        const char* id; // session_locked: config.json's key
        const char* icon;
        const char* name;
        const char* when;
        const char* group; // Settings' heading
        std::vector<Variable> vars;
    };
    using Env = std::vector<std::pair<std::string, std::string>>;
    struct Result {
        std::time_t at = 0;
        int code = 0; // the last command's exit status (-1: it couldn't start)
        bool test = false;
    };

    static Hooks& get();
    // Listens to the shell's services for their events, from here on (once,
    // at startup; what's true already fires nothing)
    void watch();
    static const std::vector<Event>& events();
    static const Event* find(const std::string& id);
    static std::string logFile(); // ~/.local/state/material3ui/hooks.log
    // Every event as it happens, a JSON line each, hooks or not, for scripts
    // and test runs to follow (tail -f): $XDG_RUNTIME_DIR/material3ui/
    // events.jsonl, yours alone and gone at logout. Settings → Hooks can stop it
    static std::string eventsFile();
    // `material3ui events …`, for scripts and tests, without the shell's IPC:
    //   events [follow]: each event as it comes, a JSON line each
    //   events wait <event|*> [--since <ms>] [--timeout <s>]: the first that
    //     happens (or happened at or after <ms>, so one already over isn't
    //     missed), then exits; 1 when none comes in time (30 s)
    static int cli(const std::vector<std::string>& args);

    bool enabled() const; // Settings → Hooks → Run hooks
    std::vector<std::string> commands(const std::string& event) const;
    void setCommands(const std::string& event, const std::vector<std::string>& commands);
    bool paused(const std::string& event) const;
    void setPaused(const std::string& event, bool paused);

    // An event happened: its commands run (unless hooks are off, or it's
    // paused)
    void fire(const std::string& event, const Env& env = {});
    // The same, then `then` once they've finished, or after `waitMs` at the
    // most (just before logging out, rebooting…)
    void fireThen(const std::string& event, const Env& env, std::function<void()> then, int waitMs = 5000);
    // Runs its commands now, switches or not, with the sample values
    void test(const std::string& event);
    // As if it happened (material3ui msg hooks fire): recorded as such
    void simulate(const std::string& event, const Env& env);

    const Result* last(const std::string& event) const;
    Signal<> ran; // a hook finished: how it went (Settings shows it)

private:
    Hooks() = default;
    void record(const std::string& event, const Env& env, bool simulated);
    void runAll(const std::string& event, std::vector<std::string> commands, const Env& env, bool test, std::function<void()> done);
    std::map<std::string, Result> m_last;
};

} // namespace m3
