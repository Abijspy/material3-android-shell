#pragma once

#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Runs `command` through /bin/sh in its own session, detached from the shell.
bool spawn(const std::string& command);

// Runs argv asynchronously, collecting stdout; `done` gets the exit code
// (-1 if it could not start) and the output, on the event loop.
void run(const std::vector<std::string>& argv, std::function<void(int code, const std::string& out)> done,
         const std::string& input = {});

// Called on SIGCHLD: reaps every exited child and completes run() jobs
void reapChildren();

} // namespace m3
