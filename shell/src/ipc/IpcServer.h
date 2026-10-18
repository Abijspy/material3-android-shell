#pragma once

#include "core/EventLoop.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// Line protocol over $XDG_RUNTIME_DIR/material3ui/ipc.sock: the client sends
// "command arg1 arg2\n", the shell replies with text and closes. Modules
// register the commands they own.
class IpcServer : public EventSource {
public:
    using Args = std::vector<std::string>;
    using Handler = std::function<std::string(const Args&)>;

    static IpcServer& get();
    ~IpcServer() override;

    // Fails if another shell instance already owns the socket
    bool listen();
    void add(const std::string& command, const std::string& help, Handler handler);
    std::string help() const;

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    IpcServer() = default;
    std::string run(const std::string& line);

    // A connected client; reads until newline, answers, closes
    class Client : public EventSource {
    public:
        Client(IpcServer& server, int fd) : m_server(server), m_fd(fd) {}
        ~Client() override;
        int fd() const override { return m_fd; }
        void dispatch(short revents) override;

    private:
        IpcServer& m_server;
        int m_fd;
        std::string m_buffer;
    };
    void finish(Client* client);

    struct Command {
        std::string help;
        Handler handler;
    };
    std::map<std::string, Command> m_commands;
    std::vector<std::unique_ptr<Client>> m_clients;
    int m_fd = -1;
    std::string m_path;
};

// `material3ui msg …`: send one command to the running shell, print the reply
int ipcSend(const std::vector<std::string>& args);

} // namespace m3
