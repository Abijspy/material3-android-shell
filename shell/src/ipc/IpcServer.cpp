#include "ipc/IpcServer.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace m3 {

namespace {

sockaddr_un socketAddress(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof addr.sun_path - 1);
    return addr;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> std::quoted(word)) out.push_back(word);
    return out;
}

} // namespace

IpcServer& IpcServer::get() {
    static IpcServer instance;
    return instance;
}

IpcServer::~IpcServer() {
    if (m_fd >= 0) {
        close(m_fd);
        unlink(m_path.c_str());
    }
}

bool IpcServer::listen() {
    m_path = paths::ipcSocket();
    std::error_code ec;
    std::filesystem::create_directories(paths::runtimeDir(), ec);

    // A socket that still accepts connections belongs to a running shell
    if (std::filesystem::exists(m_path)) {
        const int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        auto addr = socketAddress(m_path);
        const bool alive = ::connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
        close(probe);
        if (alive) {
            log::error("another material3ui is already running");
            return false;
        }
        unlink(m_path.c_str());
    }

    m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    auto addr = socketAddress(m_path);
    if (m_fd < 0 || bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || ::listen(m_fd, 8) < 0) {
        log::error("ipc socket {}: {}", m_path, std::strerror(errno));
        return false;
    }
    EventLoop::get().add(this);

    add("help", "List commands", [this](const Args&) { return help(); });
    return true;
}

void IpcServer::add(const std::string& command, const std::string& help, Handler handler) {
    m_commands[command] = Command{help, std::move(handler)};
}

std::string IpcServer::help() const {
    std::string out;
    for (const auto& [name, cmd] : m_commands) out += std::format("  {:<18} {}\n", name, cmd.help);
    return out;
}

std::string IpcServer::run(const std::string& line) {
    const auto args = split(line);
    if (args.empty()) return "error: empty command\n";
    auto it = m_commands.find(args[0]);
    if (it == m_commands.end()) return std::format("error: unknown command '{}' (try help)\n", args[0]);
    std::string reply = it->second.handler(Args(args.begin() + 1, args.end()));
    if (!reply.empty() && reply.back() != '\n') reply += '\n';
    return reply;
}

void IpcServer::dispatch(short) {
    for (;;) {
        const int client = accept4(m_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) break;
        m_clients.push_back(std::make_unique<Client>(*this, client));
        EventLoop::get().add(m_clients.back().get());
    }
}

void IpcServer::finish(Client* client) {
    EventLoop::get().remove(client);
    // Defer the delete: we are inside the client's own dispatch()
    EventLoop::get().defer([this, client] {
        std::erase_if(m_clients, [client](auto& c) { return c.get() == client; });
    });
}

IpcServer::Client::~Client() {
    close(m_fd);
}

void IpcServer::Client::dispatch(short) {
    char buf[1024];
    ssize_t n;
    while ((n = ::read(m_fd, buf, sizeof buf)) > 0) m_buffer.append(buf, static_cast<size_t>(n));
    const auto nl = m_buffer.find('\n');
    const bool closed = n == 0;
    if (nl == std::string::npos && !closed) {
        if (m_buffer.size() > 65536) m_server.finish(this);
        return;
    }
    const std::string reply = m_server.run(m_buffer.substr(0, nl));
    // Replies are small; a blocking write keeps this simple
    ssize_t off = 0;
    while (off < static_cast<ssize_t>(reply.size())) {
        const ssize_t w = ::send(m_fd, reply.data() + off, reply.size() - static_cast<size_t>(off), MSG_NOSIGNAL);
        if (w <= 0 && errno != EAGAIN) break;
        if (w > 0) off += w;
    }
    m_server.finish(this);
}

int ipcSend(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: material3ui msg <command> [args…]  (material3ui msg help)\n";
        return 2;
    }
    const std::string path = paths::ipcSocket();
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    auto addr = socketAddress(path);
    if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        std::cerr << "material3ui: the shell is not running (no " << path << ")\n";
        return 1;
    }
    std::ostringstream line;
    for (size_t i = 0; i < args.size(); ++i) line << (i ? " " : "") << std::quoted(args[i]);
    line << '\n';
    const std::string msg = line.str();
    if (::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL) < 0) {
        close(fd);
        return 1;
    }
    std::string reply;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) reply.append(buf, static_cast<size_t>(n));
    close(fd);
    std::cout << reply;
    return reply.starts_with("error:") ? 1 : 0;
}

} // namespace m3
