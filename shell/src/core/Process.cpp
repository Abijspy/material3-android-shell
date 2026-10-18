#include "core/Process.h"

#include <csignal>
#include <spawn.h>
#include <unistd.h>

extern char** environ;

namespace m3 {

bool spawn(const std::string& command) {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Own session: closing the shell must not take launched apps with it
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    pid_t pid;
    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    const int r = posix_spawn(&pid, "/bin/sh", nullptr, &attr, const_cast<char* const*>(argv), environ);
    posix_spawnattr_destroy(&attr);
    return r == 0;
}

} // namespace m3
