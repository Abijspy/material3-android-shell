#include "services/Auth.h"

#include "core/EventLoop.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <security/pam_appl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace m3::auth {

namespace {

struct Conversation {
    std::string password;
    std::string message;
};

int converse(int count, const pam_message** msgs, pam_response** out, void* data) {
    auto* conv = static_cast<Conversation*>(data);
    auto* replies = static_cast<pam_response*>(std::calloc(static_cast<size_t>(count), sizeof(pam_response)));
    if (!replies) return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        switch (msgs[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            replies[i].resp = strdup(conv->password.c_str());
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            if (msgs[i]->msg) conv->message = msgs[i]->msg;
            break;
        default:
            break;
        }
    }
    *out = replies;
    return PAM_SUCCESS;
}

} // namespace

std::string currentUser() {
    if (const passwd* pw = getpwuid(getuid())) return pw->pw_name;
    const char* u = std::getenv("USER");
    return u ? u : "";
}

std::string realName(const std::string& user) {
    const passwd* pw = getpwnam(user.c_str());
    if (!pw || !pw->pw_gecos || !*pw->pw_gecos) return user;
    std::string gecos = pw->pw_gecos;
    gecos = gecos.substr(0, gecos.find(','));
    return gecos.empty() ? user : gecos;
}

void check(const std::string& user, const std::string& password, std::function<void(bool, const std::string&)> done) {
    std::thread([user, pw = std::string(password), done = std::move(done)]() mutable {
        const char* service = std::filesystem::exists("/etc/pam.d/material3ui") ? "material3ui" : "login";
        Conversation conv{pw, {}};
        pam_conv pc{converse, &conv};
        pam_handle_t* handle = nullptr;
        int r = pam_start(service, user.c_str(), &pc, &handle);
        if (r == PAM_SUCCESS) r = pam_authenticate(handle, 0);
        const bool ok = r == PAM_SUCCESS;
        std::string message = !conv.message.empty() ? conv.message
                            : ok                    ? ""
                            : r == PAM_MAXTRIES || r == PAM_AUTHTOK_ERR ? "Too many attempts; try again later"
                                                                         : "Wrong password";
        if (handle) pam_end(handle, r);
        // Don't keep the password around longer than needed
        std::fill(conv.password.begin(), conv.password.end(), '\0');
        std::fill(pw.begin(), pw.end(), '\0');
        EventLoop::get().post([done = std::move(done), ok, message] { done(ok, message); });
    }).detach();
}

} // namespace m3::auth
