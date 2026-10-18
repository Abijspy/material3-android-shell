#pragma once

#include <string>

namespace m3 {

// Runs `command` through /bin/sh in its own session, detached from the shell
// (children are reaped automatically: SIGCHLD is ignored in main).
bool spawn(const std::string& command);

} // namespace m3
