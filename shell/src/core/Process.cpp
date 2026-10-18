#include "core/Process.h"

#include "core/EventLoop.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <map>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace m3 {

namespace {

// One run() child: its stdout pipe is an event source; the job completes
// once the pipe hits EOF and the child has been reaped
class Job : public EventSource {
public:
    pid_t pid = -1;
    int out = -1;
    std::string buffer;
    bool eof = false, exited = false;
    int code = -1;
    std::function<void(int, const std::string&)> done;

    ~Job() override {
        if (out >= 0) close(out);
    }
    int fd() const override { return out; }
    void dispatch(short) override;
    void maybeFinish();
};

std::map<pid_t, std::unique_ptr<Job>>& jobs() {
    static std::map<pid_t, std::unique_ptr<Job>> j;
    return j;
}

void Job::dispatch(short) {
    char buf[8192];
    for (;;) {
        const ssize_t n = read(out, buf, sizeof buf);
        if (n > 0) {
            buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            eof = true;
            EventLoop::get().remove(this);
        }
        break;
    }
    maybeFinish();
}

void Job::maybeFinish() {
    if (!eof || !exited) return;
    const pid_t p = pid;
    // Hand off and destroy outside our own dispatch
    EventLoop::get().defer([p] {
        auto it = jobs().find(p);
        if (it == jobs().end()) return;
        auto job = std::move(it->second);
        jobs().erase(it);
        if (job->done) job->done(job->code, job->buffer);
    });
}

posix_spawnattr_t detachedAttr() {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Own session: closing the shell must not take launched apps with it
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    sigset_t none, def;
    sigemptyset(&none);
    sigfillset(&def);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &def);
    return attr;
}

} // namespace

bool spawn(const std::string& command) {
    auto attr = detachedAttr();
    pid_t pid;
    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    const int r = posix_spawn(&pid, "/bin/sh", nullptr, &attr, const_cast<char* const*>(argv), environ);
    posix_spawnattr_destroy(&attr);
    return r == 0;
}

void run(const std::vector<std::string>& argv, std::function<void(int, const std::string&)> done,
         const std::string& input) {
    int outPipe[2], inPipe[2];
    if (pipe2(outPipe, O_CLOEXEC) < 0) {
        if (done) done(-1, {});
        return;
    }
    if (pipe2(inPipe, O_CLOEXEC) < 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        if (done) done(-1, {});
        return;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, inPipe[0], 0);
    posix_spawn_file_actions_adddup2(&fa, outPipe[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    auto attr = detachedAttr();

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid;
    const int r = posix_spawnp(&pid, args[0], &fa, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(outPipe[1]);
    close(inPipe[0]);
    if (r != 0) {
        close(outPipe[0]);
        close(inPipe[1]);
        if (done) done(-1, {});
        return;
    }
    // Small inputs (passwords, clipboard ids) fit the pipe buffer
    if (!input.empty()) [[maybe_unused]] auto w = write(inPipe[1], input.data(), input.size());
    close(inPipe[1]);

    fcntl(outPipe[0], F_SETFL, O_NONBLOCK);
    auto job = std::make_unique<Job>();
    job->pid = pid;
    job->out = outPipe[0];
    job->done = std::move(done);
    EventLoop::get().add(job.get());
    jobs()[pid] = std::move(job);
}

void reapChildren() {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        auto it = jobs().find(pid);
        if (it == jobs().end()) continue; // a detached spawn
        it->second->exited = true;
        it->second->code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        it->second->maybeFinish();
    }
}

} // namespace m3
