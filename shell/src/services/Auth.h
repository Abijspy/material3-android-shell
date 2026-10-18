#pragma once

#include <functional>
#include <string>

namespace m3::auth {

// The user running the shell
std::string currentUser();
std::string realName(const std::string& user);

// Checks `password` for `user` with PAM on a worker thread (PAM may sleep
// for seconds after a failure). `done` runs on the event loop. The PAM
// service is "material3ui" when /etc/pam.d/material3ui exists, else "login".
void check(const std::string& user, const std::string& password,
           std::function<void(bool ok, const std::string& message)> done);

} // namespace m3::auth
